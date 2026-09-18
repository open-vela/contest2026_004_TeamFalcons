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
    lv_obj_t * row_ai[ALARM_LIST_MAX];
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
    bool scrolling;
    bool struct_pending;
    char pending_sig[VG_SENSOR_ID_MAX * ALARM_LIST_MAX + 16];
    char anchor_id[VG_SENSOR_ID_MAX];
    int32_t anchor_y;
} alarm_ctx_t;

static alarm_ctx_t s_alarm_ui;

typedef struct {
    char id[VG_SENSOR_ID_MAX];
    uint32_t alarm_epoch;
    uint32_t structure_version;
    uint32_t page_gen;
    bool valid;
} alarm_press_t;

static alarm_press_t s_alarm_press;

static void refresh_alarm(void * user);

static void alarm_press_bind(int idx)
{
    const vg_sensor_t * s;

    memset(&s_alarm_press, 0, sizeof(s_alarm_press));
    if(idx < 0 || idx >= s_alarm_ui.row_n) return;
    strncpy(s_alarm_press.id, s_alarm_ui.row_id[idx], sizeof(s_alarm_press.id) - 1);
    s = vg_model_get_sensor(s_alarm_press.id);
    if(s != NULL) s_alarm_press.alarm_epoch = s->al_epoch;
    s_alarm_press.structure_version = vg_model_structure_version();
    s_alarm_press.page_gen = vg_shell_page_generation();
    s_alarm_press.valid = true;
}

static bool alarm_press_ok(int idx)
{
    const vg_sensor_t * s;

    if(!s_alarm_press.valid) return false;
    if(idx < 0 || idx >= s_alarm_ui.row_n) return false;
    if(s_alarm_press.page_gen != vg_shell_page_generation()) return false;
    if(s_alarm_press.structure_version != vg_model_structure_version()) return false;
    if(strcmp(s_alarm_press.id, s_alarm_ui.row_id[idx]) != 0) return false;
    s = vg_model_get_sensor(s_alarm_press.id);
    if(s == NULL || !s->al_active) return false;
    if(s->al_epoch != s_alarm_press.alarm_epoch) return false;
    return true;
}

static bool alarm_gesture_active(void)
{
    return s_alarm_ui.scrolling || s_alarm_press.valid;
}

static void alarm_capture_anchor(void)
{
    int i;
    int32_t sy;

    s_alarm_ui.anchor_id[0] = '\0';
    s_alarm_ui.anchor_y = 0;
    if(s_alarm_ui.body == NULL || !lv_obj_is_valid(s_alarm_ui.body)) return;
    sy = lv_obj_get_scroll_y(s_alarm_ui.body);
    s_alarm_ui.anchor_y = sy;
    for(i = 0; i < s_alarm_ui.row_n; i++) {
        lv_obj_t * row = s_alarm_ui.row_obj[i];
        int32_t y;

        if(row == NULL || !lv_obj_is_valid(row)) continue;
        y = lv_obj_get_y(row) - sy;
        if(y + VG_MIN_TOUCH_H > 0) {
            strncpy(s_alarm_ui.anchor_id, s_alarm_ui.row_id[i],
                    sizeof(s_alarm_ui.anchor_id) - 1);
            s_alarm_ui.anchor_y = y;
            return;
        }
    }
}

static void alarm_restore_anchor(void)
{
    int i;

    if(s_alarm_ui.body == NULL || !lv_obj_is_valid(s_alarm_ui.body)) return;
    lv_obj_update_layout(s_alarm_ui.body);
    if(s_alarm_ui.anchor_id[0] == '\0') return;
    for(i = 0; i < s_alarm_ui.row_n; i++) {
        lv_obj_t * row;
        int32_t target;

        if(strcmp(s_alarm_ui.row_id[i], s_alarm_ui.anchor_id) != 0) continue;
        row = s_alarm_ui.row_obj[i];
        if(row == NULL || !lv_obj_is_valid(row)) break;
        target = lv_obj_get_y(row) - s_alarm_ui.anchor_y;
        if(target < 0) target = 0;
        lv_obj_scroll_to_y(s_alarm_ui.body, target, LV_ANIM_OFF);
        return;
    }
}

static void on_alarm_scroll_begin(lv_event_t * e)
{
    LV_UNUSED(e);
    s_alarm_ui.scrolling = true;
    alarm_capture_anchor();
    s_alarm_press.valid = false;
}

static void refresh_alarm(void * user);

static void on_alarm_scroll_end(lv_event_t * e)
{
    LV_UNUSED(e);
    s_alarm_ui.scrolling = false;
    if(s_alarm_ui.struct_pending) {
        s_alarm_ui.struct_pending = false;
        /* Force signature mismatch path by clearing, then refresh. */
        s_alarm_ui.sig[0] = '\0';
        refresh_alarm(NULL);
        alarm_restore_anchor();
    }
}

