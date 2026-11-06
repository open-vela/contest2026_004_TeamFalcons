#include "vg_pages.h"
#include "widgets/vg_widgets.h"
#include "theme/vg_theme.h"
#include "model/vg_model.h"
#include "vg_display.h"
#include <stdio.h>
#include <string.h>
#ifdef __NuttX__
#include <nuttx/config.h>
#endif

typedef struct {
    lv_obj_t * root;
    lv_obj_t * idle_box;
    lv_obj_t * offer_box;
    lv_obj_t * btn_install;
    lv_obj_t * of_ver;
    lv_obj_t * of_size;
    lv_obj_t * of_note;
    lv_obj_t * of_warn;
    lv_obj_t * of_hint;
    lv_obj_t * progress_box;
    lv_obj_t * bar;
    lv_obj_t * bar_lab;
    lv_obj_t * ok_box;
    lv_obj_t * fail_box;
    lv_obj_t * fail_lab;
} ota_ctx_t;

static ota_ctx_t s_ota_ui;
#define VG_OTA_INSTALL_HOLD_MS 2000u
static uint32_t s_press_ms;
static bool s_pressed;
static char s_press_task[41];

static void show_only(lv_obj_t * show)
{
    lv_obj_t * boxes[] = {
        s_ota_ui.idle_box, s_ota_ui.offer_box, s_ota_ui.progress_box,
        s_ota_ui.ok_box, s_ota_ui.fail_box
    };
    size_t i;
    for(i = 0; i < sizeof(boxes) / sizeof(boxes[0]); i++) {
        if(boxes[i] == NULL) continue;
        if(show == boxes[i]) {
            if(lv_obj_has_flag(boxes[i], LV_OBJ_FLAG_HIDDEN))
                lv_obj_remove_flag(boxes[i], LV_OBJ_FLAG_HIDDEN);
        }
        else if(!lv_obj_has_flag(boxes[i], LV_OBJ_FLAG_HIDDEN)) {
            lv_obj_add_flag(boxes[i], LV_OBJ_FLAG_HIDDEN);
        }
    }
}

static void on_install_btn_event(lv_event_t * e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if(code == LV_EVENT_PRESSED) {
        const vg_ota_t *o = vg_model_get_ota();
        s_press_ms = lv_tick_get();
        s_pressed = true;
        snprintf(s_press_task, sizeof(s_press_task), "%s", o->task_id);
    }
    else if(code == LV_EVENT_PRESSING && s_pressed &&
            lv_tick_elaps(s_press_ms) >= VG_OTA_INSTALL_HOLD_MS) {
        const vg_ota_t *o = vg_model_get_ota();
        s_pressed = false;
        if(strcmp(s_press_task, o->task_id) != 0) return;
        if(vg_model_ota_demo_get()) return;
        if(s_ota_ui.of_hint) lv_label_set_text(s_ota_ui.of_hint, "正在检查安装条件...");
        vg_model_ota_accept();
    }
    else if(code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        s_pressed = false;
    }
    else if(code == LV_EVENT_CLICKED) {
        if(s_ota_ui.of_hint) lv_label_set_text(s_ota_ui.of_hint, "请长按 2 秒确认安装");
    }
}

static void on_done(lv_event_t * e)
{
    LV_UNUSED(e);
    vg_nav_back();
}

static void on_retry(lv_event_t * e)
{
    LV_UNUSED(e);
    vg_model_ota_retry();
}

static void on_cancel_offer(lv_event_t * e)
{
    LV_UNUSED(e);
    vg_model_ota_cancel();
}

static void on_back(lv_event_t * e)
{
    LV_UNUSED(e);
    vg_nav_back();
}

static void refresh_ota(void * user)
{
    const vg_ota_t * o;
    char buf[48];
    LV_UNUSED(user);
    if(vg_nav_current() != VG_PAGE_OTA) return;
    if(s_ota_ui.root == NULL || !lv_obj_is_valid(s_ota_ui.root)) return;

    o = vg_model_get_ota();
    if(o == NULL) return;

#ifdef CONFIG_VG_OTA_TEST_FIXTURES
    printf("vgota-page: state=%d demo=%d ver=%s\n",
           (int)o->state, vg_model_ota_demo_get() ? 1 : 0, o->version);
#endif

    switch(o->state) {
        case VG_OTA_OFFER:
            if(s_ota_ui.btn_install) {
                if(vg_model_ota_demo_get()) {
                    lv_obj_add_state(s_ota_ui.btn_install, LV_STATE_DISABLED);
                }
                else {
                    lv_obj_remove_state(s_ota_ui.btn_install, LV_STATE_DISABLED);
                }
            }
            lv_label_set_text(s_ota_ui.of_ver, o->version[0] ? o->version : "--");
            if(o->size_bytes) lv_snprintf(buf, sizeof(buf), "%lu B", (unsigned long)o->size_bytes);
            else lv_snprintf(buf, sizeof(buf), "%d MB", (int)o->size_mb);
            lv_label_set_text(s_ota_ui.of_size, buf);
            lv_label_set_text(s_ota_ui.of_note, o->note[0] ? o->note : "--");
            if(o->error[0] && s_ota_ui.of_hint) {
                lv_label_set_text(s_ota_ui.of_hint, o->error);
            }
            show_only(s_ota_ui.offer_box);
            break;

        case VG_OTA_PROGRESS:
            lv_bar_set_value(s_ota_ui.bar, o->progress_pct, LV_ANIM_OFF);
            lv_snprintf(buf, sizeof(buf), "%d%% · 升级中，请勿断电",
                        (int)o->progress_pct);
            lv_label_set_text(s_ota_ui.bar_lab, buf);
            show_only(s_ota_ui.progress_box);
            break;

        case VG_OTA_DONE_OK:
            show_only(s_ota_ui.ok_box);
            break;

        case VG_OTA_DONE_FAIL:
            lv_label_set_text(s_ota_ui.fail_lab,
                              o->error[0] ? o->error : "升级失败");
            show_only(s_ota_ui.fail_box);
            break;

        case VG_OTA_IDLE:
        default:
            show_only(s_ota_ui.idle_box);
            break;
    }
}

