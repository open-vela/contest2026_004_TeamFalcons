#include "vg_pages.h"
#include "theme/vg_theme.h"
#include "model/vg_model.h"
#include "vg_display.h"
#include <stdio.h>
#include <string.h>
#include <stdint.h>

#define VG_TREND_WIN_RECENT 60u  /* recent window, in samples */
#define VG_TREND_SERIES_N 4u     /* ser + thr_warn + thr_crit + anomaly */
#define VG_TREND_OPT_MAX 2048u   /* dropdown options text budget */

typedef struct {
    lv_obj_t * root;
    lv_obj_t * chart;
    lv_chart_series_t * ser;
    lv_chart_series_t * thr_warn;
    lv_chart_series_t * thr_crit;
    lv_chart_series_t * anomaly;
    lv_obj_t * value_lab;
    lv_obj_t * meta_lab;
    lv_obj_t * win_btn[2];
    lv_obj_t * win_lab[2];
    lv_obj_t * point_dd;
    uint16_t dd_count;            /* sensors listed when options were built */
    uint8_t window_recent;        /* 1 = last 60 samples, 0 = full history */
} trend_ctx_t;

static trend_ctx_t s_trend;
static char s_point_opts[VG_TREND_OPT_MAX];

static void refresh_trend(void * user);

static void fmt_f1(char * buf, size_t n, float v)
{
    int vi = (int)v;
    int vf = (int)((v - (float)vi) * 10.0f);
    if(vf < 0) vf = -vf;
    lv_snprintf(buf, n, "%d.%d", vi, vf);
}

/*
 * LVGL 9 chart styles line width / point size per chart, not per series.
 * lv_chart.c draw_series() iterates the series list tail→head and assigns
 * base.id1 = (series_count - 1) down to 0 in draw order. Series are added
 * at the list tail, so the anomaly series (added last, drawn FIRST) carries
 * id1 == VG_TREND_SERIES_N - 1, and the main data series (added first,
 * drawn last) carries id1 == 0. Hide the anomaly series' connecting line
 * and the dot tasks of every other series so only red over-threshold dots
 * remain (manual 6.5).
 */
static void chart_draw_task_cb(lv_event_t * e)
{
    lv_draw_task_t * task = lv_event_get_draw_task(e);
    lv_draw_dsc_base_t * base = (lv_draw_dsc_base_t *)task->draw_dsc;

    if(task->type == LV_DRAW_TASK_TYPE_LINE && base->part == LV_PART_ITEMS) {
        if(base->id1 == (uint32_t)(VG_TREND_SERIES_N - 1)) {
            lv_draw_line_dsc_t * dsc = (lv_draw_line_dsc_t *)task->draw_dsc;
            dsc->width = 0;
            dsc->opa = LV_OPA_TRANSP;
        }
    }
    else if(task->type == LV_DRAW_TASK_TYPE_FILL && base->part == LV_PART_INDICATOR) {
        if(base->id1 != (uint32_t)(VG_TREND_SERIES_N - 1)) {
            lv_draw_rect_dsc_t * dsc = (lv_draw_rect_dsc_t *)task->draw_dsc;
            dsc->bg_opa = LV_OPA_TRANSP;
        }
    }
}

static uint32_t window_count(void)
{
    return s_trend.window_recent ? VG_TREND_WIN_RECENT : (uint32_t)VG_HISTORY_LEN;
}

static uint32_t window_offset(uint32_t count)
{
    if(!s_trend.window_recent) return 0;
    if((uint32_t)VG_HISTORY_LEN < count) return 0;
    return (uint32_t)VG_HISTORY_LEN - count;
}

/*
 * Dropdown text lists every point by display name; id is the lookup key
 * (names may repeat or be UTF-8), so duplicates get an id suffix and the
 * selection callback maps the chosen row back through the sensor array.
 */
static void rebuild_point_options(void)
{
    const vg_sensor_t * sensors;
    const char * sel_id = vg_model_get_selected_sensor_id();
    uint16_t n = 0;
    uint16_t i;
    uint16_t sel_idx = 0;
    size_t used = 0;

    s_point_opts[0] = '\0';
    sensors = vg_model_get_sensors(&n);
    if(sensors == NULL) n = 0;

    for(i = 0; i < n; i++) {
        char entry[VG_SENSOR_NAME_MAX + VG_SENSOR_ID_MAX + 4];
        uint16_t j;
        bool dup = false;

        for(j = 0; j < i; j++) {
            if(strcmp(sensors[j].name, sensors[i].name) == 0) {
                dup = true;
                break;
            }
        }
        if(dup) lv_snprintf(entry, sizeof(entry), "%s ·%s", sensors[i].name, sensors[i].id);
        else lv_snprintf(entry, sizeof(entry), "%s", sensors[i].name);

        if(used + strlen(entry) + 2 >= sizeof(s_point_opts)) break;
        if(used > 0) s_point_opts[used++] = '\n';
        strcpy(s_point_opts + used, entry);
        used += strlen(entry);

        if(sel_id != NULL && strcmp(sensors[i].id, sel_id) == 0) sel_idx = i;
    }

    lv_dropdown_set_options(s_trend.point_dd, s_point_opts);
    if(n > 0) lv_dropdown_set_selected(s_trend.point_dd, sel_idx);
    s_trend.dd_count = n;
}

