/* Alarm page: one row per active point alarm (crit > offline > warn),
 * each with a summary line plus per-row 静音 / 标记处理 buttons; tapping a
 * row binds the detail section (rule summary, metric rows, history) to it.
 * Rows rebuild only when the alarm set changes (home-list signature
 * pattern); texts refresh in place on every model tick. */
#include "vg_pages.h"
#include "widgets/vg_widgets.h"
#include "theme/vg_theme.h"
#include "model/vg_model.h"
#include "vg_display.h"
#include <stdio.h>
#include <string.h>

#define ALARM_LIST_MAX 8
#define ALARM_ROW_MUTE_W 56
#define ALARM_ROW_ACK_W 88
#define ALARM_ROW_SELECTED_BG 0x2A3138

typedef struct {
    lv_obj_t * root;
    lv_obj_t * title;
    lv_obj_t * chip;
    lv_obj_t * body;
    lv_obj_t * list;
    lv_obj_t * row_obj[ALARM_LIST_MAX];
    lv_obj_t * row_name[ALARM_LIST_MAX];
    lv_obj_t * row_chip[ALARM_LIST_MAX];
    lv_obj_t * row_sum[ALARM_LIST_MAX];
    lv_obj_t * row_mute[ALARM_LIST_MAX];
    lv_obj_t * row_ack[ALARM_LIST_MAX];
    char row_id[ALARM_LIST_MAX][VG_SENSOR_ID_MAX];
    int row_n;
    char sig[VG_SENSOR_ID_MAX * ALARM_LIST_MAX + 16];
    char sel_id[VG_SENSOR_ID_MAX];
    lv_obj_t * ai_head;
    lv_obj_t * ai_lab;
    lv_obj_t * metric_rows[6];
    lv_obj_t * hist_lab;
} alarm_ctx_t;

static alarm_ctx_t s_alarm_ui;

static void refresh_alarm(void * user);

static void fmt_f1(char * buf, size_t n, float v)
{
    int vi = (int)v;
    int vf = (int)((v - (float)vi) * 10.0f);
    if(vf < 0) vf = -vf;
    lv_snprintf(buf, n, "%d.%d", vi, vf);
}

static void set_ai_text(const char * text)
{
    if(s_alarm_ui.ai_lab == NULL) {
        return;
    }
    lv_label_set_text(s_alarm_ui.ai_lab, text);
    if(s_alarm_ui.ai_head) {
        lv_obj_clear_flag(s_alarm_ui.ai_head, LV_OBJ_FLAG_HIDDEN);
    }
    lv_obj_clear_flag(s_alarm_ui.ai_lab, LV_OBJ_FLAG_HIDDEN);
}

static void on_row_click(lv_event_t * e)
{
    int idx = (int)(intptr_t)lv_event_get_user_data(e);

    if(idx < 0 || idx >= s_alarm_ui.row_n) return;
    strncpy(s_alarm_ui.sel_id, s_alarm_ui.row_id[idx], sizeof(s_alarm_ui.sel_id) - 1);
    s_alarm_ui.sel_id[sizeof(s_alarm_ui.sel_id) - 1] = '\0';
    refresh_alarm(NULL);
    /* Bring the detail section of the tapped row into view: align the
     * rule-summary block with the viewport top (clamped by LVGL) */
    if(s_alarm_ui.ai_head && lv_obj_is_valid(s_alarm_ui.ai_head) &&
       s_alarm_ui.body && lv_obj_is_valid(s_alarm_ui.body)) {
        lv_obj_scroll_to_y(s_alarm_ui.body, lv_obj_get_y(s_alarm_ui.ai_head),
                           LV_ANIM_OFF);
    }
}

static void on_row_mute(lv_event_t * e)
{
    int idx = (int)(intptr_t)lv_event_get_user_data(e);

    if(idx < 0 || idx >= s_alarm_ui.row_n) return;
    vg_model_mute_alarm_id(s_alarm_ui.row_id[idx]);
    vg_shell_toast("已静音");
}

