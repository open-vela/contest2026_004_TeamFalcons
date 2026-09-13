/* Shared headless harness implementation. See harness_common.h. */

#include "harness_common.h"
#include "theme/vg_theme.h"
#include "model/vg_model.h"
#include "shell/vg_shell.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t * s_fb;   /* HG_H rows of HG_W RGB triples */
static uint8_t * s_lvbuf;/* display draw buffer, sized by format + stride */
static int s_failures;

/* ---------------- scripted pointer indev ---------------- */

typedef struct {
    int x;
    int y;
    int hold_frames; /* >0 pressed; 0 = release at this point */
} hg_step_t;

static hg_step_t s_steps[48];
static int s_step_n;
static int s_step_i;
static int s_hold_left;
static bool s_pressed;
static int s_cur_x;
static int s_cur_y;

static void hg_push_step(int x, int y, int hold_frames)
{
    if(s_step_n >= 48) return;
    s_steps[s_step_n].x = x;
    s_steps[s_step_n].y = y;
    s_steps[s_step_n].hold_frames = hold_frames;
    s_step_n++;
}

void hg_queue_click(int x, int y)
{
    hg_push_step(x, y, 3);
    hg_push_step(x, y, 0);
}

void hg_queue_drag(int x0, int y0, int x1, int y1, int steps)
{
    int i;
    int n = steps;

    if(n < 2) n = 2;
    hg_push_step(x0, y0, 2);
    for(i = 1; i <= n; i++) {
        int x = x0 + (x1 - x0) * i / n;
        int y = y0 + (y1 - y0) * i / n;
        hg_push_step(x, y, 1);
    }
    hg_push_step(x1, y1, 0);
}

void hg_queue_press_hold(int x, int y, int hold_frames)
{
    int hold = hold_frames;

    if(hold < 1) hold = 1;
    hg_push_step(x, y, hold);
    hg_push_step(x, y, 0);
}

static void indev_read_cb(lv_indev_t * indev, lv_indev_data_t * data)
{
    LV_UNUSED(indev);

    if(!s_pressed && s_step_i < s_step_n) {
        s_cur_x = s_steps[s_step_i].x;
        s_cur_y = s_steps[s_step_i].y;
        if(s_steps[s_step_i].hold_frames > 0) {
            s_pressed = true;
            s_hold_left = s_steps[s_step_i].hold_frames;
        }
        else {
            /* Explicit release step. */
            s_pressed = false;
            s_step_i++;
        }
    }

    if(s_pressed) {
        data->point.x = s_cur_x;
        data->point.y = s_cur_y;
        data->state = LV_INDEV_STATE_PRESSED;
        if(--s_hold_left <= 0) {
            s_step_i++;
            if(s_step_i < s_step_n && s_steps[s_step_i].hold_frames > 0) {
                s_cur_x = s_steps[s_step_i].x;
                s_cur_y = s_steps[s_step_i].y;
                s_hold_left = s_steps[s_step_i].hold_frames;
            }
            else if(s_step_i < s_step_n && s_steps[s_step_i].hold_frames == 0) {
                s_pressed = false;
                s_step_i++;
            }
            else {
                s_pressed = false;
            }
        }
    }
    else {
        data->state = LV_INDEV_STATE_RELEASED;
        data->point.x = s_cur_x;
        data->point.y = s_cur_y;
    }
}

/* ---------------- display flush: format-aware compose ---------------- */

static void flush_cb(lv_display_t * disp, const lv_area_t * area, uint8_t * px_map)
{
    int y, x;
    uint32_t area_w = (uint32_t)(area->x2 - area->x1 + 1);
    lv_color_format_t cf = lv_display_get_color_format(disp);
    uint32_t stride = lv_draw_buf_width_to_stride(area_w, cf);
    bool is565 = (cf == LV_COLOR_FORMAT_RGB565);
    uint32_t bpp = (uint32_t)(lv_color_format_get_bpp(cf) + 7) / 8;

    for(y = area->y1; y <= area->y2; y++) {
        const uint8_t * src = px_map + (uint32_t)(y - area->y1) * stride;
        for(x = area->x1; x <= area->x2; x++, src += bpp) {
            uint8_t r, g, b;
            if(is565) {
                /* little-endian RGB565: rescale 5/6-bit fields to 8 bit */
                uint16_t v = (uint16_t)(src[0] | ((uint16_t)src[1] << 8));
                r = (uint8_t)((((v >> 11) & 0x1F) * 255u + 15u) / 31u);
                g = (uint8_t)((((v >> 5) & 0x3F) * 255u + 31u) / 63u);
                b = (uint8_t)(((v & 0x1F) * 255u + 15u) / 31u);
            }
            else {
                /* little-endian XRGB8888/ARGB8888 = B,G,R,(X/A) */
                r = src[2];
                g = src[1];
                b = src[0];
            }
            s_fb[((uint32_t)y * HG_W + (uint32_t)x) * 3 + 0] = r;
            s_fb[((uint32_t)y * HG_W + (uint32_t)x) * 3 + 1] = g;
            s_fb[((uint32_t)y * HG_W + (uint32_t)x) * 3 + 2] = b;
        }
    }
    lv_display_flush_ready(disp);
}

/* ---------------- lifecycle ---------------- */