static void on_point_selected(lv_event_t * e)
{
    lv_obj_t * dd = lv_event_get_target(e);
    uint16_t sel = (uint16_t)lv_dropdown_get_selected(dd);
    const vg_sensor_t * sensors;
    uint16_t n = 0;

    sensors = vg_model_get_sensors(&n);
    if(sensors == NULL || sel >= n) return;
    vg_model_set_selected_sensor(sensors[sel].id);
    /* lv_dropdown_close only hides the list and never invalidates the
     * button, so the caption would keep the previous option string */
    lv_obj_invalidate(dd);
    refresh_trend(NULL);
}

static void refresh_trend(void * user)
{
    const vg_sensor_t * s;
    char buf[64];
    char a[16], b[16], c[16];
    uint32_t i;
    uint32_t count;
    uint32_t offset;
    uint16_t sensor_n = 0;
    LV_UNUSED(user);
    if(vg_nav_current() != VG_PAGE_TREND) return;
    if(s_trend.root == NULL || !lv_obj_is_valid(s_trend.root)) return;

    /* Point table can re-import while the page is open: resync the list */
    vg_model_get_sensors(&sensor_n);
    if(sensor_n != s_trend.dd_count) rebuild_point_options();

    s = vg_model_get_selected_sensor();
    if(s == NULL || s_trend.chart == NULL) return;

    if(s->online) {
        fmt_f1(a, sizeof(a), s->value);
        lv_snprintf(buf, sizeof(buf), "%s %s", a, s->unit);
    }
    else {
        lv_snprintf(buf, sizeof(buf), "-- %s", s->unit);
    }
    lv_label_set_text(s_trend.value_lab, buf);
    lv_obj_set_style_text_color(s_trend.value_lab, vg_color_severity(s->severity), 0);

    count = window_count();
    offset = window_offset(count);
    if(offset + count > (uint32_t)s->history_len) {
        offset = 0;
        count = (uint32_t)s->history_len;
    }

    if(s->has_warn || s->has_crit) {
        fmt_f1(b, sizeof(b), s->thr_warn);
        fmt_f1(c, sizeof(c), s->thr_crit);
        lv_snprintf(buf, sizeof(buf), "阈值 预警%s / 严重%s · 当前 %u 点", b, c,
                    (unsigned)count);
    }
    else {
        b[0] = '\0';
        lv_snprintf(buf, sizeof(buf), "无阈值 · 当前 %u 点", (unsigned)count);
    }
    lv_label_set_text(s_trend.meta_lab, buf);

    if(s->has_warn || s->has_crit) {
        int32_t min_v = (int32_t)((s->thr_low - 5.0f) * 10.0f);
        int32_t max_v = (int32_t)((s->thr_crit + 10.0f) * 10.0f);
        if(max_v <= min_v) max_v = min_v + 200;
        lv_chart_set_range(s_trend.chart, LV_CHART_AXIS_PRIMARY_Y, min_v, max_v);
    }
    else {
        /* No thresholds: frame the drawn window with a 10% margin so the
         * curve stays visible for any data range. */
        float lo = 0.0f, hi = 0.0f;
        if(count > 0) {
            lo = s->history[offset];
            hi = lo;
            for(i = 1; i < count; i++) {
                float v = s->history[offset + i];
                if(v < lo) lo = v;
                if(v > hi) hi = v;
            }
        }
        if(hi - lo < 0.002f) {
            lo -= 1.0f;
            hi += 1.0f;
        }
        else {
            float m = (hi - lo) * 0.1f;
            lo -= m;
            hi += m;
        }
        lv_chart_set_range(s_trend.chart, LV_CHART_AXIS_PRIMARY_Y,
                           (int32_t)(lo * 10.0f), (int32_t)(hi * 10.0f));
    }

    lv_chart_set_point_count(s_trend.chart, count);
    for(i = 0; i < count; i++) {
        float v = s->history[offset + i];
        lv_chart_set_value_by_id(s_trend.chart, s_trend.ser, i, (int32_t)(v * 10.0f));
        /* Threshold lines only where a threshold exists, else gaps */
        lv_chart_set_value_by_id(s_trend.chart, s_trend.thr_warn, i,
                                 s->has_warn ? (int32_t)(s->thr_warn * 10.0f) : LV_CHART_POINT_NONE);
        lv_chart_set_value_by_id(s_trend.chart, s_trend.thr_crit, i,
                                 s->has_crit ? (int32_t)(s->thr_crit * 10.0f) : LV_CHART_POINT_NONE);
        /* red marker above warn threshold, otherwise no point */
        lv_chart_set_value_by_id(s_trend.chart, s_trend.anomaly, i,
                                 (v > s->thr_warn && s->has_warn) || (v > s->thr_crit && s->has_crit)
                                     ? (int32_t)(v * 10.0f) : LV_CHART_POINT_NONE);
    }
    lv_chart_refresh(s_trend.chart);
}

