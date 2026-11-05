#include "vg_config_confirmation_ui.h"

#include "model/vg_model.h"
#include "shell/vg_shell.h"
#include "theme/vg_theme.h"
#include "vg_confirm_dialog.h"
#include "vg_display.h"
#include "widgets/vg_widgets.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

#define VG_CONFIG_UI_POLL_MS 250

static struct vg_config_confirmation s_confirmation;
static bool s_initialized;
static lv_obj_t * s_modal;
static char s_shown_confirmation_id[VG_CONFIG_HEX32_LEN];

extern int vg_config_mqtt_runtime_bind_confirmation(
    struct vg_config_confirmation *confirmation,
    bool (*alarm_active)(void *ctx),
    void *alarm_ctx) __attribute__((weak));

static void delete_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    s_modal = NULL;
    s_shown_confirmation_id[0] = '\0';
}

static void close_surface(void)
{
    if(s_modal && lv_obj_is_valid(s_modal)) {
        lv_obj_delete(s_modal);
    }
    s_modal = NULL;
    s_shown_confirmation_id[0] = '\0';
}

static lv_obj_t * make_button(lv_obj_t *parent, const char *text,
                              bool primary, lv_event_cb_t cb)
{
    lv_obj_t *btn = lv_button_create(parent);
    lv_obj_t *label = lv_label_create(btn);

    lv_obj_set_size(btn, 112, VG_MIN_TOUCH_H);
    vg_style_apply_btn(btn, primary);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, vg_font_ui(), 0);
    lv_obj_center(label);
    if(cb) lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);
    return btn;
}

static lv_obj_t * make_wrapped_label(lv_obj_t *parent, const char *text,
                                     bool muted)
{
    lv_obj_t *label = lv_label_create(parent);

    lv_label_set_text(label, text ? text : "");
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(label, lv_pct(100));
    vg_style_apply_label(label, muted);
    lv_obj_set_style_text_font(label, vg_font_small(), 0);
    return label;
}

static lv_obj_t * make_section(lv_obj_t *parent, const char *title)
{
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_t *label = lv_label_create(card);

    lv_obj_set_width(card, lv_pct(100));
    lv_obj_set_height(card, LV_SIZE_CONTENT);
    vg_style_apply_card(card);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(card, 7, 0);
    lv_obj_set_style_pad_row(card, 4, 0);
    lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    lv_label_set_text(label, title ? title : "");
    vg_style_apply_label(label, false);
    lv_obj_set_style_text_font(label, vg_font_small(), 0);
    return card;
}

static void add_point(lv_obj_t *parent,
                      const struct vg_config_candidate_point *point)
{
    char title[128];
    char detail[192];
    lv_obj_t *card = make_section(parent, "");

    snprintf(title, sizeof(title), "%s  [%s]", point->name, point->id);
    make_wrapped_label(card, title, false);
    snprintf(detail, sizeof(detail),
             "从站 %u · FC%u · 寄存器 0x%04X · %s · 倍率 %.4g · 偏移 0 · 单位 %s",
             (unsigned)point->addr, (unsigned)point->fc,
             (unsigned)point->reg, point->dtype,
             (double)point->scale, point->unit[0] ? point->unit : "-");
    make_wrapped_label(card, detail, true);
    if(point->original_address[0] != '\0') {
        snprintf(detail, sizeof(detail), "手册原址 %s · 地址起始 %u",
                 point->original_address,
                 (unsigned)point->address_base);
        make_wrapped_label(card, detail, true);
    }
}

static void add_evidence(lv_obj_t *parent,
                         const struct vg_config_evidence *evidence)
{
    char head[64];
    lv_obj_t *card = make_section(parent, "");

    snprintf(head, sizeof(head), "%s · 手册第 %u 页",
             evidence->id, (unsigned)evidence->page);
    make_wrapped_label(card, head, false);
    make_wrapped_label(card, evidence->quote, true);
}

