/* Headless render check for the multi-row alarm page.
 *
 * Builds the LVGL UI into a 480x272 off-screen buffer (no SDL, no board),
 * drives the real pointer indev with scripted clicks located by widget
 * text, dumps frames as PPM for visual inspection, and prints model-level
 * PASS/FAIL lines. Not compiled into firmware (app/velaguard Makefile) or
 * the SDL simulator (gui/CMakeLists.txt globs main/ui only).
 *
 * Usage: alarm_check <out-dir>
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

/* Find the nth button whose label text matches, return its center */
static bool find_btn_center(const char * text, int nth, int * cx, int * cy)
{
    /* BFS over the whole widget tree */
    lv_obj_t * queue[256];
    int qh = 0, qt = 0;
    int counter = 0;

    queue[qt++] = lv_screen_active();
    while(qh < qt) {
        lv_obj_t * o = queue[qh++];
        uint32_t i;
        uint32_t n = lv_obj_get_child_count(o);
        if(lv_obj_check_type(o, &lv_button_class) && n > 0) {
            lv_obj_t * lab = lv_obj_get_child(o, 0);
            if(lab && lv_obj_check_type(lab, &lv_label_class) &&
               strcmp(lv_label_get_text(lab), text) == 0) {
                if(counter == nth) {
                    lv_area_t a;
                    lv_obj_get_coords(o, &a);
                    *cx = (a.x1 + a.x2) / 2;
                    *cy = (a.y1 + a.y2) / 2;
                    return true;
                }
                counter++;
            }
        }
        for(i = 0; i < n && qt < 256; i++) {
            queue[qt++] = lv_obj_get_child(o, (int32_t)i);
        }
    }
    return false;
}

/* Find the nth button with the given label text, then climb to its alarm
 * row card (btn -> line1 -> row) and return a point on the card's left
 * half so the click selects the row instead of pressing the button. */
static bool find_row_click_point(const char * text, int nth, int * cx, int * cy)
{
    /* BFS over the whole widget tree */
    lv_obj_t * queue[256];
    int qh = 0, qt = 0;
    int counter = 0;

    queue[qt++] = lv_screen_active();
    while(qh < qt) {
        lv_obj_t * o = queue[qh++];
        uint32_t i;
        uint32_t n = lv_obj_get_child_count(o);
        if(lv_obj_check_type(o, &lv_button_class) && n > 0) {
            lv_obj_t * lab = lv_obj_get_child(o, 0);
            if(lab && lv_obj_check_type(lab, &lv_label_class) &&
               strcmp(lv_label_get_text(lab), text) == 0) {
                if(counter == nth) {
                    lv_obj_t * row = lv_obj_get_parent(lv_obj_get_parent(o));
                    lv_area_t a;
                    lv_obj_get_coords(row, &a);
                    *cx = a.x1 + 50;
                    *cy = (a.y1 + a.y2) / 2;
                    printf("row-click point (%d,%d) for '%s' #%d\n", *cx, *cy, text, nth);
                    return true;
                }
                counter++;
            }
        }
        for(i = 0; i < n && qt < 256; i++) {
            queue[qt++] = lv_obj_get_child(o, (int32_t)i);
        }
    }
    return false;
}

static bool click_row(const char * text, int nth)
{
    int cx, cy;
    if(!find_row_click_point(text, nth, &cx, &cy)) {
        printf("FAIL find row for '%s' #%d\n", text, nth);
        return false;
    }
    queue_click(cx, cy);
    pump(12);
    return true;
}

static bool click_nth_btn(const char * text, int nth)
{
    int cx, cy;
    if(!find_btn_center(text, nth, &cx, &cy)) {
        printf("FAIL find button '%s' #%d\n", text, nth);
        return false;
    }
    queue_click(cx, cy);
    pump(12);
    return true;
}