static void style_win_btn(lv_obj_t * btn, lv_obj_t * lab, bool on)
{
    if(on) {
        lv_obj_set_style_bg_color(btn, vg_color_accent(), 0);
        lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
        lv_obj_set_style_border_color(btn, vg_color_accent(), 0);
        lv_obj_set_style_text_color(lab, lv_color_hex(0xFFFFFF), 0);
    }
    else {
        lv_obj_set_style_bg_color(btn, vg_color_surface(), 0);
        lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
        lv_obj_set_style_border_color(btn, vg_color_border(), 0);
        lv_obj_set_style_text_color(lab, vg_color_text(), 0);
    }
}

static void refresh_win_btns(void)
{
    int i;
    for(i = 0; i < 2; i++) {
        if(s_trend.win_btn[i] && s_trend.win_lab[i])
            style_win_btn(s_trend.win_btn[i], s_trend.win_lab[i], s_trend.window_recent == (i == 0));
    }
}

static void on_window(lv_event_t * e)
{
    int recent = (int)(intptr_t)lv_event_get_user_data(e);
    LV_UNUSED(e);
    if(s_trend.window_recent == (uint8_t)recent) return;
    s_trend.window_recent = (uint8_t)recent;
    refresh_win_btns();
    refresh_trend(NULL);
}

static void on_trend_delete(lv_event_t * e)
{
    LV_UNUSED(e);
    vg_model_off_change(refresh_trend, NULL);
    memset(&s_trend, 0, sizeof(s_trend));
}

