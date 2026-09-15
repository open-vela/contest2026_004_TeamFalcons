#include "vg_pages.h"
#include "widgets/vg_widgets.h"
#include "theme/vg_theme.h"
#include "model/vg_model.h"
#include "model/vg_ui_backend.h"
#include "vg_display.h"
#include <string.h>

#define REPORT_POLL_PERIOD_MS 100
#define REPORT_GEN_TIMEOUT_S  300
#define REPORT_BODY_HINT \
    "暂无运行报告。\n\n(板端: /data/velaguard/reports/runtime-report.md)\n" \
    "点右上角刷新可更新本次上电以来的运行概况。"

typedef struct {
    lv_obj_t * root;
    lv_obj_t * title_lab;
    lv_obj_t * body_lab;
    lv_obj_t * body_card;
    lv_obj_t * refresh_btn;
    lv_timer_t * poll_tmr;
    uint32_t last_version;
} report_ctx_t;

static report_ctx_t s_report;

static void report_set_refresh_enabled(bool enabled)
{
    if(s_report.refresh_btn == NULL) {
        return;
    }
    if(enabled) {
        lv_obj_add_flag(s_report.refresh_btn, LV_OBJ_FLAG_CLICKABLE);
    }
    else {
        lv_obj_remove_flag(s_report.refresh_btn, LV_OBJ_FLAG_CLICKABLE);
    }
}

static void apply_snapshot(const vg_ui_report_snapshot_t * snap)
{
    const char * base;
    char buf[1024];

    if(s_report.title_lab == NULL || s_report.body_lab == NULL) {
        return;
    }

    base = (snap->path[0] != '\0') ? strrchr(snap->path, '/') : NULL;
    base = (base != NULL) ? base + 1 : snap->path;
    if(base != NULL && base[0] != '\0') {
        lv_label_set_text_fmt(s_report.title_lab, "运行报告 · %s", base);
    }
    else {
        lv_label_set_text(s_report.title_lab, "运行报告");
    }

    switch(snap->status) {
        case VG_UI_REPORT_READY:
            lv_label_set_text(s_report.body_lab, snap->body[0] ? snap->body : REPORT_BODY_HINT);
            report_set_refresh_enabled(true);
            break;
        case VG_UI_REPORT_GENERATING:
            lv_snprintf(buf, sizeof(buf),
                        "运行报告生成中(MiMo)…\n\n已等待 %us / %us,完成后自动显示。\n"
                        "也可先返回其他页面,生成后回到本页点刷新。",
                        (unsigned)snap->elapsed_s, (unsigned)REPORT_GEN_TIMEOUT_S);
            lv_label_set_text(s_report.body_lab, buf);
            report_set_refresh_enabled(false);
            break;
        case VG_UI_REPORT_READING:
            lv_label_set_text(s_report.body_lab, "正在读取运行报告…");
            report_set_refresh_enabled(false);
            break;
        case VG_UI_REPORT_EMPTY:
            lv_label_set_text(s_report.body_lab, REPORT_BODY_HINT);
            report_set_refresh_enabled(true);
            break;
        case VG_UI_REPORT_ERROR:
        default:
            lv_label_set_text(s_report.body_lab,
                              "运行报告读取或生成失败。\n\n请检查网络与 MiMo 状态，稍后点刷新重试。");
            report_set_refresh_enabled(true);
            break;
    }
}

static void report_poll_cb(lv_timer_t * t)
{
    vg_ui_report_snapshot_t snap;
    LV_UNUSED(t);

    if(s_report.root == NULL) {
        return;
    }

    if(vg_ui_report_snapshot(&snap)) {
        if(snap.version != s_report.last_version) {
            s_report.last_version = snap.version;
            apply_snapshot(&snap);
        }
    }
}

static void on_refresh_clicked(lv_event_t * e)
{
    vg_ui_report_snapshot_t snap;
    LV_UNUSED(e);

    (void)vg_ui_report_request(true, NULL);
    if(vg_ui_report_snapshot(&snap)) {
        s_report.last_version = snap.version;
        apply_snapshot(&snap);
    }
}

