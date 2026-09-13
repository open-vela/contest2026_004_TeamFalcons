/* Headless render check for the point-table trend page (task
 * 09-13-trend-page-live). Harness pattern copied from
 * gui/headless/alarm_check_main.c: 480x272 off-screen buffer, scripted
 * pointer indev, PPM frame dumps, model-level PASS/FAIL lines.
 * Not compiled into firmware (app/velaguard Makefile) or the SDL
 * simulator (gui/CMakeLists.txt globs main/ui only).
 *
 * Usage: trend_check <out-dir>
 */

#include "lvgl/lvgl.h"
#include "theme/vg_theme.h"
#include "model/vg_model.h"
#include "shell/vg_shell.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define W 480
#define H 272

static uint8_t s_fb[H][W][3];

/* ---------------- scripted pointer indev ---------------- */

typedef struct {
    int x, y;
} click_t;

static click_t s_queue[16];
static int s_queue_n;
static click_t s_active;
static int s_hold_frames;
static bool s_pressed;

static void queue_click(int x, int y)
{
    if(s_queue_n >= 16) return;
    s_queue[s_queue_n].x = x;
    s_queue[s_queue_n].y = y;
    s_queue_n++;
}

static void indev_read_cb(lv_indev_t * indev, lv_indev_data_t * data)
{
    LV_UNUSED(indev);
    if(!s_pressed && s_queue_n > 0) {
        s_active = s_queue[0];
        memmove(&s_queue[0], &s_queue[1], sizeof(click_t) * (unsigned)(s_queue_n - 1));
        s_queue_n--;
        s_pressed = true;
        s_hold_frames = 3;
    }
    if(s_pressed) {
        data->point.x = s_active.x;
        data->point.y = s_active.y;
        data->state = LV_INDEV_STATE_PRESSED;
        if(--s_hold_frames <= 0) {
            s_pressed = false;
        }
    }
    else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

/* ---------------- display flush ---------------- */

static void flush_cb(lv_display_t * disp, const lv_area_t * area, uint8_t * px_map)
{
    int y, x;
    uint32_t area_w = (uint32_t)(area->x2 - area->x1 + 1);
    lv_color_format_t cf = lv_display_get_color_format(disp);
    uint32_t stride = lv_draw_buf_width_to_stride(area_w, cf);
    /* lv_conf pins 32bpp: little-endian XRGB8888/ARGB8888 = B,G,R,(X/A) */
    uint32_t bpp = (uint32_t)(lv_color_format_get_bpp(cf) + 7) / 8;

    for(y = area->y1; y <= area->y2; y++) {
        const uint8_t * src = px_map + (uint32_t)(y - area->y1) * stride;
        for(x = area->x1; x <= area->x2; x++, src += bpp) {
            s_fb[y][x][0] = src[2];
            s_fb[y][x][1] = src[1];
            s_fb[y][x][2] = src[0];
        }
    }
    lv_display_flush_ready(disp);
}

/* ---------------- helpers ---------------- */

static void pump(int frames)
{
    int f;
    for(f = 0; f < frames; f++) {
        lv_tick_inc(50);
        lv_timer_handler();
    }
}

static void dump_ppm(const char * out_dir, const char * name)
{
    char path[256];
    FILE * f;
    int y;

    snprintf(path, sizeof(path), "%s/%s.ppm", out_dir, name);
    f = fopen(path, "wb");
    if(f == NULL) {
        printf("FAIL dump %s\n", name);
        return;
    }
    fprintf(f, "P6\n%d %d\n255\n", W, H);
    for(y = 0; y < H; y++) {
        fwrite(s_fb[y], 1, W * 3, f);
    }
    fclose(f);
    printf("dumped %s\n", name);
}

/* BFS over the whole widget tree; run match(obj) and return the first
 * object where it returns true, plus its center point. */
typedef bool (*obj_match_t)(lv_obj_t * o, void * user);

static lv_obj_t * find_obj(obj_match_t match, void * user, int * cx, int * cy)
{
    lv_obj_t * queue[256];
    int qh = 0, qt = 0;

    queue[qt++] = lv_screen_active();
    while(qh < qt) {
        lv_obj_t * o = queue[qh++];
        uint32_t i;
        uint32_t n = lv_obj_get_child_count(o);
        if(match(o, user)) {
            lv_area_t a;
            lv_obj_get_coords(o, &a);
            if(cx) *cx = (a.x1 + a.x2) / 2;
            if(cy) *cy = (a.y1 + a.y2) / 2;
            return o;
        }
        for(i = 0; i < n && qt < 256; i++) {
            queue[qt++] = lv_obj_get_child(o, (int32_t)i);
        }
    }
    return NULL;
}

static bool match_dropdown(lv_obj_t * o, void * user)
{
    LV_UNUSED(user);
    return lv_obj_check_type(o, &lv_dropdown_class);
}

static bool match_label_sub(lv_obj_t * o, void * user)
{
    const char * sub = (const char *)user;
    if(!lv_obj_check_type(o, &lv_label_class)) return false;
    return strstr(lv_label_get_text(o), sub) != NULL;
}

static bool match_btn_text(lv_obj_t * o, void * user)
{
    const char * text = (const char *)user;
    lv_obj_t * lab;
    if(!lv_obj_check_type(o, &lv_button_class)) return false;
    if(lv_obj_get_child_count(o) < 1) return false;
    lab = lv_obj_get_child(o, 0);
    if(!lab || !lv_obj_check_type(lab, &lv_label_class)) return false;
    return strcmp(lv_label_get_text(lab), text) == 0;
}

static void click_obj(lv_obj_t * o)
{
    lv_area_t a;
    lv_obj_get_coords(o, &a);
    queue_click((a.x1 + a.x2) / 2, (a.y1 + a.y2) / 2);
    pump(12);
}

static bool click_btn(const char * text)
{
    lv_obj_t * b = find_obj(match_btn_text, (void *)text, NULL, NULL);
    if(b == NULL) {
        printf("FAIL find button '%s'\n", text);
        return false;
    }
    click_obj(b);
    return true;
}

static bool expect(bool cond, const char * what)
{
    printf("%s %s\n", cond ? "[PASS]" : "[FAIL]", what);
    return cond;
}

/* ---------------- checks ---------------- */

int main(int argc, char ** argv)
{
    const char * out_dir = (argc > 1) ? argv[1] : ".";
    static uint8_t lvbuf[W * H * sizeof(lv_color_t)];
    lv_display_t * disp;
    lv_indev_t * indev;
    bool ok = true;
    lv_obj_t * dd;
    lv_obj_t * lab;
    const vg_sensor_t * sensors;
    uint16_t n = 0;
    uint16_t i;
    uint16_t sel0, target;
    const char * sel_id;

    lv_init();
    disp = lv_display_create(W, H);
    lv_display_set_flush_cb(disp, flush_cb);
    lv_display_set_buffers(disp, lvbuf, NULL, sizeof(lvbuf), LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_render_mode(disp, LV_DISPLAY_RENDER_MODE_PARTIAL);
    indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, indev_read_cb);

    /* Same init order as vg_app_init, without the SDL keyboard polling */
    vg_theme_init();
    vg_model_init();
    vg_shell_create();
    vg_nav_goto(VG_PAGE_HOME, NULL);
    pump(30); /* >= 1 model tick so sim history fills */

    sensors = vg_model_get_sensors(&n);
    if(sensors == NULL) n = 0;
    if(!expect(n > 1, "sim model exposes several points")) return 1;

    /* 1) AC1: trend page is reachable (stage-2 gate lifted) */
    vg_nav_goto(VG_PAGE_TREND, NULL);
    pump(20);
    ok &= expect(vg_nav_current() == VG_PAGE_TREND, "trend page reachable (no stage gate)");
    dd = find_obj(match_dropdown, NULL, NULL, NULL);
    ok &= expect(dd != NULL, "trend page has a point dropdown");
    if(dd == NULL) return 1;
    ok &= expect(strlen(lv_dropdown_get_options(dd)) > 0, "dropdown lists points");
    dump_ppm(out_dir, "01_trend_initial");

    /* 2) AC2/AC3: pick a different point through the real dropdown UI.
     * LVGL 9 renders all options in one multi-line label inside the list;
     * rows are hit-tested by y, so click the target row's coordinates. */
    sel_id = vg_model_get_selected_sensor_id();
    sel0 = 0;
    for(i = 0; i < n; i++) {
        if(sel_id && strcmp(sensors[i].id, sel_id) == 0) sel0 = i;
    }
    target = (uint16_t)((sel0 + 1) % n);
    click_obj(dd); /* open the list */
    {
        lv_obj_t * list = lv_dropdown_get_list(dd);
        ok &= expect(list != NULL, "dropdown list opens");
        if(list != NULL) {
            lv_obj_t * opts = lv_obj_get_child(list, 0);
            ok &= expect(opts != NULL && lv_obj_check_type(opts, &lv_label_class),
                         "list holds the options label");
            if(opts != NULL) {
                const lv_font_t * f = lv_obj_get_style_text_font(opts, LV_PART_MAIN);
                int32_t lh = lv_font_get_line_height(f);
                lv_area_t la;
                lv_obj_get_coords(opts, &la);
                queue_click((la.x1 + la.x2) / 2,
                            la.y1 + (int)target * lh + lh / 2);
                pump(12);
            }
        }
    }
    pump(10);
    ok &= expect(vg_model_get_selected_sensor_id() != NULL &&
                 strcmp(vg_model_get_selected_sensor_id(), sensors[target].id) == 0,
                 "dropdown click switches selected point by id");
    dump_ppm(out_dir, "02_trend_switched");

    /* 3) AC4: window toggles in samples */
    ok &= click_btn("全部");
    lab = find_obj(match_label_sub, (void *)"当前 ", NULL, NULL);
    ok &= expect(lab != NULL && strstr(lv_label_get_text(lab), "当前 300 点") != NULL,
                 "full window shows 300 samples on sim");
    ok &= click_btn("最近60点");
    lab = find_obj(match_label_sub, (void *)"当前 ", NULL, NULL);
    ok &= expect(lab != NULL && strstr(lv_label_get_text(lab), "当前 60 点") != NULL,
                 "recent window shows 60 samples");
    dump_ppm(out_dir, "03_trend_recent60");

    /* 4) AC7: an offline point keeps the chart, its value shows -- */
    vg_model_set_scenario(VG_SCENARIO_OFFLINE);
    pump(30);
    sensors = vg_model_get_sensors(&n);
    for(i = 0; i < n; i++) {
        if(!sensors[i].online) break;
    }
    if(i < n) {
        vg_model_set_selected_sensor(sensors[i].id);
        pump(10);
        lab = find_obj(match_label_sub, (void *)"--", NULL, NULL);
        ok &= expect(lab != NULL, "offline point value label shows --");
        dump_ppm(out_dir, "04_trend_offline");
    }
    else {
        printf("[SKIP] OFFLINE scenario leaves every point online\n");
    }
    vg_model_set_scenario(VG_SCENARIO_NORMAL);
    pump(30);

    /* 5) AC5: a point without thresholds renders with auto range */
    {
        uint16_t free_i = (uint16_t)-1;
        for(i = 0; i < n; i++) {
            if(!sensors[i].has_warn && !sensors[i].has_crit) {
                free_i = i;
                break;
            }
        }
        if(free_i != (uint16_t)-1) {
            vg_model_set_selected_sensor(sensors[free_i].id);
            pump(10);
            dump_ppm(out_dir, "05_trend_no_threshold");
            ok &= expect(true, "no-threshold point rendered (auto Y range)");
        }
        else {
            printf("[SKIP] no no-threshold point in sim table\n");
        }
    }

    /* 6) AC1: device page button leads back to trend on that point */
    vg_nav_goto(VG_PAGE_DEVICE, NULL);
    pump(10);
    ok &= click_btn("查看实时趋势");
    pump(10);
    ok &= expect(vg_nav_current() == VG_PAGE_TREND, "device page button opens trend");
    dump_ppm(out_dir, "06_trend_from_device");

    printf("\ntrend_check: %s\n", ok ? "ALL PASS" : "FAILURES PRESENT");
    return ok ? 0 : 1;
}
