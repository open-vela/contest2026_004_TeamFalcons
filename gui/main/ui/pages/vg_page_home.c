#include "vg_pages.h"
#include "widgets/vg_widgets.h"
#include "theme/vg_theme.h"
#include "model/vg_model.h"
#include "vg_display.h"
#include <stdio.h>
#include <string.h>
#include <stdint.h>

/*
 * Sensor grid budget (content host inner height = 232px):
 *   filter bar 36 + gap 3 + grid ~154 + gap 3 + actions 36.
 * Grid holds 4 rows of 36px tiles on 3px tracks (4*36 + 3*3 = 153),
 * 2 columns per row -> 8 sensors per screen; scroll beyond.
 * Tiles keep the >=36px industrial touch target (VG_MIN_TOUCH_H).
 */
#define HOME_TRACK_GAP 3

typedef struct {
    lv_obj_t * root;
    lv_obj_t * filt_bar;
    lv_obj_t * filt_btn[4];
    lv_obj_t * filt_lab[4];
    lv_obj_t * list;
    /* Rebuild only when visible set changes (not every model tick) */
    vg_home_filter_t list_filter;
    uint16_t list_n;
    char list_sig[160];
} home_ctx_t;

static home_ctx_t s_home;

static void fmt_value_unit(char * buf, size_t n, float v, const char * unit, bool online)
{
    int vi = (int)v;
    int vf = (int)((v - (float)vi) * 10.0f);
    if(vf < 0) vf = -vf;
    if(online) lv_snprintf(buf, n, "%d.%d%s", vi, vf, unit);
    else lv_snprintf(buf, n, "--%s", unit);
}

static void style_filt_btn(lv_obj_t * btn, lv_obj_t * lab, bool on)
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

static void refresh_filter_chips(void)
{
    uint16_t all = 0, alarm = 0, offline = 0, ok = 0;
    char buf[24];
    vg_home_filter_t f = vg_model_get_home_filter();
    int i;
    vg_model_count_by_filter(&all, &alarm, &offline, &ok);

    lv_snprintf(buf, sizeof(buf), "全部 %u", (unsigned)all);
    if(s_home.filt_lab[0]) lv_label_set_text(s_home.filt_lab[0], buf);
    lv_snprintf(buf, sizeof(buf), "告警 %u", (unsigned)alarm);
    if(s_home.filt_lab[1]) lv_label_set_text(s_home.filt_lab[1], buf);
    lv_snprintf(buf, sizeof(buf), "离线 %u", (unsigned)offline);
    if(s_home.filt_lab[2]) lv_label_set_text(s_home.filt_lab[2], buf);
    lv_snprintf(buf, sizeof(buf), "正常 %u", (unsigned)ok);
    if(s_home.filt_lab[3]) lv_label_set_text(s_home.filt_lab[3], buf);

    for(i = 0; i < 4; i++) {
        if(s_home.filt_btn[i] && s_home.filt_lab[i])
            style_filt_btn(s_home.filt_btn[i], s_home.filt_lab[i], (int)f == i);
    }
}

static void on_row(lv_event_t * e)
{
    const char * id = (const char *)lv_event_get_user_data(e);
    if(id) vg_model_set_selected_sensor(id);
    vg_nav_goto(VG_PAGE_DEVICE, NULL);
}

/* Signature of visible set: filter + ids (order). Values change every tick — not in sig. */
static void build_list_sig(char * out, size_t n)
{
    uint16_t cnt = vg_model_home_sensor_count();
    uint16_t i;
    size_t used = 0;
    int w;

    w = lv_snprintf(out, n, "%d:%u|", (int)vg_model_get_home_filter(), (unsigned)cnt);
    if(w < 0) {
        out[0] = '\0';
        return;
    }
    used = (size_t)w;
    for(i = 0; i < cnt && used + 1 < n; i++) {
        const vg_sensor_t * s = vg_model_home_sensor_at(i);
        if(s == NULL) continue;
        w = lv_snprintf(out + used, n - used, "%s,", s->id);
        if(w < 0) break;
        used += (size_t)w;
    }
}

/* In-place value/severity refresh for one tile */
static void set_tile_value(lv_obj_t * tile, const vg_sensor_t * s)
{
    lv_obj_t * val = (lv_obj_t *)lv_obj_get_user_data(tile);
    lv_obj_t * name = lv_obj_get_child(tile, 0);
    char vbuf[20];

    if(val && lv_obj_is_valid(val)) {
        fmt_value_unit(vbuf, sizeof(vbuf), s->value, s->unit,
                       s->online && s->quality_pct > 0);
        lv_label_set_text(val, vbuf);
        lv_obj_set_style_text_color(val, vg_color_severity(s->severity), 0);
    }
    if(name && lv_obj_is_valid(name) && lv_obj_check_type(name, &lv_label_class)) {
        lv_obj_set_style_text_color(name, s->online ? vg_color_text() : vg_color_muted(), 0);
    }
    lv_obj_set_style_border_color(tile, vg_color_severity(s->severity), 0);
}