static void on_report_delete(lv_event_t * e)
{
    LV_UNUSED(e);
    if(s_report.poll_tmr != NULL) {
        lv_timer_del(s_report.poll_tmr);
        s_report.poll_tmr = NULL;
    }
    memset(&s_report, 0, sizeof(s_report));
}

void vg_page_report_create(lv_obj_t * parent, const void * args)
{
    lv_obj_t * head;
    lv_obj_t * card;
    lv_obj_t * refresh_lab;
    vg_ui_report_snapshot_t snap;
    LV_UNUSED(args);

    memset(&s_report, 0, sizeof(s_report));
    s_report.root = parent;
    lv_obj_add_event_cb(parent, on_report_delete, LV_EVENT_DELETE, NULL);

    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(parent, 6, 0);
    lv_obj_remove_flag(parent, LV_OBJ_FLAG_SCROLLABLE);

    /* Head row: title left, manual refresh right */
    head = lv_obj_create(parent);
    lv_obj_remove_flag(head, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(head, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(head, 0, 0);
    lv_obj_set_style_pad_all(head, 0, 0);
    lv_obj_set_width(head, lv_pct(100));
    lv_obj_set_height(head, VG_MIN_TOUCH_H);
    lv_obj_set_style_min_height(head, VG_MIN_TOUCH_H, 0);
    lv_obj_set_style_max_height(head, VG_MIN_TOUCH_H, 0);
    lv_obj_set_flex_flow(head, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(head, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    s_report.title_lab = lv_label_create(head);
    vg_style_apply_label(s_report.title_lab, false);
    lv_label_set_long_mode(s_report.title_lab, LV_LABEL_LONG_DOT);
    lv_obj_set_flex_grow(s_report.title_lab, 1);
    lv_label_set_text(s_report.title_lab, "运行报告");

    s_report.refresh_btn = lv_button_create(head);
    lv_obj_set_size(s_report.refresh_btn, 64, VG_MIN_TOUCH_H);
    vg_style_apply_btn(s_report.refresh_btn, true);
    refresh_lab = lv_label_create(s_report.refresh_btn);
    lv_label_set_text(refresh_lab, "刷新");
    lv_obj_center(refresh_lab);
    lv_obj_add_event_cb(s_report.refresh_btn, on_refresh_clicked,
                        LV_EVENT_CLICKED, NULL);

    card = lv_obj_create(parent);
    vg_style_apply_card(card);
    lv_obj_set_width(card, lv_pct(100));
    lv_obj_set_flex_grow(card, 1);
    lv_obj_set_style_min_height(card, 0, 0);
    lv_obj_add_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(card, LV_DIR_VER);
    s_report.body_card = card;

    s_report.body_lab = lv_label_create(card);
    lv_label_set_long_mode(s_report.body_lab, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_report.body_lab, lv_pct(100));
    vg_style_apply_label(s_report.body_lab, true);
    lv_obj_set_style_text_font(s_report.body_lab, vg_font_small(), 0);

    /* Submit initial read request if idle, and sample latest snapshot */
    (void)vg_ui_report_request(false, NULL);
    if(vg_ui_report_snapshot(&snap)) {
        s_report.last_version = snap.version;
        apply_snapshot(&snap);
    }
    else {
        lv_label_set_text(s_report.body_lab, REPORT_BODY_HINT);
    }

    s_report.poll_tmr = lv_timer_create(report_poll_cb, REPORT_POLL_PERIOD_MS, NULL);
}

void vg_page_report_nav_capture(vg_nav_state_t * st)
{
    if(st == NULL) return;
    st->report_version = s_report.last_version;
    if(s_report.body_card != NULL && lv_obj_is_valid(s_report.body_card)) {
        st->scroll_y = lv_obj_get_scroll_y(s_report.body_card);
    }
}

void vg_page_report_nav_restore(const vg_nav_state_t * st)
{
    if(st == NULL) return;
    if(s_report.body_card == NULL || !lv_obj_is_valid(s_report.body_card)) return;
    /* Same report version keeps scroll; new content starts at top. */
    if(st->report_version != 0 && st->report_version == s_report.last_version) {
        lv_obj_update_layout(s_report.body_card);
        lv_obj_scroll_to_y(s_report.body_card, st->scroll_y, LV_ANIM_OFF);
    }
}