static void submit_action(enum vg_config_confirmation_action_kind_e kind)
{
    const vg_alarm_t *alarm = vg_model_get_active_alarm();
    bool alarm_active = alarm != NULL && alarm->active;
    int rc = vg_config_confirmation_submit(&s_confirmation, kind,
                                            alarm_active);

    close_surface();
    if(rc == -EALREADY || rc == -ESTALE) {
        vg_shell_toast("候选已失效，未添加");
    }
    else if(rc != 0) {
        vg_shell_toast("本地确认未送达");
    }
}

static void on_cancel(lv_event_t *e)
{
    LV_UNUSED(e);
    submit_action(VG_CONFIG_CONFIRMATION_CANCEL);
}

static void on_confirm(lv_event_t *e)
{
    LV_UNUSED(e);
    submit_action(VG_CONFIG_CONFIRMATION_CONFIRM);
}

static void create_surface(const struct vg_config_pending_preview *pending)
{
    const struct vg_config_candidate *candidate = &pending->candidate;
    lv_obj_t *card;
    lv_obj_t *header;
    lv_obj_t *title;
    lv_obj_t *body;
    lv_obj_t *summary;
    lv_obj_t *footer;
    char text[320];
    uint32_t i;

    close_surface();
    s_modal = lv_obj_create(lv_layer_top());
    lv_obj_set_size(s_modal, VG_DISP_W, VG_DISP_H);
    lv_obj_set_pos(s_modal, 0, 0);
    lv_obj_set_style_bg_color(s_modal, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(s_modal, LV_OPA_50, 0);
    lv_obj_set_style_border_width(s_modal, 0, 0);
    lv_obj_set_style_pad_all(s_modal, 0, 0);
    lv_obj_remove_flag(s_modal, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_modal, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_modal, delete_cb, LV_EVENT_DELETE, NULL);

    card = lv_obj_create(s_modal);
    lv_obj_set_size(card, 448, 256);
    lv_obj_center(card);
    vg_style_apply_card(card);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_all(card, 10, 0);
    lv_obj_set_style_pad_row(card, 7, 0);
    lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    header = lv_obj_create(card);
    lv_obj_set_width(header, lv_pct(100));
    lv_obj_set_height(header, 28);
    lv_obj_set_style_bg_opa(header, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_set_style_pad_all(header, 0, 0);
    lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(header, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(header, LV_OBJ_FLAG_SCROLLABLE);

    title = lv_label_create(header);
    lv_label_set_text(title, "云端候选待确认");
    vg_style_apply_label(title, false);
    lv_obj_set_style_text_font(title, vg_font_ui(), 0);
    vg_risk_badge_create(header, "medium");

    lv_snprintf(text, sizeof(text), "任务 %s · 修订 %u",
                pending->task_id, (unsigned)pending->revision);
    summary = make_wrapped_label(card, text, true);
    lv_obj_set_height(summary, 18);

    body = lv_obj_create(card);
    lv_obj_set_width(body, lv_pct(100));
    lv_obj_set_flex_grow(body, 1);
    lv_obj_set_style_bg_opa(body, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(body, 0, 0);
    lv_obj_set_style_pad_all(body, 0, 0);
    lv_obj_set_style_pad_row(body, 6, 0);
    lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(body, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(body, LV_SCROLLBAR_MODE_AUTO);

    lv_snprintf(text, sizeof(text),
                "%s · 从站 %u · 9600 · %u 个新增测点",
                candidate->model, (unsigned)candidate->slave_address,
                (unsigned)candidate->point_count);
    make_wrapped_label(body, text, false);
    lv_snprintf(text, sizeof(text), "手册 %s", candidate->manual_id);
    make_wrapped_label(body, text, true);

    for(i = 0; i < candidate->point_count &&
              i < VG_CONFIG_CANDIDATE_POINT_MAX; i++) {
        add_point(body, &candidate->points[i]);
    }
    for(i = 0; i < candidate->evidence_count &&
              i < VG_CONFIG_EVIDENCE_MAX; i++) {
        add_evidence(body, &candidate->evidence[i]);
    }
    lv_snprintf(text, sizeof(text), "手册 SHA-256 %s",
                candidate->manual_sha256);
    make_wrapped_label(body, text, true);
    lv_snprintf(text, sizeof(text), "候选 SHA-256 %s",
                pending->candidate_hash);
    make_wrapped_label(body, text, true);

    footer = lv_obj_create(card);
    lv_obj_set_width(footer, lv_pct(100));
    lv_obj_set_height(footer, VG_MIN_TOUCH_H);
    lv_obj_set_style_bg_opa(footer, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(footer, 0, 0);
    lv_obj_set_style_pad_all(footer, 0, 0);
    lv_obj_set_flex_flow(footer, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(footer, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(footer, 8, 0);
    lv_obj_remove_flag(footer, LV_OBJ_FLAG_SCROLLABLE);
    make_button(footer, "取消", false, on_cancel);
    make_button(footer, "确认添加", true, on_confirm);

    lv_snprintf(s_shown_confirmation_id, sizeof(s_shown_confirmation_id),
                "%s", pending->confirmation_id);
}

void vg_config_confirmation_ui_poll(void)
{
    const vg_alarm_t *alarm;
    const struct vg_config_pending_preview *pending;
    bool alarm_active;

    if(!s_initialized) return;
    alarm = vg_model_get_active_alarm();
    alarm_active = alarm != NULL && alarm->active;
    (void)vg_config_confirmation_sync(&s_confirmation, alarm_active);

    if(!vg_config_confirmation_is_visible(&s_confirmation) ||
       vg_confirm_dialog_is_open()) {
        close_surface();
        return;
    }

    pending = vg_config_confirmation_peek(&s_confirmation);
    if(pending == NULL) {
        close_surface();
        return;
    }

    if(s_modal == NULL ||
       strcmp(s_shown_confirmation_id, pending->confirmation_id) != 0) {
        create_surface(pending);
    }
}

static void poll_timer_cb(lv_timer_t *timer)
{
    LV_UNUSED(timer);
    vg_config_confirmation_ui_poll();
}

static bool confirmation_alarm_active(void *ctx)
{
    const vg_alarm_t *alarm = vg_model_get_active_alarm();
    LV_UNUSED(ctx);
    return alarm != NULL && alarm->active;
}

void vg_config_confirmation_ui_init(void)
{
    if(s_initialized) return;
    if(vg_config_confirmation_init(&s_confirmation, NULL) != 0) return;
    s_initialized = true;
    lv_timer_create(poll_timer_cb, VG_CONFIG_UI_POLL_MS, NULL);
    (void)vg_config_confirmation_ui_attach_runtime();
}

int vg_config_confirmation_ui_attach_runtime(void)
{
    if(!s_initialized) return -1;
    if(vg_config_mqtt_runtime_bind_confirmation == NULL) return -1;
    return vg_config_mqtt_runtime_bind_confirmation(
        &s_confirmation, confirmation_alarm_active, NULL);
}

int vg_config_confirmation_ui_bind(
    const struct vg_config_confirmation_ports *ports)
{
    int rc;

    if(!s_initialized) {
        vg_config_confirmation_ui_init();
    }
    if(!s_initialized) return -1;
    rc = vg_config_confirmation_bind(&s_confirmation, ports);
    if(rc == 0) {
        close_surface();
        vg_config_confirmation_ui_poll();
    }
    return rc;
}

bool vg_config_confirmation_ui_is_open(void)
{
    return s_modal != NULL && lv_obj_is_valid(s_modal);
}

void vg_config_confirmation_ui_back(void)
{
    if(!vg_config_confirmation_ui_is_open()) return;
    submit_action(VG_CONFIG_CONFIRMATION_CANCEL);
}