static lv_obj_t * make_sensor_tile(lv_obj_t * parent, const vg_sensor_t * s, uint16_t idx)
{
    char vbuf[20];
    lv_obj_t * tile;
    lv_obj_t * name;
    lv_obj_t * val;

    /*
     * Plain obj tile (stacking recipe as vg_page_logs rows): fixed touch
     * height, flex row of name + value; severity carried by the left
     * accent border and the value color instead of a text label.
     */
    tile = lv_obj_create(parent);
    lv_obj_remove_flag(tile, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(tile, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE);
    lv_obj_set_height(tile, VG_MIN_TOUCH_H);
    lv_obj_set_flex_grow(tile, 1);
    lv_obj_set_style_radius(tile, VG_CARD_RADIUS, 0);
    lv_obj_set_style_border_side(tile, LV_BORDER_SIDE_LEFT, 0);
    lv_obj_set_style_border_width(tile, 4, 0);
    lv_obj_set_style_border_color(tile, vg_color_severity(s->severity), 0);
    /* Zebra by grid row (idx >> 1) so both tiles of a pair match */
    lv_obj_set_style_bg_color(tile, ((idx >> 1) & 1) ? lv_color_hex(0x1E2228) : vg_color_surface(), 0);
    lv_obj_set_style_bg_opa(tile, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(tile, lv_color_hex(0x2A3138), LV_STATE_PRESSED);
    lv_obj_set_style_pad_hor(tile, 8, 0);
    lv_obj_set_style_pad_ver(tile, 0, 0);
    lv_obj_set_flex_flow(tile, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(tile, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(tile, 6, 0);
    lv_obj_add_event_cb(tile, on_row, LV_EVENT_CLICKED, (void *)s->id);

    name = lv_label_create(tile);
    lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
    lv_obj_set_flex_grow(name, 1);
    lv_label_set_text(name, s->name);
    lv_obj_set_style_text_font(name, vg_font_ui(), 0);
    lv_obj_set_style_text_color(name, s->online ? vg_color_text() : vg_color_muted(), 0);

    val = lv_label_create(tile);
    fmt_value_unit(vbuf, sizeof(vbuf), s->value, s->unit,
                   s->online && s->quality_pct > 0);
    lv_label_set_text(val, vbuf);
    lv_obj_set_style_text_font(val, vg_font_metric(), 0);
    lv_obj_set_style_text_color(val, vg_color_severity(s->severity), 0);

    /* Store value label for in-place refresh; name is child 0 */
    lv_obj_set_user_data(tile, val);
    return tile;
}

/* One grid row: two tiles side by side (filler keeps odd tail tile at half width) */
static lv_obj_t * make_pair(lv_obj_t * parent)
{
    lv_obj_t * pair = lv_obj_create(parent);
    lv_obj_remove_flag(pair, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_width(pair, lv_pct(100));
    lv_obj_set_height(pair, VG_MIN_TOUCH_H);
    lv_obj_set_style_radius(pair, 0, 0);
    lv_obj_set_style_bg_opa(pair, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(pair, 0, 0);
    lv_obj_set_style_pad_all(pair, 0, 0);
    lv_obj_set_flex_flow(pair, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(pair, HOME_TRACK_GAP, 0);
    return pair;
}

static void make_pair_filler(lv_obj_t * pair)
{
    lv_obj_t * filler = lv_obj_create(pair);
    lv_obj_set_flex_grow(filler, 1);
    lv_obj_set_height(filler, 1);
    lv_obj_set_style_bg_opa(filler, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(filler, 0, 0);
    lv_obj_remove_flag(filler, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
}

static void update_tile_texts(void)
{
    uint16_t n = vg_model_home_sensor_count();
    uint16_t i;
    uint32_t pair_n;

    if(s_home.list == NULL || !lv_obj_is_valid(s_home.list)) return;
    pair_n = lv_obj_get_child_count(s_home.list);
    if(pair_n != (uint32_t)((n + 1) / 2)) return; /* set changed — rebuild will handle */

    for(i = 0; i < n; i++) {
        const vg_sensor_t * s = vg_model_home_sensor_at(i);
        lv_obj_t * pair = lv_obj_get_child(s_home.list, (int32_t)(i >> 1));
        lv_obj_t * tile;
        if(s == NULL || pair == NULL) continue;
        tile = lv_obj_get_child(pair, (int32_t)(i & 1));
        if(tile == NULL || !lv_obj_is_valid(tile)) continue;
        set_tile_value(tile, s);
    }
}

static void rebuild_list(bool force)
{
    char sig[160];
    uint16_t n;
    uint16_t i;

    if(s_home.list == NULL || !lv_obj_is_valid(s_home.list)) return;

    build_list_sig(sig, sizeof(sig));
    if(!force && s_home.list_n > 0 && strcmp(sig, s_home.list_sig) == 0) {
        update_tile_texts();
        return;
    }

    lv_obj_clean(s_home.list);

    n = vg_model_home_sensor_count();
    if(n == 0) {
        lv_obj_t * empty = lv_label_create(s_home.list);
        /* Reuse discover-page CJK glyphs. ALL = no confirmed table yet. */
        lv_label_set_text(empty,
            vg_model_get_home_filter() == VG_HOME_FILTER_ALL ?
            "未发现从站" : "无匹配传感器");
        vg_style_apply_label(empty, true);
        lv_obj_set_width(empty, lv_pct(100));
        lv_obj_set_style_text_align(empty, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_pad_ver(empty, 24, 0);
        s_home.list_n = 0;
        s_home.list_filter = vg_model_get_home_filter();
        lv_snprintf(s_home.list_sig, sizeof(s_home.list_sig), "%s", sig);
        return;
    }

    for(i = 0; i < n; i += 2) {
        lv_obj_t * pair = make_pair(s_home.list);
        const vg_sensor_t * a = vg_model_home_sensor_at(i);
        const vg_sensor_t * b = vg_model_home_sensor_at(i + 1);
        if(a) make_sensor_tile(pair, a, i);
        if(b) make_sensor_tile(pair, b, i + 1);
        else make_pair_filler(pair);
    }

    s_home.list_n = n;
    s_home.list_filter = vg_model_get_home_filter();
    lv_snprintf(s_home.list_sig, sizeof(s_home.list_sig), "%s", sig);
}

static void refresh_home(void * user)
{
    LV_UNUSED(user);
    if(vg_nav_current() != VG_PAGE_HOME) return;
    if(s_home.root == NULL || !lv_obj_is_valid(s_home.root)) return;

    refresh_filter_chips();
    rebuild_list(false);
}

static void on_filter(lv_event_t * e)
{
    vg_home_filter_t f = (vg_home_filter_t)(intptr_t)lv_event_get_user_data(e);
    vg_model_set_home_filter(f);
    /* Force rebuild so filter switch is immediate */
    rebuild_list(true);
    refresh_filter_chips();
}

static void on_detail(lv_event_t * e)
{
    LV_UNUSED(e);
    vg_nav_goto(VG_PAGE_DEVICE, NULL);
}

static void on_trend(lv_event_t * e)
{
    LV_UNUSED(e);
    vg_nav_goto(VG_PAGE_TREND, NULL);
}

static void on_nav(lv_event_t * e)
{
    vg_page_id_t id = (vg_page_id_t)(intptr_t)lv_event_get_user_data(e);
    vg_nav_goto(id, NULL);
}

/* Manual 6.3 quick action: mute the active alarm from home */
static void on_mute(lv_event_t * e)
{
    const vg_alarm_t * a;
    LV_UNUSED(e);
    a = vg_model_get_active_alarm();
    if(a && a->active) {
        vg_model_mute_alarm();
        vg_shell_toast("已静音");
    }
    else {
        vg_shell_toast("当前无活动告警");
    }
}

static void on_home_delete(lv_event_t * e)
{
    LV_UNUSED(e);
    vg_model_off_change(refresh_home, NULL);
    memset(&s_home, 0, sizeof(s_home));
}

static lv_obj_t * make_action(lv_obj_t * parent, const char * text, bool primary, lv_event_cb_t cb, void * ud)
{
    lv_obj_t * btn = lv_button_create(parent);
    /* 5 actions: 5x62 + 4x4 gaps = 326 <= 468 inner width */
    lv_obj_set_size(btn, 62, VG_MIN_TOUCH_H);
    vg_style_apply_btn(btn, primary);
    lv_obj_t * lab = lv_label_create(btn);
    lv_label_set_text(lab, text);
    lv_obj_set_style_text_font(lab, vg_font_ui(), 0);
    lv_obj_center(lab);
    if(cb) lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, ud);
    return btn;
}

static lv_obj_t * make_filt(lv_obj_t * parent, int idx)
{
    lv_obj_t * btn = lv_button_create(parent);
    lv_obj_t * lab;
    lv_obj_set_size(btn, 110, VG_MIN_TOUCH_H);
    lv_obj_set_style_radius(btn, 3, 0);
    lv_obj_set_style_border_width(btn, 1, 0);
    lv_obj_set_style_pad_hor(btn, 4, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lab = lv_label_create(btn);
    lv_label_set_text(lab, "");
    lv_obj_set_style_text_font(lab, vg_font_small(), 0);
    lv_obj_center(lab);
    lv_obj_add_event_cb(btn, on_filter, LV_EVENT_CLICKED, (void *)(intptr_t)idx);
    s_home.filt_btn[idx] = btn;
    s_home.filt_lab[idx] = lab;
    return btn;
}

void vg_page_home_create(lv_obj_t * parent, const void * args)
{
    lv_obj_t * actions;
    int i;
    LV_UNUSED(args);
    memset(&s_home, 0, sizeof(s_home));
    s_home.root = parent;
    lv_obj_add_event_cb(parent, on_home_delete, LV_EVENT_DELETE, NULL);

    /*
     * Column flex, no page scroll — grid scrolls inside.
     * 3px section gaps (not VG_GAP) so the tile grid keeps 4 full rows:
     * 36 + 3 + 154 + 3 + 36 = 232 = inner content height.
     */
    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(parent, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(parent, HOME_TRACK_GAP, 0);
    lv_obj_remove_flag(parent, LV_OBJ_FLAG_SCROLLABLE);

    /* Filter bar — fixed touch height, no grow */
    s_home.filt_bar = lv_obj_create(parent);
    lv_obj_remove_flag(s_home.filt_bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(s_home.filt_bar, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_home.filt_bar, 0, 0);
    lv_obj_set_style_pad_all(s_home.filt_bar, 0, 0);
    lv_obj_set_width(s_home.filt_bar, lv_pct(100));
    lv_obj_set_height(s_home.filt_bar, VG_MIN_TOUCH_H);
    lv_obj_set_style_flex_grow(s_home.filt_bar, 0, 0);
    lv_obj_set_flex_flow(s_home.filt_bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(s_home.filt_bar, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(s_home.filt_bar, 4, 0);
    for(i = 0; i < 4; i++) make_filt(s_home.filt_bar, i);

    /*
     * Tile grid — scroll container of pair rows (same grow recipe as
     * vg_page_logs list): flex_grow=1 + min_height=0, vertical scroll only.
     * Tiles carry the visuals, so the container stays bare.
     */
    s_home.list = lv_obj_create(parent);
    lv_obj_set_width(s_home.list, lv_pct(100));
    lv_obj_set_flex_grow(s_home.list, 1);
    lv_obj_set_style_min_height(s_home.list, 0, 0);
    lv_obj_set_style_bg_opa(s_home.list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_home.list, 0, 0);
    lv_obj_set_style_radius(s_home.list, 0, 0);
    lv_obj_set_style_pad_all(s_home.list, 0, 0);
    lv_obj_set_style_pad_row(s_home.list, HOME_TRACK_GAP, 0);
    lv_obj_set_flex_flow(s_home.list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_home.list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_add_flag(s_home.list, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(s_home.list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(s_home.list, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_remove_flag(s_home.list, LV_OBJ_FLAG_SCROLL_ELASTIC);

    /*
     * Actions — fixed, no grow. Active-alarm path: the status-bar
     * "告警!" chip jumps to the alarm page, 静音 stays here as the
     * quick action (the old home alarm strip was folded into both).
     */
    actions = lv_obj_create(parent);
    lv_obj_set_style_flex_grow(actions, 0, 0);
    lv_obj_remove_flag(actions, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(actions, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(actions, 0, 0);
    lv_obj_set_style_pad_all(actions, 0, 0);
    lv_obj_set_width(actions, lv_pct(100));
    lv_obj_set_height(actions, VG_MIN_TOUCH_H);
    lv_obj_set_flex_flow(actions, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(actions, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(actions, 4, 0);

    make_action(actions, "详情", true, on_detail, NULL);
    make_action(actions, "告警", false, on_nav, (void *)(intptr_t)VG_PAGE_ALARM);
    make_action(actions, "报告", false, on_nav, (void *)(intptr_t)VG_PAGE_REPORT);
    make_action(actions, "探查", false, on_nav, (void *)(intptr_t)VG_PAGE_DISCOVER);
    make_action(actions, "静音", false, on_mute, NULL);

    vg_model_on_change(refresh_home, NULL);
    refresh_home(NULL);
}