static void fmt_f1(char * buf, size_t n, float v)
{
    int vi = (int)v;
    int vf = (int)((v - (float)vi) * 10.0f);
    if(vf < 0) vf = -vf;
    lv_snprintf(buf, n, "%d.%d", vi, vf);
}

static void set_ai_block(const char * head, const char * body)
{
    if(s_alarm_ui.ai_lab == NULL) {
        return;
    }
    if(s_alarm_ui.ai_head != NULL) {
        lv_label_set_text(s_alarm_ui.ai_head, head);
        lv_obj_clear_flag(s_alarm_ui.ai_head, LV_OBJ_FLAG_HIDDEN);
    }
    lv_label_set_text(s_alarm_ui.ai_lab, body);
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

    if(!alarm_press_ok(idx)) {
        s_alarm_press.valid = false;
        return;
    }
    vg_model_mute_alarm_id(s_alarm_press.id);
    s_alarm_press.valid = false;
    vg_shell_toast("已静音");
}

static void on_row_ack(lv_event_t * e)
{
    int idx = (int)(intptr_t)lv_event_get_user_data(e);

    if(!alarm_press_ok(idx)) {
        s_alarm_press.valid = false;
        return;
    }
    vg_model_ack_alarm_id(s_alarm_press.id);
    s_alarm_press.valid = false;
    vg_shell_toast("已标记处理");
}

static void on_row_btn_pressed(lv_event_t * e)
{
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    alarm_press_bind(idx);
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
    lv_obj_add_event_cb(btn, on_row_btn_pressed, LV_EVENT_PRESSED, (void *)(intptr_t)idx);
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
    lv_obj_t * ai;

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

    /* AI advice line, one per row, hidden until there is advice for this
     * exact alarm episode.  Keeping it a separate line rather than replacing
     * the rule summary means the deterministic numbers never disappear: the
     * row simply grows by one line while advice is available. */
    ai = lv_label_create(row);
    lv_label_set_long_mode(ai, LV_LABEL_LONG_DOT);
    lv_obj_set_width(ai, lv_pct(100));
    lv_label_set_text(ai, "");
    lv_obj_set_style_text_font(ai, vg_font_small(), 0);
    lv_obj_set_style_text_color(ai, vg_color_info(), 0);
    lv_obj_add_flag(ai, LV_OBJ_FLAG_HIDDEN);

    s_alarm_ui.row_obj[idx] = row;
    s_alarm_ui.row_name[idx] = name;
    s_alarm_ui.row_chip[idx] = chip;
    s_alarm_ui.row_sum[idx] = sum;
    s_alarm_ui.row_ai[idx] = ai;
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
    if(s_alarm_ui.row_ai[idx]) {
        vg_ai_advice_entry_t adv;
        uint32_t epoch = (s != NULL) ? s->al_epoch : 0;

        /* One line from the validated cache, keyed to this exact alarm
         * episode.  A miss hides the line rather than inventing text, so
         * the row falls back to exactly today's appearance. */
        if(vg_ui_alarm_advice_get(a->sensor_id, epoch, &adv)) {
            char ai[96];
            lv_snprintf(ai, sizeof(ai), "AI · %s", adv.sum);
            lv_label_set_text(s_alarm_ui.row_ai[idx], ai);
            lv_obj_clear_flag(s_alarm_ui.row_ai[idx], LV_OBJ_FLAG_HIDDEN);
        }
        else {
            /* Hide and clear: hiding is what the user sees, clearing keeps
             * the widget tree free of advice that no longer applies. */
            lv_label_set_text(s_alarm_ui.row_ai[idx], "");
            lv_obj_add_flag(s_alarm_ui.row_ai[idx], LV_OBJ_FLAG_HIDDEN);
        }
    }
}

/* AI advice block for the selected alarm.
 *
 * The only source of AI text is a cache entry the board already validated
 * against the VGADV1 contract, so there is no path here that can invent an
 * explanation.  Everything else -- still generating, failed, offline, or a
 * plain miss -- keeps the deterministic rule summary that render_detail
 * just built, and only the heading changes to say why. */