static void on_ota_delete(lv_event_t * e)
{
    LV_UNUSED(e);
    s_pressed = false;
    vg_model_off_change(refresh_ota, NULL);
    memset(&s_ota_ui, 0, sizeof(s_ota_ui));
}

static lv_obj_t * make_btn(lv_obj_t * parent, const char * text,
                           bool primary, lv_event_cb_t cb)
{
    lv_obj_t * btn = lv_button_create(parent);
    lv_obj_t * lab;
    lv_obj_set_size(btn, 118, VG_MIN_TOUCH_H);
    vg_style_apply_btn(btn, primary);
    lab = lv_label_create(btn);
    lv_label_set_text(lab, text);
    lv_obj_set_style_text_font(lab, vg_font_ui(), 0);
    lv_obj_center(lab);
    if(cb) lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);
    return btn;
}

static lv_obj_t * make_step_card(lv_obj_t * parent)
{
    lv_obj_t * card = lv_obj_create(parent);
    vg_style_apply_card(card);
    lv_obj_set_width(card, lv_pct(100));
    lv_obj_set_flex_grow(card, 1);
    lv_obj_set_style_min_height(card, 0, 0);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(card, 4, 0);
    return card;
}

static void build_idle_step(lv_obj_t * parent)
{
    lv_obj_t * box = make_step_card(parent);
    lv_obj_t * lab;
    lv_obj_t * btn_row;
    s_ota_ui.idle_box = box;

    lv_obj_set_flex_align(box, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(box, 8, 0);

    lab = lv_label_create(box);
    lv_label_set_text(lab, "当前无可用更新");
    vg_style_apply_label(lab, false);

    lab = lv_label_create(box);
    lv_label_set_text(lab, "系统已是最新版本");
    vg_style_apply_label(lab, true);

    btn_row = lv_obj_create(box);
    lv_obj_remove_flag(btn_row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(btn_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(btn_row, 0, 0);
    lv_obj_set_style_pad_all(btn_row, 0, 0);
    lv_obj_set_width(btn_row, lv_pct(100));
    lv_obj_set_height(btn_row, VG_MIN_TOUCH_H);
    lv_obj_set_flex_flow(btn_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btn_row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);

    make_btn(btn_row, "返回", true, on_back);
}

static void build_offer_step(lv_obj_t * parent)
{
    lv_obj_t * box = make_step_card(parent);
    lv_obj_t * btn_row;
    lv_obj_t * title;

    s_ota_ui.offer_box = box;

    title = lv_label_create(box);
    lv_label_set_text(title, "发现新固件");
    vg_style_apply_label(title, false);

    /* vg_metric_row_create returns the row; child 1 is the value label. */
    s_ota_ui.of_ver = lv_obj_get_child(vg_metric_row_create(box, "版本", "--"), 1);
    s_ota_ui.of_size = lv_obj_get_child(vg_metric_row_create(box, "大小", "--"), 1);
    s_ota_ui.of_note = lv_obj_get_child(vg_metric_row_create(box, "说明", "--"), 1);

    s_ota_ui.of_warn = lv_label_create(box);
    lv_label_set_text(s_ota_ui.of_warn, "安装后设备将重启，升级期间请勿断电");
    vg_style_apply_label(s_ota_ui.of_warn, true);

    s_ota_ui.of_hint = lv_label_create(box);
    lv_label_set_text(s_ota_ui.of_hint, "");
    lv_obj_set_style_text_color(s_ota_ui.of_hint, vg_color_warn(), 0);

    btn_row = lv_obj_create(box);
    lv_obj_remove_flag(btn_row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(btn_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(btn_row, 0, 0);
    lv_obj_set_style_pad_all(btn_row, 0, 0);
    lv_obj_set_width(btn_row, lv_pct(100));
    lv_obj_set_height(btn_row, VG_MIN_TOUCH_H);
    lv_obj_set_flex_flow(btn_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btn_row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(btn_row, 8, 0);

    s_ota_ui.btn_install = make_btn(btn_row, "长按安装", true, NULL);
    lv_obj_add_event_cb(s_ota_ui.btn_install, on_install_btn_event, LV_EVENT_ALL,
                        NULL);

    make_btn(btn_row, "取消升级", false, on_cancel_offer);
}

static void build_progress_step(lv_obj_t * parent)
{
    lv_obj_t * box = make_step_card(parent);
    lv_obj_t * lab;
    s_ota_ui.progress_box = box;

    lv_obj_set_flex_align(box, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(box, 10, 0);

    lab = lv_label_create(box);
    lv_label_set_text(lab, "OTA 升级中");
    vg_style_apply_label(lab, false);

    s_ota_ui.bar = lv_bar_create(box);
    lv_obj_set_size(s_ota_ui.bar, lv_pct(78), 14);
    lv_obj_set_style_bg_color(s_ota_ui.bar, vg_color_surface(), 0);
    lv_obj_set_style_bg_opa(s_ota_ui.bar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(s_ota_ui.bar, vg_color_border(), 0);
    lv_obj_set_style_border_width(s_ota_ui.bar, 1, 0);
    lv_obj_set_style_radius(s_ota_ui.bar, 4, 0);
    lv_obj_set_style_bg_color(s_ota_ui.bar, vg_color_accent(), LV_PART_INDICATOR);
    lv_bar_set_range(s_ota_ui.bar, 0, 100);
    lv_bar_set_value(s_ota_ui.bar, 0, LV_ANIM_OFF);

    s_ota_ui.bar_lab = lv_label_create(box);
    lv_label_set_text(s_ota_ui.bar_lab, "0% · 升级中，请勿断电");
    vg_style_apply_label(s_ota_ui.bar_lab, true);
    lv_obj_set_style_text_font(s_ota_ui.bar_lab, vg_font_small(), 0);
}

static void build_ok_step(lv_obj_t * parent)
{
    lv_obj_t * box = make_step_card(parent);
    lv_obj_t * lab;
    lv_obj_t * btn_row;
    s_ota_ui.ok_box = box;

    lv_obj_set_flex_align(box, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(box, 8, 0);

    lab = lv_label_create(box);
    lv_label_set_text(lab, "升级成功");
    lv_obj_set_style_text_color(lab, vg_color_ok(), 0);
    lv_obj_set_style_text_font(lab, vg_font_ui(), 0);

    lab = lv_label_create(box);
    lv_label_set_text(lab, "设备已运行新固件");
    vg_style_apply_label(lab, true);

    btn_row = lv_obj_create(box);
    lv_obj_remove_flag(btn_row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(btn_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(btn_row, 0, 0);
    lv_obj_set_style_pad_all(btn_row, 0, 0);
    lv_obj_set_width(btn_row, lv_pct(100));
    lv_obj_set_height(btn_row, VG_MIN_TOUCH_H);
    lv_obj_set_flex_flow(btn_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btn_row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);

    make_btn(btn_row, "完成", true, on_done);
}

static void build_fail_step(lv_obj_t * parent)
{
    lv_obj_t * box = make_step_card(parent);
    lv_obj_t * lab;
    lv_obj_t * btn_row;
    s_ota_ui.fail_box = box;

    lv_obj_set_flex_align(box, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(box, 6, 0);

    lab = lv_label_create(box);
    lv_label_set_text(lab, "升级失败");
    lv_obj_set_style_text_color(lab, vg_color_crit(), 0);
    lv_obj_set_style_text_font(lab, vg_font_ui(), 0);

    s_ota_ui.fail_lab = lv_label_create(box);
    lv_label_set_text(s_ota_ui.fail_lab, "");
    vg_style_apply_label(s_ota_ui.fail_lab, true);

    btn_row = lv_obj_create(box);
    lv_obj_remove_flag(btn_row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(btn_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(btn_row, 0, 0);
    lv_obj_set_style_pad_all(btn_row, 0, 0);
    lv_obj_set_width(btn_row, lv_pct(100));
    lv_obj_set_height(btn_row, VG_MIN_TOUCH_H);
    lv_obj_set_flex_flow(btn_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btn_row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(btn_row, 4, 0);

    make_btn(btn_row, "重试", true, on_retry);
    make_btn(btn_row, "返回", false, on_back);
}

void vg_page_ota_create(lv_obj_t * parent, const void * args)
{
    LV_UNUSED(args);
    memset(&s_ota_ui, 0, sizeof(s_ota_ui));
    s_ota_ui.root = parent;
    lv_obj_add_event_cb(parent, on_ota_delete, LV_EVENT_DELETE, NULL);

    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(parent, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(parent, VG_GAP, 0);
    lv_obj_remove_flag(parent, LV_OBJ_FLAG_SCROLLABLE);

    build_idle_step(parent);
    build_offer_step(parent);
    build_progress_step(parent);
    build_ok_step(parent);
    build_fail_step(parent);

    vg_model_on_change(refresh_ota, NULL);
    refresh_ota(NULL);
}
