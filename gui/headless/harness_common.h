/* Shared headless harness for the VelaGuard HMI checks
 * (task 09-13-hmi-performance-baseline).
 *
 * One 480x272 off-screen display whose buffer is allocated and decoded by
 * the display's actual color format and stride (RGB565 or XRGB8888 — never
 * sizeof(lv_color_t)), a scripted pointer indev, PPM frame dumps and
 * model-level PASS/FAIL helpers. The alarm/trend/fixture checks build on
 * this instead of private copies.
 *
 * Determinism: time advances only via hg_pump()'s lv_tick_inc; no wall
 * clock, no board config reads.
 */

#ifndef VG_HEADLESS_HARNESS_H
#define VG_HEADLESS_HARNESS_H

#include "lvgl/lvgl.h"
#include <stdbool.h>

#define HG_W 480
#define HG_H 272

/* lv_init + off-screen display + pointer indev + theme/model/shell +
 * home page (same init order as vg_app_init, minus SDL keyboard polling). */
void hg_init(void);

/* Advance LVGL time and run the timer handler `frames` times. */
void hg_pump(int frames);

/* Dump the composed framebuffer as P6 PPM into out_dir. */
void hg_dump_ppm(const char * out_dir, const char * name);

/* Framebuffer sanity: enough non-background pixels that the scene is not
 * blank and text/controls are visible. Fails via hg_expect. */
bool hg_expect_not_blank(const char * what);

/* Scripted pointer input: press at (x,y) held 3 pump frames, then release. */
void hg_queue_click(int x, int y);

/* Scripted vertical/horizontal drag: press at start, move across `steps`
 * intermediate points, then release. Used to verify scroll cancels click. */
void hg_queue_drag(int x0, int y0, int x1, int y1, int steps);

/* Press and hold for `hold_frames` pump reads, then release. Useful for
 * mid-press navigation (I16 wait_release) without finishing a click. */
void hg_queue_press_hold(int x, int y, int hold_frames);

/* Widget search + click helpers driving the real pointer indev. */
lv_obj_t * hg_find_obj(bool (*match)(lv_obj_t * o, void * user), void * user,
                       int * cx, int * cy);
bool hg_find_btn_center(const char * text, int nth, int * cx, int * cy);
bool hg_find_row_click_point(const char * text, int nth, int * cx, int * cy);
bool hg_click_nth_btn(const char * text, int nth);
bool hg_click_btn(const char * text); /* first match */
bool hg_click_row(const char * text, int nth);
bool hg_click_obj_center(lv_obj_t * o);

bool hg_expect(bool cond, const char * what);
int hg_failures(void);

/* Common matchers. */
bool hg_match_dropdown(lv_obj_t * o, void * user);
bool hg_match_label_sub(lv_obj_t * o, void * user);
bool hg_match_btn_text(lv_obj_t * o, void * user);

#endif /* VG_HEADLESS_HARNESS_H */