void vg_page_trend_create(lv_obj_t * parent, const void * args)
{
    lv_obj_t * head;
    lv_obj_t * list;
    LV_UNUSED(args);
    memset(&s_trend, 0, sizeof(s_trend));
    s_trend.root = parent;
    lv_obj_add_event_cb(parent, on_trend_delete, LV_EVENT_DELETE, NULL);

    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(parent, VG_GAP, 0);
    lv_obj_remove_flag(parent, LV_OBJ_FLAG_SCROLLABLE);

    /* head: point dropdown | current value | window toggles */
    head = lv_obj_create(parent);
    lv_obj_remove_flag(head, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(head, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(head, 0, 0);
    lv_obj_set_style_pad_all(head, 0, 0);
    lv_obj_set_width(head, lv_pct(100));
    lv_obj_set_height(head, 36);
    lv_obj_set_style_min_height(head, 36, 0);
    lv_obj_set_style_max_height(head, 36, 0);
    lv_obj_set_flex_flow(head, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(head, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    /* point selector: text is the display name, key is the point id */
    s_trend.point_dd = lv_dropdown_create(head);
    lv_obj_set_size(s_trend.point_dd, 150, VG_MIN_TOUCH_H);
    /* the project CJK font has no FontAwesome glyphs (U+F078 arrow) */
    lv_dropdown_set_symbol(s_trend.point_dd, NULL);
    lv_obj_set_style_bg_color(s_trend.point_dd, vg_color_surface(), 0);
    lv_obj_set_style_bg_opa(s_trend.point_dd, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(s_trend.point_dd, vg_color_border(), 0);
    lv_obj_set_style_border_width(s_trend.point_dd, 1, 0);
    lv_obj_set_style_radius(s_trend.point_dd, 3, 0);
    lv_obj_set_style_shadow_width(s_trend.point_dd, 0, 0);
    lv_obj_set_style_text_color(s_trend.point_dd, vg_color_text(), 0);
    lv_obj_set_style_text_font(s_trend.point_dd, vg_font_small(), 0);
    lv_obj_set_style_pad_hor(s_trend.point_dd, 6, 0);
    list = lv_dropdown_get_list(s_trend.point_dd);
    if(list != NULL) {
        /* default theme is light; the popup must match the dark UI */
        lv_obj_set_style_bg_color(list, vg_color_surface(), 0);
        lv_obj_set_style_border_color(list, vg_color_border(), 0);
        lv_obj_set_style_text_color(list, vg_color_text(), 0);
        lv_obj_set_style_bg_color(list, vg_color_accent(), LV_PART_SELECTED);
        lv_obj_set_style_max_height(list, 190, 0);
        lv_obj_set_style_text_font(list, vg_font_small(), 0);
    }
    lv_obj_add_event_cb(s_trend.point_dd, on_point_selected, LV_EVENT_VALUE_CHANGED, NULL);

    s_trend.value_lab = lv_label_create(head);
    /* Metric font (20) fits 36px head; avoid lg/24 clipping */
    lv_obj_set_style_text_font(s_trend.value_lab, vg_font_metric(), 0);
    lv_obj_set_style_text_color(s_trend.value_lab, vg_color_text(), 0);
    lv_label_set_text(s_trend.value_lab, "--");

    /* window toggles in samples (board sample spacing follows the poll
     * round, so minute labels would be inaccurate) */
    {
        static const char * names[2] = {"最近60点", "全部"};
        static const int widths[2] = {70, 54};
        int i;
        for(i = 0; i < 2; i++) {
            lv_obj_t * btn = lv_button_create(head);
            lv_obj_t * lab;
            lv_obj_set_size(btn, widths[i], VG_MIN_TOUCH_H);
            lv_obj_set_style_radius(btn, 3, 0);
            lv_obj_set_style_border_width(btn, 1, 0);
            lv_obj_set_style_shadow_width(btn, 0, 0);
            lab = lv_label_create(btn);
            lv_label_set_text(lab, names[i]);
            lv_obj_set_style_text_font(lab, vg_font_small(), 0);
            lv_obj_center(lab);
            lv_obj_add_event_cb(btn, on_window, LV_EVENT_CLICKED,
                                (void *)(intptr_t)(i == 0)); /* btn0 = recent 60 */
            s_trend.win_btn[i] = btn;
            s_trend.win_lab[i] = lab;
        }
    }

    /* meta line under the head: thresholds + drawn sample count */
    s_trend.meta_lab = lv_label_create(parent);
    vg_style_apply_label(s_trend.meta_lab, true);
    lv_obj_set_style_text_font(s_trend.meta_lab, vg_font_small(), 0);
    lv_label_set_long_mode(s_trend.meta_lab, LV_LABEL_LONG_DOT);
    lv_obj_set_width(s_trend.meta_lab, lv_pct(100));
    lv_obj_set_style_text_align(s_trend.meta_lab, LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_set_text(s_trend.meta_lab, "");

    s_trend.chart = lv_chart_create(parent);
    lv_obj_set_width(s_trend.chart, lv_pct(100));
    lv_obj_set_flex_grow(s_trend.chart, 1);
    lv_obj_set_style_min_height(s_trend.chart, 0, 0);
    lv_obj_set_style_bg_color(s_trend.chart, vg_color_surface(), 0);
    lv_obj_set_style_bg_opa(s_trend.chart, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(s_trend.chart, vg_color_border(), 0);
    lv_obj_set_style_border_width(s_trend.chart, 1, 0);
    lv_obj_set_style_radius(s_trend.chart, VG_CARD_RADIUS, 0);
    lv_obj_set_style_pad_all(s_trend.chart, 4, 0);
    lv_obj_set_style_line_color(s_trend.chart, vg_color_border(), LV_PART_MAIN);
    lv_chart_set_type(s_trend.chart, LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(s_trend.chart, VG_HISTORY_LEN);
    lv_chart_set_div_line_count(s_trend.chart, 4, 6);
    /* 5px dots for the anomaly marker series; other series' dots are
     * hidden in chart_draw_task_cb. */
    lv_obj_set_style_size(s_trend.chart, 5, 0, LV_PART_INDICATOR);

    s_trend.ser = lv_chart_add_series(s_trend.chart, vg_color_accent(), LV_CHART_AXIS_PRIMARY_Y);
    s_trend.thr_warn = lv_chart_add_series(s_trend.chart, vg_color_warn(), LV_CHART_AXIS_PRIMARY_Y);
    s_trend.thr_crit = lv_chart_add_series(s_trend.chart, vg_color_crit(), LV_CHART_AXIS_PRIMARY_Y);
    /* must stay the 4th (last) series — see chart_draw_task_cb */
    s_trend.anomaly = lv_chart_add_series(s_trend.chart, vg_color_crit(), LV_CHART_AXIS_PRIMARY_Y);

    lv_obj_add_flag(s_trend.chart, LV_OBJ_FLAG_SEND_DRAW_TASK_EVENTS);
    lv_obj_add_event_cb(s_trend.chart, chart_draw_task_cb, LV_EVENT_DRAW_TASK_ADDED, NULL);

    s_trend.window_recent = 1;
    rebuild_point_options();
    refresh_win_btns();
    vg_model_on_change(refresh_trend, NULL);
    refresh_trend(NULL);
}