static void print_alarms(void)
{
    vg_alarm_t list[16];
    uint16_t n = vg_model_collect_alarms(list, 16);
    uint16_t i;
    printf("alarms: total=%u quieted=%d\n", (unsigned)vg_model_active_alarm_count(),
           vg_model_alarms_all_quieted() ? 1 : 0);
    for(i = 0; i < n; i++) {
        printf("  [%u] id=%s sev=%d acked=%d muted=%d dur=%d\n", (unsigned)i,
               list[i].sensor_id, (int)list[i].severity, list[i].acked ? 1 : 0,
               list[i].muted ? 1 : 0, (int)list[i].duration_sec);
    }
}

static bool expect(bool cond, const char * what)
{
    printf("%s %s\n", cond ? "[PASS]" : "[FAIL]", what);
    return cond;
}

int main(int argc, char ** argv)
{
    const char * out_dir = (argc > 1) ? argv[1] : ".";
    static uint8_t lvbuf[W * H * sizeof(lv_color_t)];
    lv_display_t * disp;
    lv_indev_t * indev;
    bool ok = true;
    vg_alarm_t list[16];
    uint16_t n;

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

    /* 1) CRIT scenario seeds 3 episodes: primary crit + s9 crit + s3 warn */
    vg_model_set_scenario(VG_SCENARIO_CRIT);
    vg_nav_goto(VG_PAGE_ALARM, NULL);
    pump(40); /* >= 2 model ticks so durations advance */
    dump_ppm(out_dir, "01_alarm_crit_multi");
    print_alarms();
    n = vg_model_collect_alarms(list, 16);
    ok &= expect(n == 3, "crit scenario yields 3 alarm rows");
    ok &= expect(list[0].severity == VG_SEV_CRIT, "rows sorted: primary crit first");
    ok &= expect(list[2].severity == VG_SEV_WARN, "rows sorted: warn last");

    /* 2) ack + mute row 2 (2nd crit) via its real buttons */
    ok &= click_nth_btn("标记处理", 1);
    ok &= click_nth_btn("静音", 1);
    dump_ppm(out_dir, "02_alarm_row2_quieted");
    print_alarms();
    ok &= expect(vg_model_collect_alarms(list, 16) == 3, "ack/mute keeps episodes alive");
    ok &= expect(list[1].acked && list[1].muted, "row2 acked+muted");
    ok &= expect(!list[0].acked && !list[0].muted, "row1 untouched");
    ok &= expect(!vg_model_alarms_all_quieted(), "not all quieted (row3 active)");

    /* 3) quiet the rest FIRST (rows still in view): row1 mute, row3 ack
     *    -> all quieted, status chip would dim */
    ok &= click_nth_btn("静音", 0);
    ok &= click_nth_btn("标记处理", 2);
    dump_ppm(out_dir, "03_alarm_all_quieted");
    print_alarms();
    ok &= expect(vg_model_alarms_all_quieted(), "all alarms quieted after per-row actions");

    /* 4) select row 2 LAST: tapping its card scrolls the detail of that
     *    row into view (rows scroll out, so do this after all clicks) */
    ok &= click_row("标记处理", 1);
    pump(30);
    dump_ppm(out_dir, "04_alarm_select_row2");

    /* 5) empty state */
    vg_model_set_scenario(VG_SCENARIO_NORMAL);
    pump(30);
    dump_ppm(out_dir, "05_alarm_empty");
    print_alarms();
    ok &= expect(vg_model_active_alarm_count() == 0, "normal scenario clears all episodes");

    /* 6) WARN scenario: 3 warn rows, rebuild from empty */
    vg_model_set_scenario(VG_SCENARIO_WARN);
    pump(30);
    dump_ppm(out_dir, "06_alarm_warn_multi");
    print_alarms();
    n = vg_model_collect_alarms(list, 16);
    ok &= expect(n == 3, "warn scenario yields 3 alarm rows");

    printf("\nalarm_check: %s\n", ok ? "ALL PASS" : "FAILURES PRESENT");
    return ok ? 0 : 1;
}