static void on_row_ack(lv_event_t * e)
{
    int idx = (int)(intptr_t)lv_event_get_user_data(e);

    if(idx < 0 || idx >= s_alarm_ui.row_n) return;
    vg_model_ack_alarm_id(s_alarm_ui.row_id[idx]);
    vg_shell_toast("已标记处理");
}

static lv_obj_t * make_row_btn(lv_obj_t * parent, const char * text, bool primary,
                               lv_event_cb_t cb, int w, int idx)
{
    lv_obj_t * btn = lv_button_create(parent);
    lv_obj_t * lab;

    lv_obj_set_size(btn, w, VG_MIN_TOUCH_H);
    lv_obj_set_style_min_height(btn, VG_MIN_TOUCH_H, 0);
    lv_obj_set_style_max_height(btn, VG_MIN_TOUCH_H, 0);
    lv_obj_set_style_pad_hor(btn, 4, 0);
    vg_style_apply_btn(btn, primary);
    lab = lv_label_create(btn);
    lv_label_set_text(lab, text);
    lv_obj_set_style_text_font(lab, vg_font_small(), 0);
    lv_obj_center(lab);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, (void *)(intptr_t)idx);
    return btn;
}

/* Signature of the alarm set: ids + severities (order). Values, durations
 * and ack/mute flags change every tick — refreshed in place, not in sig. */
static void build_sig(char * out, size_t cap, const vg_alarm_t * list, int n)
{
    size_t used = 0;
    int i, w;

    out[0] = '\0';
    for(i = 0; i < n && used + 1 < cap; i++) {
        w = lv_snprintf(out + used, cap - used, "%s%c,",
                        list[i].sensor_id, (char)('0' + (int)list[i].severity));
        if(w < 0) break;
        used += (size_t)w;
    }
}

