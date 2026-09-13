#include "vg_pages.h"
#include "widgets/vg_widgets.h"
#include "theme/vg_theme.h"
#include "model/vg_model.h"
#include "model/vg_ui_backend.h"
#include "vg_display.h"
#include <string.h>

#define REPORT_POLL_PERIOD_MS 2000
#define REPORT_GEN_TIMEOUT_S  180
#define REPORT_BODY_HINT \
    "暂无日报。\n\n(板端: /data/velaguard/reports/daily-*.md)\n点右上角刷新可请求 MiMo 生成。"

typedef struct {
    lv_obj_t * root;
    lv_obj_t * title_lab;
    lv_obj_t * body_lab;
    lv_obj_t * refresh_btn;
    lv_timer_t * poll_tmr;
    uint32_t poll_count;
} report_ctx_t;

static report_ctx_t s_report;

/* True only when a real daily report body was read; the board backend fills
 * hint text and returns -ENOENT when the reports dir has no daily-*.md. */
static bool report_read(char * body, size_t body_sz, char * path, size_t path_sz)
{
    const vg_ui_backend_t * be = vg_ui_backend_get();

    body[0] = '\0';
    path[0] = '\0';
    if(be == NULL || be->read_latest_report == NULL) {
        return false;
    }
    if(be->read_latest_report(body, body_sz, path, path_sz) != 0) {
        return false;
    }
    return body[0] != '\0';
}

/* Title keeps the filename only: the full path would truncate its own date */
static void report_render(const char * path, const char * body)
{
    const char * base;

    if(s_report.title_lab == NULL || s_report.body_lab == NULL) {
        return;
    }

    base = (path != NULL) ? strrchr(path, '/') : NULL;
    base = (base != NULL) ? base + 1 : path;
    if(base != NULL && base[0] != '\0') {
        lv_label_set_text_fmt(s_report.title_lab, "最新日报 · %s", base);
    }
    else {
        lv_label_set_text(s_report.title_lab, "运行报告");
    }
    lv_label_set_text(s_report.body_lab, body);
}

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

static void report_poll_stop(void)
{
    if(s_report.poll_tmr != NULL) {
        lv_timer_del(s_report.poll_tmr);
        s_report.poll_tmr = NULL;
    }
}

static void gen_poll_cb(lv_timer_t * t)
{
    char body[1024];
    char path[128];
    uint32_t elapsed;

    LV_UNUSED(t);
    if(s_report.poll_tmr == NULL) {
        return;
    }

    s_report.poll_count++;
    elapsed = s_report.poll_count * (REPORT_POLL_PERIOD_MS / 1000);

    if(report_read(body, sizeof(body), path, sizeof(path))) {
        report_poll_stop();
        report_set_refresh_enabled(true);
        report_render(path, body);
        return;
    }

    if(elapsed >= REPORT_GEN_TIMEOUT_S) {
        report_poll_stop();
        report_set_refresh_enabled(true);
        report_render(NULL,
            "日报生成超时。\n\n请检查 MiMo 服务与网络(状态栏 MiMo),稍后点刷新重试。");
        return;
    }

    lv_snprintf(body, sizeof(body),
                "日报生成中(MiMo)…\n\n已等待 %us / %us,完成后自动显示。\n"
                "也可先返回其他页面,生成后回到本页点刷新。",
                (unsigned)elapsed, (unsigned)REPORT_GEN_TIMEOUT_S);
    lv_label_set_text(s_report.body_lab, body);
}

static void report_reload(void)
{
    char body[1024];
    char path[128];

    if(report_read(body, sizeof(body), path, sizeof(path))) {
        report_render(path, body);
    }
    else {
        report_render(NULL, REPORT_BODY_HINT);
    }
}

static void report_refresh(void)
{
    const vg_ui_backend_t * be = vg_ui_backend_get();
    char body[1024];
    char path[128];

    if(s_report.poll_tmr != NULL) {
        return; /* generation already running */
    }

    if(report_read(body, sizeof(body), path, sizeof(path))) {
        report_render(path, body);
        return;
    }

    if(be == NULL || be->request_daily_report == NULL ||
       !be->request_daily_report()) {
        report_render(NULL, REPORT_BODY_HINT);
        return;
    }

    lv_snprintf(body, sizeof(body),
                "日报生成中(MiMo)…\n\n已通知后台 Agent 按运营日报 Skill 采集数据,\n"
                "请保持设备联网,完成后自动显示。");
    report_render(NULL, body);
    report_set_refresh_enabled(false);
    s_report.poll_count = 0;
    s_report.poll_tmr = lv_timer_create(gen_poll_cb, REPORT_POLL_PERIOD_MS, NULL);
}

static void on_refresh_clicked(lv_event_t * e)
{
    LV_UNUSED(e);
    report_refresh();
}

static void on_report_delete(lv_event_t * e)
{
    LV_UNUSED(e);
    report_poll_stop();
    memset(&s_report, 0, sizeof(s_report));
}

void vg_page_report_create(lv_obj_t * parent, const void * args)
{
    lv_obj_t * head;
    lv_obj_t * card;
    lv_obj_t * refresh_lab;
    LV_UNUSED(args);

    memset(&s_report, 0, sizeof(s_report));
    s_report.root = parent;
    lv_obj_add_event_cb(parent, on_report_delete, LV_EVENT_DELETE, NULL);

    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(parent, 6, 0);
    lv_obj_remove_flag(parent, LV_OBJ_FLAG_SCROLLABLE);

    /* Head row: title left, manual refresh right (trend-page pattern) */
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

    s_report.body_lab = lv_label_create(card);
    lv_label_set_long_mode(s_report.body_lab, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_report.body_lab, lv_pct(100));
    vg_style_apply_label(s_report.body_lab, true);
    lv_obj_set_style_text_font(s_report.body_lab, vg_font_small(), 0);

    report_reload();
}
