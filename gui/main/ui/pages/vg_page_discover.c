#include "vg_pages.h"
#include "shell/vg_shell.h"
#include "widgets/vg_widgets.h"
#include "widgets/vg_confirm_dialog.h"
#include "theme/vg_theme.h"
#include "model/vg_ui_backend.h"
#include "model/vg_model.h"
#include "vg_display.h"
#include <stdio.h>
#include <string.h>

typedef struct {
    lv_obj_t * root;
    lv_obj_t * scan_sw;
    lv_obj_t * scan_btn;
    lv_obj_t * confirm_btn;
    lv_obj_t * hint_lab;
    lv_obj_t * list;
    lv_timer_t * poll_tmr;
    int last_hit_n;
} discover_ctx_t;

static discover_ctx_t s_disc;

static void refresh_scan_controls(void);
static void refresh_scan_hint(int st);
static void refresh_confirm_btn(void);

static void fill_slave_results(bool import_model)
{
    const vg_ui_backend_t * be = vg_ui_backend_get();
    vg_ui_slave_t slaves[32];
    int n;
    int i;
    char buf[48];

    if(s_disc.list == NULL || be == NULL || be->get_slaves == NULL) {
        return;
    }

    lv_obj_clean(s_disc.list);
    n = be->get_slaves(slaves, 32);
    s_disc.last_hit_n = n;
    if(n <= 0) {
        lv_obj_t * row = lv_label_create(s_disc.list);
        lv_label_set_text(row, "未发现从站");
        vg_style_apply_label(row, true);
        refresh_confirm_btn();
        return;
    }

    for(i = 0; i < n; i++) {
        lv_obj_t * row = lv_label_create(s_disc.list);
        lv_snprintf(buf, sizeof(buf), "%s  FC03@reg%u OK",
                    slaves[i].label[0] ? slaves[i].label : "addr=?",
                    (unsigned)slaves[i].probe_reg);
        lv_label_set_text(row, buf);
        vg_style_apply_label(row, true);
        lv_obj_set_style_text_font(row, vg_font_small(), 0);
        lv_obj_set_width(row, lv_pct(100));
    }

    /* Never mutate fleet / points until explicit confirm (manual §5.1). */
    if(import_model) {
        vg_model_import_discover_slaves(slaves, n);
    }

    refresh_confirm_btn();
}

static void refresh_confirm_btn(void)
{
    bool show = false;

    if(s_disc.confirm_btn == NULL) {
        return;
    }

    if(s_disc.scan_sw && lv_obj_has_state(s_disc.scan_sw, LV_STATE_CHECKED) &&
       s_disc.last_hit_n > 0) {
        show = true;
    }

    if(show) {
        lv_obj_remove_flag(s_disc.confirm_btn, LV_OBJ_FLAG_HIDDEN);
    }
    else {
        lv_obj_add_flag(s_disc.confirm_btn, LV_OBJ_FLAG_HIDDEN);
    }
}

static void apply_poll_cb(lv_timer_t * t)
{
    const vg_ui_backend_t * be = vg_ui_backend_get();
    int st;

    LV_UNUSED(t);
    if(be == NULL || be->discover_apply_status == NULL) {
        return;
    }

    st = be->discover_apply_status();
    if(st == 1) {
        return;
    }

    if(s_disc.poll_tmr) {
        lv_timer_delete(s_disc.poll_tmr);
        s_disc.poll_tmr = NULL;
    }

    if(st == 2) {
        bool partial = vg_ui_backend_apply_last_result() > 0;
        vg_shell_toast(partial ? "候选仅含部分结果，请核对" : "候选点表已生成");
        if(s_disc.hint_lab) {
            lv_label_set_text(s_disc.hint_lab,
                "NSH: vgpoint test · 核对后单独 apply --confirm");
        }
    }
    else {
        char errbuf[40];
        int rc = vg_ui_backend_apply_last_result();
        lv_snprintf(errbuf, sizeof(errbuf), "候选生成失败 (%d)", rc);
        vg_shell_toast(errbuf);
    }
}