static void render_ai_detail(const vg_alarm_t * a, const vg_sensor_t * as,
                             const char * rule_text)
{
    vg_ai_advice_entry_t adv;
    uint32_t epoch = (as != NULL) ? as->al_epoch : 0;
    char body[640];
    const char * head;

    if(as != NULL && vg_ui_alarm_advice_get(a->sensor_id, epoch, &adv)) {
        lv_snprintf(body, sizeof(body),
                    "【AI 建议】%s\n【依据】%s\n【建议关注】%s%s",
                    adv.sum,
                    adv.ev[0] ? adv.ev : "（未给出）",
                    adv.att[0] ? adv.att : "（未给出）",
                    adv.unresolved ? "\n证据不足，未能给出确定结论" : "");
        set_ai_block("OPENVELACLAW 建议（AI 推测）", body);
        return;
    }

    switch(vg_ui_alarm_advice_state()) {
        case VG_UI_ADV_PENDING:
            head = "AI 建议生成中，暂显示规则摘要";
            break;
        case VG_UI_ADV_ERROR:
            head = "AI 建议不可用，显示规则摘要";
            break;
        /* Distinct from ERROR on purpose: the agent has no LLM credentials, so
         * waiting will not change anything and the fix is to provision the
         * board again. */
        case VG_UI_ADV_NO_CRED:
            head = "AI 凭证未配置，显示规则摘要";
            break;
        default:
            head = "规则摘要（本地）";
            break;
    }

    set_ai_block(head, rule_text);
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
        set_ai_block("规则摘要（本地）", "【规则摘要】当前无活动告警。");
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
    render_ai_detail(a, as, buf);
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
        if(alarm_gesture_active()) {
            /* Keep geometry; apply once when the gesture ends. */
            strncpy(s_alarm_ui.pending_sig, newsig, sizeof(s_alarm_ui.pending_sig) - 1);
            s_alarm_ui.pending_sig[sizeof(s_alarm_ui.pending_sig) - 1] = '\0';
            s_alarm_ui.struct_pending = true;
        }
        else {
            strncpy(s_alarm_ui.sig, newsig, sizeof(s_alarm_ui.sig) - 1);
            s_alarm_ui.sig[sizeof(s_alarm_ui.sig) - 1] = '\0';
            rebuild_rows(list, n);
            s_alarm_ui.struct_pending = false;
        }
    }

    if(!s_alarm_ui.struct_pending) {
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
    }
    else {
        /* Structure frozen: still refresh same-ID rows in place; skip reorder. */
        for(i = 0; i < s_alarm_ui.row_n; i++) {
            int j;
            for(j = 0; j < n; j++) {
                if(strcmp(s_alarm_ui.row_id[i], list[j].sensor_id) == 0) {
                    set_row_texts(i, &list[j]);
                    if(sel == NULL && s_alarm_ui.sel_id[0] != '\0' &&
                       strcmp(s_alarm_ui.sel_id, list[j].sensor_id) == 0) {
                        sel = &list[j];
                    }
                    break;
                }
            }
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
    lv_obj_add_event_cb(body, on_alarm_scroll_begin, LV_EVENT_SCROLL_BEGIN, NULL);
    lv_obj_add_event_cb(body, on_alarm_scroll_end, LV_EVENT_SCROLL_END, NULL);

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

void vg_page_alarm_nav_capture(vg_nav_state_t * st)
{
    uint16_t n = 0;
    const vg_sensor_t * sensors;

    if(st == NULL) return;
    if(s_alarm_ui.sel_id[0] != '\0') {
        strncpy(st->alarm_id, s_alarm_ui.sel_id, sizeof(st->alarm_id) - 1);
    }
    if(s_alarm_ui.body != NULL && lv_obj_is_valid(s_alarm_ui.body)) {
        st->scroll_y = lv_obj_get_scroll_y(s_alarm_ui.body);
    }
    sensors = vg_model_get_sensors(&n);
    if(sensors != NULL && st->alarm_id[0] != '\0') {
        uint16_t i;
        for(i = 0; i < n; i++) {
            if(strcmp(sensors[i].id, st->alarm_id) == 0) {
                st->alarm_epoch = sensors[i].al_epoch;
                break;
            }
        }
    }
}

void vg_page_alarm_nav_restore(const vg_nav_state_t * st)
{
    uint16_t n = 0;
    vg_alarm_t list[ALARM_LIST_MAX];
    uint16_t i;
    bool found = false;

    if(st == NULL) return;
    if(s_alarm_ui.root == NULL || !lv_obj_is_valid(s_alarm_ui.root)) return;

    n = vg_model_collect_alarms(list, ALARM_LIST_MAX);
    if(st->alarm_id[0] != '\0') {
        for(i = 0; i < n; i++) {
            if(strcmp(list[i].sensor_id, st->alarm_id) == 0) {
                found = true;
                break;
            }
        }
    }

    if(found) {
        strncpy(s_alarm_ui.sel_id, st->alarm_id, sizeof(s_alarm_ui.sel_id) - 1);
    }
    else if(n > 0) {
        /* Original alarm recovered: pick current highest-priority active. */
        strncpy(s_alarm_ui.sel_id, list[0].sensor_id, sizeof(s_alarm_ui.sel_id) - 1);
    }
    else {
        s_alarm_ui.sel_id[0] = '\0';
    }

    refresh_alarm(NULL);
    if(s_alarm_ui.body != NULL && lv_obj_is_valid(s_alarm_ui.body) && found) {
        lv_obj_update_layout(s_alarm_ui.body);
        lv_obj_scroll_to_y(s_alarm_ui.body, st->scroll_y, LV_ANIM_OFF);
    }
}