void hg_init(void)
{
    lv_display_t * disp;
    lv_indev_t * indev;
    lv_color_format_t cf;
    uint32_t stride;

    lv_init();
    disp = lv_display_create(HG_W, HG_H);
    lv_display_set_flush_cb(disp, flush_cb);

    cf = lv_display_get_color_format(disp);
    stride = lv_draw_buf_width_to_stride(HG_W, cf);
    /* Full-screen partial buffer sized by the real format: never
     * sizeof(lv_color_t). */
    s_lvbuf = malloc((size_t)stride * HG_H);
    if(s_lvbuf == NULL) {
        fprintf(stderr, "harness: display buffer alloc failed\n");
        exit(2);
    }
    lv_display_set_buffers(disp, s_lvbuf, NULL, (size_t)stride * HG_H,
                           LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_render_mode(disp, LV_DISPLAY_RENDER_MODE_PARTIAL);

    indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, indev_read_cb);

    s_fb = malloc((size_t)HG_W * HG_H * 3);
    if(s_fb == NULL) {
        fprintf(stderr, "harness: framebuffer alloc failed\n");
        exit(2);
    }
    memset(s_fb, 0, (size_t)HG_W * HG_H * 3);

    vg_theme_init();
    vg_model_init();
    vg_shell_create();
    vg_nav_goto(VG_PAGE_HOME, NULL);
}

void hg_pump(int frames)
{
    int f;
    for(f = 0; f < frames; f++) {
        lv_tick_inc(50);
        lv_timer_handler();
    }
}

void hg_dump_ppm(const char * out_dir, const char * name)
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
    fprintf(f, "P6\n%d %d\n255\n", HG_W, HG_H);
    for(y = 0; y < HG_H; y++) {
        fwrite(s_fb + (size_t)y * HG_W * 3, 1, HG_W * 3, f);
    }
    fclose(f);
    printf("dumped %s\n", name);
}

bool hg_expect_not_blank(const char * what)
{
    /* Histogram by exact RGB triple: a rendered page has text and widgets
     * in many colors; a blank/failed compose is essentially one color. */
    static uint32_t hist[4096]; /* 12-bit key: (r>>4)<<8 | (g>>4)<<4 | (b>>4) */
    uint32_t total = (uint32_t)HG_W * HG_H;
    uint32_t maxc = 0;
    uint32_t i;

    memset(hist, 0, sizeof(hist));
    for(i = 0; i < total; i++) {
        uint8_t r = s_fb[i * 3 + 0], g = s_fb[i * 3 + 1], b = s_fb[i * 3 + 2];
        uint32_t key = ((uint32_t)r >> 4) << 8 | ((uint32_t)g >> 4) << 4 |
                       ((uint32_t)b >> 4);
        hist[key]++;
    }
    for(i = 0; i < 4096; i++) {
        if(hist[i] > maxc) maxc = hist[i];
    }
    /* > 5% pixels differing from the dominant color = content visible.
     * RGB565 quantization keeps this far above the threshold for text. */
    return hg_expect((total - maxc) * 20u > total, what);
}

/* ---------------- search + click ---------------- */

lv_obj_t * hg_find_obj(bool (*match)(lv_obj_t * o, void * user), void * user,
                       int * cx, int * cy)
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

bool hg_match_dropdown(lv_obj_t * o, void * user)
{
    LV_UNUSED(user);
    return lv_obj_check_type(o, &lv_dropdown_class);
}

bool hg_match_label_sub(lv_obj_t * o, void * user)
{
    const char * sub = (const char *)user;
    if(!lv_obj_check_type(o, &lv_label_class)) return false;
    return strstr(lv_label_get_text(o), sub) != NULL;
}

bool hg_match_btn_text(lv_obj_t * o, void * user)
{
    const char * text = (const char *)user;
    lv_obj_t * lab;
    if(!lv_obj_check_type(o, &lv_button_class)) return false;
    if(lv_obj_get_child_count(o) < 1) return false;
    lab = lv_obj_get_child(o, 0);
    if(!lab || !lv_obj_check_type(lab, &lv_label_class)) return false;
    return strcmp(lv_label_get_text(lab), text) == 0;
}

/* Find the nth button whose label text matches, return its center */
bool hg_find_btn_center(const char * text, int nth, int * cx, int * cy)
{
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
 * row card and return a point on the card's left half so the click selects
 * the row instead of pressing the button. */
bool hg_find_row_click_point(const char * text, int nth, int * cx, int * cy)
{
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

bool hg_click_obj_center(lv_obj_t * o)
{
    lv_area_t a;
    if(o == NULL) return false;
    lv_obj_get_coords(o, &a);
    hg_queue_click((a.x1 + a.x2) / 2, (a.y1 + a.y2) / 2);
    hg_pump(12);
    return true;
}

bool hg_click_nth_btn(const char * text, int nth)
{
    int cx, cy;
    if(!hg_find_btn_center(text, nth, &cx, &cy)) {
        printf("FAIL find button '%s' #%d\n", text, nth);
        return false;
    }
    hg_queue_click(cx, cy);
    hg_pump(12);
    return true;
}

bool hg_click_btn(const char * text)
{
    int cx, cy;
    if(!hg_find_btn_center(text, 0, &cx, &cy)) {
        printf("FAIL find button '%s'\n", text);
        return false;
    }
    hg_queue_click(cx, cy);
    hg_pump(12);
    return true;
}

bool hg_click_row(const char * text, int nth)
{
    int cx, cy;
    if(!hg_find_row_click_point(text, nth, &cx, &cy)) {
        printf("FAIL find row for '%s' #%d\n", text, nth);
        return false;
    }
    hg_queue_click(cx, cy);
    hg_pump(12);
    return true;
}

/* ---------------- reporting ---------------- */

bool hg_expect(bool cond, const char * what)
{
    printf("%s %s\n", cond ? "[PASS]" : "[FAIL]", what);
    if(!cond) s_failures++;
    return cond;
}

int hg_failures(void)
{
    return s_failures;
}