static void scan_poll_cb(lv_timer_t * t)
{
    const vg_ui_backend_t * be = vg_ui_backend_get();
    int st;

    LV_UNUSED(t);
    if(be == NULL || be->discover_scan_status == NULL) {
        return;
    }

    st = be->discover_scan_status();
    if(st == 1) {
        fill_slave_results(false);
        refresh_scan_hint(st);
        return;
    }

    if(s_disc.poll_tmr) {
        lv_timer_delete(s_disc.poll_tmr);
        s_disc.poll_tmr = NULL;
    }

    if(st == 2) {
        int n = 0;
        const vg_ui_backend_t * be2 = vg_ui_backend_get();
        vg_ui_slave_t slaves[32];

        /* Preview list only — do not import fleet or write points yet. */
        fill_slave_results(false);
        if(be2 != NULL && be2->get_slaves != NULL) {
            n = be2->get_slaves(slaves, 32);
        }
        refresh_scan_hint(0);
        if(n > 0) {
            char done[48];
            lv_snprintf(done, sizeof(done), "扫描完成 %d/32 · 可生成候选", n);
            vg_shell_toast(done);
            if(s_disc.hint_lab) {
                lv_label_set_text(s_disc.hint_lab,
                                  "仅预览 · 生成候选后通过 NSH 试读、人工确认");
            }
        }
        else {
            vg_shell_toast("未发现从站");
        }
    }
    else {
        char errbuf[40];
        int rc = vg_ui_backend_scan_last_result();
        lv_snprintf(errbuf, sizeof(errbuf), "扫描失败 (%d)", rc);
        vg_shell_toast(errbuf);
        refresh_confirm_btn();
    }
}

static void refresh_scan_hint(int st)
{
    char hint[64];
    int cur = 0;
    int max = 32;

    if(s_disc.hint_lab == NULL) {
        return;
    }

    if(st == 1) {
        vg_ui_backend_scan_progress(&cur, &max);
        if(cur > 0 && max > 0) {
            lv_snprintf(hint, sizeof(hint),
                        "扫描中 %d/%d · 9600 · FC03@reg0/1/2", cur, max);
        }
        else {
            lv_snprintf(hint, sizeof(hint),
                        "扫描中… · 9600 · FC03@reg0/1/2 (约 1–2 分钟)");
        }
        lv_label_set_text(s_disc.hint_lab, hint);
        return;
    }

    refresh_scan_controls();
}