static lv_obj_t * make_alarm_row(lv_obj_t * parent, int idx)
{
    lv_obj_t * row;
    lv_obj_t * line1;
    lv_obj_t * name;
    lv_obj_t * chip;
    lv_obj_t * sum;

    row = lv_obj_create(parent);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE);
    lv_obj_set_width(row, lv_pct(100));
    lv_obj_set_height(row, LV_SIZE_CONTENT);
    lv_obj_set_style_radius(row, VG_CARD_RADIUS, 0);
    /* Same visual language as home tiles: severity left accent border */
    lv_obj_set_style_border_side(row, LV_BORDER_SIDE_LEFT, 0);
    lv_obj_set_style_border_width(row, 4, 0);
    lv_obj_set_style_bg_color(row, vg_color_surface(), 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(row, lv_color_hex(ALARM_ROW_SELECTED_BG), LV_STATE_PRESSED);
    lv_obj_set_style_pad_all(row, 4, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(row, 1, 0);
    lv_obj_add_event_cb(row, on_row_click, LV_EVENT_CLICKED, (void *)(intptr_t)idx);

    line1 = lv_obj_create(row);
    lv_obj_remove_flag(line1, LV_OBJ_FLAG_SCROLLABLE);
    /* plain containers are clickable by default in LVGL 9; let taps fall
     * through to the row card so the whole row selects */
    lv_obj_clear_flag(line1, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_width(line1, lv_pct(100));
    lv_obj_set_height(line1, VG_MIN_TOUCH_H);
    lv_obj_set_style_min_height(line1, VG_MIN_TOUCH_H, 0);
    lv_obj_set_style_max_height(line1, VG_MIN_TOUCH_H, 0);
    lv_obj_set_style_bg_opa(line1, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(line1, 0, 0);
    lv_obj_set_style_pad_all(line1, 0, 0);
    lv_obj_set_flex_flow(line1, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(line1, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(line1, 6, 0);

    name = lv_label_create(line1);
    lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
    lv_obj_set_flex_grow(name, 1);
    lv_label_set_text(name, "--");
    lv_obj_set_style_text_font(name, vg_font_ui(), 0);

    chip = vg_status_chip_create(line1, "--", VG_SEV_OK);
    lv_obj_clear_flag(chip, LV_OBJ_FLAG_CLICKABLE);

    make_row_btn(line1, "静音", false, on_row_mute, ALARM_ROW_MUTE_W, idx);
    make_row_btn(line1, "标记处理", true, on_row_ack, ALARM_ROW_ACK_W, idx);

    sum = lv_label_create(row);
    lv_label_set_long_mode(sum, LV_LABEL_LONG_DOT);
    lv_obj_set_width(sum, lv_pct(100));
    lv_label_set_text(sum, "--");
    lv_obj_set_style_text_font(sum, vg_font_small(), 0);
    lv_obj_set_style_text_color(sum, vg_color_muted(), 0);

    s_alarm_ui.row_obj[idx] = row;
    s_alarm_ui.row_name[idx] = name;
    s_alarm_ui.row_chip[idx] = chip;
    s_alarm_ui.row_sum[idx] = sum;
    return row;
}

static void rebuild_rows(const vg_alarm_t * list, int n)
{
    int i;

    lv_obj_clean(s_alarm_ui.list);
    s_alarm_ui.row_n = 0;
    for(i = 0; i < n; i++) {
        make_alarm_row(s_alarm_ui.list, i);
        strncpy(s_alarm_ui.row_id[i], list[i].sensor_id, sizeof(s_alarm_ui.row_id[i]) - 1);
        s_alarm_ui.row_id[i][sizeof(s_alarm_ui.row_id[i]) - 1] = '\0';
        s_alarm_ui.row_n = i + 1;
    }
}

static void set_row_texts(int idx, const vg_alarm_t * a)
{
    const vg_sensor_t * s = vg_model_get_sensor(a->sensor_id);
    const bool selected = (strcmp(s_alarm_ui.sel_id, a->sensor_id) == 0);
    char buf[96];
    char v1[16];

    if(s_alarm_ui.row_name[idx]) {
        lv_label_set_text(s_alarm_ui.row_name[idx], s ? s->name : a->sensor_id);
        /* Selection cue beyond the subtle bg: accent-colored point name */
        lv_obj_set_style_text_color(s_alarm_ui.row_name[idx],
                                    selected ? vg_color_accent()
                                             : (s && s->online ? vg_color_text()
                                                               : vg_color_muted()), 0);
    }
    if(s_alarm_ui.row_chip[idx]) {
        vg_status_chip_set(s_alarm_ui.row_chip[idx],
                           vg_severity_label_zh(a->severity), a->severity);
    }
    if(s_alarm_ui.row_obj[idx]) {
        lv_obj_set_style_border_color(s_alarm_ui.row_obj[idx],
                                      vg_color_severity(a->severity), 0);
        lv_obj_set_style_bg_color(s_alarm_ui.row_obj[idx],
                                  selected ? lv_color_hex(ALARM_ROW_SELECTED_BG)
                                           : vg_color_surface(), 0);
    }
    if(s_alarm_ui.row_sum[idx]) {
        if(a->severity == VG_SEV_OFFLINE || s == NULL) {
            lv_snprintf(buf, sizeof(buf), "通信离线 · 持续 %ds", a->duration_sec);
        }
        else {
            fmt_f1(v1, sizeof(v1), a->value);
            {
                char v2[16];
                size_t len;
                fmt_f1(v2, sizeof(v2), a->threshold);
                len = (size_t)lv_snprintf(buf, sizeof(buf), "当前 %s%s · 阈值 %s%s · 持续 %ds",
                                          v1, s->unit, v2, s->unit, a->duration_sec);
                if(a->acked && len < sizeof(buf)) {
                    lv_snprintf(buf + len, sizeof(buf) - len, " · 已处理");
                }
                else if(a->muted && len < sizeof(buf)) {
                    lv_snprintf(buf + len, sizeof(buf) - len, " · 已静音");
                }
            }
        }
        lv_label_set_text(s_alarm_ui.row_sum[idx], buf);
    }
    if(s_alarm_ui.row_ack[idx]) {
        if(a->acked) lv_obj_add_state(s_alarm_ui.row_ack[idx], LV_STATE_DISABLED);
        else lv_obj_remove_state(s_alarm_ui.row_ack[idx], LV_STATE_DISABLED);
    }
    if(s_alarm_ui.row_mute[idx]) {
        if(a->muted) lv_obj_add_state(s_alarm_ui.row_mute[idx], LV_STATE_DISABLED);
        else lv_obj_remove_state(s_alarm_ui.row_mute[idx], LV_STATE_DISABLED);
    }
}

/* Detail section bound to the selected alarm (NULL = empty state) */
static void render_detail(const vg_alarm_t * a)
{
    const vg_sensor_t * as = NULL;
    char buf[160];
    char a1[16];
    int i;
    int start;
    int npts;

    if(a == NULL) {
        for(i = 0; i < 6; i++) {
            vg_metric_row_set_value(s_alarm_ui.metric_rows[i], "--");
        }
        vg_metric_row_set_value(s_alarm_ui.metric_rows[4], "--");
        lv_label_set_text(s_alarm_ui.hist_lab, "历史: --");
        set_ai_text("【规则摘要】当前无活动告警。");
        return;
    }

    if(a->sensor_id[0] != '\0') {
        as = vg_model_get_sensor(a->sensor_id);
    }

    vg_metric_row_set_value(s_alarm_ui.metric_rows[0],
                            a->severity == VG_SEV_OFFLINE ? "通信离线" : "阈值越限");
    if(a->severity == VG_SEV_OFFLINE || as == NULL) {
        vg_metric_row_set_value(s_alarm_ui.metric_rows[1], "--");
        vg_metric_row_set_value(s_alarm_ui.metric_rows[2], "--");
    }
    else {
        fmt_f1(a1, sizeof(a1), a->value);
        lv_snprintf(buf, sizeof(buf), "%s %s", a1, as->unit);
        vg_metric_row_set_value(s_alarm_ui.metric_rows[1], buf);

        fmt_f1(a1, sizeof(a1), a->threshold);
        lv_snprintf(buf, sizeof(buf), "%s %s", a1, as->unit);
        vg_metric_row_set_value(s_alarm_ui.metric_rows[2], buf);
    }

    lv_snprintf(buf, sizeof(buf), "%d s", a->duration_sec);
    vg_metric_row_set_value(s_alarm_ui.metric_rows[3], buf);
    vg_metric_row_set_value(s_alarm_ui.metric_rows[4], as ? as->name : "--");
    vg_metric_row_set_value(s_alarm_ui.metric_rows[5],
                            a->acked ? "已处理" : (a->muted ? "已静音" : "活动中"));

    npts = 0;
    buf[0] = '\0';
    if(as != NULL) {
        npts = 8;
        if(as->history_len < npts) npts = as->history_len;
        start = (int)as->history_len - npts;
        if(start < 0) start = 0;
        for(i = 0; i < npts; i++) {
            char p[12];
            fmt_f1(p, sizeof(p), as->history[start + i]);
            if(i == 0) {
                lv_snprintf(buf, sizeof(buf), "%s", p);
            }
            else {
                size_t len = strlen(buf);
                lv_snprintf(buf + len, sizeof(buf) - len, " %s", p);
            }
        }
    }
    if(npts == 0) {
        lv_label_set_text(s_alarm_ui.hist_lab, "历史: --");
    }
    else {
        char full[128];
        lv_snprintf(full, sizeof(full), "历史: %s", buf);
        lv_label_set_text(s_alarm_ui.hist_lab, full);
    }

    if(as != NULL && a->severity != VG_SEV_OFFLINE) {
        lv_snprintf(buf, sizeof(buf),
                    "【规则摘要】%s 当前 %.1f%s，阈值 %.1f%s，已持续 %d 秒。"
                    "判定来自点表 cmp/阈值，本地规则引擎。",
                    as->name, (double)a->value, as->unit,
                    (double)a->threshold, as->unit, a->duration_sec);
    }
    else if(a->severity == VG_SEV_OFFLINE) {
        lv_snprintf(buf, sizeof(buf),
                    "【规则摘要】滑窗读失败达阈值，离线已持续 %d 秒。",
                    a->duration_sec);
    }
    else {
        lv_snprintf(buf, sizeof(buf),
                    "【规则摘要】有活动告警，但本地尚无该点读数。");
    }
    set_ai_text(buf);
}

static void refresh_alarm(void * user)
{
    const vg_alarm_t * sel = NULL;
    vg_alarm_t list[ALARM_LIST_MAX];
    char newsig[sizeof(s_alarm_ui.sig)];
    char buf[32];
    uint16_t total;
    int n;
    int i;
    LV_UNUSED(user);
    if(vg_nav_current() != VG_PAGE_ALARM) return;
    if(s_alarm_ui.root == NULL || !lv_obj_is_valid(s_alarm_ui.root)) return;

    total = vg_model_active_alarm_count();
    n = vg_model_collect_alarms(list, ALARM_LIST_MAX);

    /* Head: real total count (list caps at ALARM_LIST_MAX) + top severity */
    if(total > 0 && n > 0) {
        lv_snprintf(buf, sizeof(buf), "活动告警 %u 个", (unsigned)total);
        lv_label_set_text(s_alarm_ui.title, buf);
        vg_status_chip_set(s_alarm_ui.chip,
                           vg_severity_label_zh(list[0].severity), list[0].severity);
    }
    else {
        lv_label_set_text(s_alarm_ui.title, "无活动告警");
        vg_status_chip_set(s_alarm_ui.chip, "正常", VG_SEV_OK);
    }

    if(total == 0) s_alarm_ui.sel_id[0] = '\0';

    /* Rebuild rows only when the alarm set changed; refresh texts always */
    if(s_alarm_ui.list == NULL || !lv_obj_is_valid(s_alarm_ui.list)) return;
    build_sig(newsig, sizeof(newsig), list, n);
    if(strcmp(newsig, s_alarm_ui.sig) != 0) {
        strncpy(s_alarm_ui.sig, newsig, sizeof(s_alarm_ui.sig) - 1);
        s_alarm_ui.sig[sizeof(s_alarm_ui.sig) - 1] = '\0';
        rebuild_rows(list, n);
    }

    for(i = 0; i < n && i < s_alarm_ui.row_n; i++) {
        set_row_texts(i, &list[i]);
        if(sel == NULL && s_alarm_ui.sel_id[0] != '\0' &&
           strcmp(s_alarm_ui.sel_id, list[i].sensor_id) == 0) {
            sel = &list[i];
        }
    }
    if(sel == NULL && n > 0) {
        strncpy(s_alarm_ui.sel_id, list[0].sensor_id, sizeof(s_alarm_ui.sel_id) - 1);
        s_alarm_ui.sel_id[sizeof(s_alarm_ui.sel_id) - 1] = '\0';
        sel = &list[0];
        if(s_alarm_ui.row_obj[0] && lv_obj_is_valid(s_alarm_ui.row_obj[0])) {
            set_row_texts(0, &list[0]);
        }
    }

    render_detail(sel);
}

static void on_alarm_delete(lv_event_t * e)
{
    LV_UNUSED(e);
    vg_model_off_change(refresh_alarm, NULL);
    memset(&s_alarm_ui, 0, sizeof(s_alarm_ui));
}

void vg_page_alarm_create(lv_obj_t * parent, const void * args)
{
    lv_obj_t * head;
    lv_obj_t * left;
    lv_obj_t * body;
    LV_UNUSED(args);
    memset(&s_alarm_ui, 0, sizeof(s_alarm_ui));
    s_alarm_ui.root = parent;
    lv_obj_add_event_cb(parent, on_alarm_delete, LV_EVENT_DELETE, NULL);

    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(parent, 2, 0);
    lv_obj_remove_flag(parent, LV_OBJ_FLAG_SCROLLABLE);

    head = lv_obj_create(parent);
    vg_style_apply_card(head);
    lv_obj_set_width(head, lv_pct(100));
    lv_obj_set_height(head, 36);
    lv_obj_set_style_min_height(head, 36, 0);
    lv_obj_set_style_max_height(head, 36, 0);
    lv_obj_set_style_pad_all(head, 4, 0);
    lv_obj_set_flex_flow(head, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(head, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    left = lv_obj_create(head);
    lv_obj_remove_flag(left, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(left, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(left, 0, 0);
    lv_obj_set_style_pad_all(left, 0, 0);
    lv_obj_set_size(left, LV_SIZE_CONTENT, LV_SIZE_CONTENT);

    s_alarm_ui.title = lv_label_create(left);
    vg_style_apply_label(s_alarm_ui.title, false);
    lv_label_set_text(s_alarm_ui.title, "告警");

    s_alarm_ui.chip = vg_status_chip_create(head, "正常", VG_SEV_OK);

    body = lv_obj_create(parent);
    vg_style_apply_card(body);
    lv_obj_set_width(body, lv_pct(100));
    lv_obj_set_flex_grow(body, 1);
    lv_obj_set_style_min_height(body, 0, 0);
    lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(body, 2, 0);
    lv_obj_set_style_pad_ver(body, 2, 0);
    lv_obj_set_style_pad_hor(body, 6, 0);
    lv_obj_add_flag(body, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(body, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(body, LV_SCROLLBAR_MODE_AUTO);
    s_alarm_ui.body = body;

    /* Rows host: bare container inside the scrollable card so detail
     * widgets below survive row rebuilds. */
    s_alarm_ui.list = lv_obj_create(body);
    lv_obj_remove_flag(s_alarm_ui.list, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_width(s_alarm_ui.list, lv_pct(100));
    lv_obj_set_height(s_alarm_ui.list, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(s_alarm_ui.list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_alarm_ui.list, 0, 0);
    lv_obj_set_style_radius(s_alarm_ui.list, 0, 0);
    lv_obj_set_style_pad_all(s_alarm_ui.list, 0, 0);
    lv_obj_set_style_pad_row(s_alarm_ui.list, 2, 0);
    lv_obj_set_flex_flow(s_alarm_ui.list, LV_FLEX_FLOW_COLUMN);

    /* AI block first: 480x272 otherwise hides it under metric rows. */
    s_alarm_ui.ai_head = lv_label_create(body);
    vg_style_apply_label(s_alarm_ui.ai_head, false);
    lv_obj_set_style_text_color(s_alarm_ui.ai_head, vg_color_info(), 0);
    lv_label_set_text(s_alarm_ui.ai_head, "规则摘要");

    s_alarm_ui.ai_lab = lv_label_create(body);
    lv_label_set_long_mode(s_alarm_ui.ai_lab, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_alarm_ui.ai_lab, lv_pct(100));
    vg_style_apply_label(s_alarm_ui.ai_lab, true);
    lv_obj_set_style_text_font(s_alarm_ui.ai_lab, vg_font_small(), 0);
    lv_label_set_text(s_alarm_ui.ai_lab,
                      "【规则摘要】当前无活动告警。");

    s_alarm_ui.metric_rows[0] = vg_metric_row_create(body, "类型", "--");
    s_alarm_ui.metric_rows[1] = vg_metric_row_create(body, "当前值", "--");
    s_alarm_ui.metric_rows[2] = vg_metric_row_create(body, "阈值", "--");
    s_alarm_ui.metric_rows[3] = vg_metric_row_create(body, "持续", "--");
    s_alarm_ui.metric_rows[4] = vg_metric_row_create(body, "传感器", "--");
    s_alarm_ui.metric_rows[5] = vg_metric_row_create(body, "状态", "--");

    s_alarm_ui.hist_lab = lv_label_create(body);
    lv_label_set_long_mode(s_alarm_ui.hist_lab, LV_LABEL_LONG_DOT);
    lv_obj_set_width(s_alarm_ui.hist_lab, lv_pct(100));
    vg_style_apply_label(s_alarm_ui.hist_lab, true);
    lv_obj_set_style_text_font(s_alarm_ui.hist_lab, vg_font_small(), 0);
    lv_label_set_text(s_alarm_ui.hist_lab, "历史: --");

    vg_model_on_change(refresh_alarm, NULL);
    refresh_alarm(NULL);
}