static void refresh_scan_controls(void)
{
    bool on = s_disc.scan_sw && lv_obj_has_state(s_disc.scan_sw, LV_STATE_CHECKED);

    if(s_disc.scan_btn) {
        if(on) lv_obj_remove_flag(s_disc.scan_btn, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(s_disc.scan_btn, LV_OBJ_FLAG_HIDDEN);
    }
    if(s_disc.hint_lab) {
        lv_label_set_text(s_disc.hint_lab,
                           on ? "9600 · FC03@reg0/2/1 · MThings请停采集"
                              : "扫描默认关闭 - 打开开关后再扫描 RS485");
    }
    refresh_confirm_btn();
}

static void on_sw(lv_event_t * e)
{
    LV_UNUSED(e);
    s_disc.last_hit_n = 0;
    refresh_scan_controls();
    if(s_disc.list) lv_obj_clean(s_disc.list);
}

static void on_scan(lv_event_t * e)
{
    const vg_ui_backend_t * be = vg_ui_backend_get();
    int rc;

    LV_UNUSED(e);
    if(s_disc.scan_sw == NULL ||
       !lv_obj_has_state(s_disc.scan_sw, LV_STATE_CHECKED)) {
        vg_shell_toast("请先启用总线扫描");
        return;
    }

    if(be == NULL || be->discover_scan_start == NULL) {
        vg_shell_toast("discover 未启用");
        return;
    }

    if(be->discover_scan_status != NULL && be->discover_scan_status() == 1) {
        vg_shell_toast("扫描进行中…");
        return;
    }

    if(be->discover_apply_status != NULL && be->discover_apply_status() == 1) {
        vg_shell_toast("候选生成中…");
        return;
    }

    if(s_disc.list) lv_obj_clean(s_disc.list);
    s_disc.last_hit_n = 0;
    refresh_confirm_btn();
    rc = be->discover_scan_start(1, 32);
    if(rc != 0) {
        char errbuf[40];
        lv_snprintf(errbuf, sizeof(errbuf), "无法启动 (%d)", rc);
        vg_shell_toast(errbuf);
        return;
    }

    vg_shell_toast("扫描中… @9600 (1-32)");
    refresh_scan_hint(1);
    if(s_disc.poll_tmr == NULL) {
        s_disc.poll_tmr = lv_timer_create(scan_poll_cb, 200, NULL);
    }
}

static void on_confirm_ok(void * user)
{
    const vg_ui_backend_t * be = vg_ui_backend_get();
    int rc;

    LV_UNUSED(user);
    if(be == NULL || be->discover_apply_start == NULL) {
        vg_shell_toast("confirm 未启用");
        return;
    }

    rc = be->discover_apply_start();
    if(rc != 0) {
        char errbuf[40];
        lv_snprintf(errbuf, sizeof(errbuf), "无法生成候选 (%d)", rc);
        vg_shell_toast(errbuf);
        return;
    }

    vg_shell_toast("探测并生成候选中…");
    if(s_disc.hint_lab) {
        lv_label_set_text(s_disc.hint_lab, "探测中 · 完成后请在 NSH 试读候选");
    }
    if(s_disc.poll_tmr == NULL) {
        s_disc.poll_tmr = lv_timer_create(apply_poll_cb, 200, NULL);
    }
}

static void on_confirm(lv_event_t * e)
{
    char body[160];

    LV_UNUSED(e);
    if(s_disc.last_hit_n <= 0) {
        vg_shell_toast("请先扫描从站");
        return;
    }

    lv_snprintf(body, sizeof(body),
                "将探测 %d 个从站并生成候选点表。\n"
                "随后在 NSH 试读并人工确认生效。",
                s_disc.last_hit_n);
    vg_confirm_dialog_create("生成候选点表", body, "high", "生成候选",
                             on_confirm_ok, NULL);
}

static void on_discover_delete(lv_event_t * e)
{
    LV_UNUSED(e);
    if(s_disc.poll_tmr) {
        lv_timer_delete(s_disc.poll_tmr);
        s_disc.poll_tmr = NULL;
    }
    memset(&s_disc, 0, sizeof(s_disc));
}

void vg_page_discover_create(lv_obj_t * parent, const void * args)
{
    lv_obj_t * row;
    lv_obj_t * sw_lab;
    LV_UNUSED(args);
    memset(&s_disc, 0, sizeof(s_disc));
    s_disc.root = parent;
    lv_obj_add_event_cb(parent, on_discover_delete, LV_EVENT_DELETE, NULL);

    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(parent, 4, 0);
    lv_obj_remove_flag(parent, LV_OBJ_FLAG_SCROLLABLE);

    row = lv_obj_create(parent);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_width(row, lv_pct(100));
    lv_obj_set_height(row, VG_MIN_TOUCH_H);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);

    sw_lab = lv_label_create(row);
    lv_label_set_text(sw_lab, "启用总线扫描");
    vg_style_apply_label(sw_lab, false);

    s_disc.scan_sw = lv_switch_create(row);
    lv_obj_add_event_cb(s_disc.scan_sw, on_sw, LV_EVENT_VALUE_CHANGED, NULL);
    /* Manual §5.1 / §6.6: default OFF — do not scan RS485 on page entry. */

    s_disc.hint_lab = lv_label_create(parent);
    lv_obj_set_width(s_disc.hint_lab, lv_pct(100));
    vg_style_apply_label(s_disc.hint_lab, true);
    lv_obj_set_style_text_font(s_disc.hint_lab, vg_font_small(), 0);

    s_disc.scan_btn = lv_button_create(parent);
    lv_obj_set_size(s_disc.scan_btn, lv_pct(100), VG_MIN_TOUCH_H);
    vg_style_apply_btn(s_disc.scan_btn, true);
    lv_obj_add_flag(s_disc.scan_btn, LV_OBJ_FLAG_HIDDEN);
    lv_obj_t * scan_lab = lv_label_create(s_disc.scan_btn);
    lv_label_set_text(scan_lab, "开始扫描 1-32");
    lv_obj_set_style_text_font(scan_lab, vg_font_ui(), 0);
    lv_obj_center(scan_lab);
    lv_obj_add_event_cb(s_disc.scan_btn, on_scan, LV_EVENT_CLICKED, NULL);

    s_disc.confirm_btn = lv_button_create(parent);
    lv_obj_set_size(s_disc.confirm_btn, lv_pct(100), VG_MIN_TOUCH_H);
    vg_style_apply_btn(s_disc.confirm_btn, false);
    lv_obj_add_flag(s_disc.confirm_btn, LV_OBJ_FLAG_HIDDEN);
    lv_obj_t * conf_lab = lv_label_create(s_disc.confirm_btn);
    lv_label_set_text(conf_lab, "探测并生成候选");
    lv_obj_set_style_text_font(conf_lab, vg_font_ui(), 0);
    lv_obj_center(conf_lab);
    lv_obj_add_event_cb(s_disc.confirm_btn, on_confirm, LV_EVENT_CLICKED, NULL);

    s_disc.list = lv_obj_create(parent);
    vg_style_apply_card(s_disc.list);
    lv_obj_set_width(s_disc.list, lv_pct(100));
    lv_obj_set_flex_grow(s_disc.list, 1);
    lv_obj_set_style_min_height(s_disc.list, 0, 0);
    lv_obj_set_flex_flow(s_disc.list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(s_disc.list, 2, 0);
    lv_obj_add_flag(s_disc.list, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(s_disc.list, LV_DIR_VER);

    refresh_scan_controls();
}
