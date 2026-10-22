#include "vg_app.h"
#include "theme/vg_theme.h"
#include "model/vg_model.h"
#include "shell/vg_shell.h"
#include "lvgl/lvgl.h"
#ifdef VG_VOICE_ROUND
#include "widgets/vg_voice_overlay.h"
#endif
#ifndef VG_HMI_BOARD
#include <SDL2/SDL.h>
#endif
#include <string.h>

#ifndef VG_HMI_BOARD
static uint8_t s_prev_keys[7];

static bool text_input_focused(void)
{
    lv_group_t * g = lv_group_get_default();
    lv_obj_t * o = g ? lv_group_get_focused(g) : NULL;
    return o != NULL && lv_obj_check_type(o, &lv_textarea_class);
}

static void scenario_key_poll(lv_timer_t * t)
{
    const Uint8 * st;
    int i;
    LV_UNUSED(t);
    st = SDL_GetKeyboardState(NULL);
    if(st == NULL) return;

    /* While typing in a textarea, digits are input, not scenario keys. */
    if(text_input_focused()) {
        for(i = 1; i <= 6; i++) {
            int sc = SDL_SCANCODE_1 + (i - 1);
            s_prev_keys[i] = st[sc] ? 1 : 0;
        }
        s_prev_keys[0] = st[SDL_SCANCODE_ESCAPE] ? 1 : 0;
        return;
    }

    for(i = 1; i <= 6; i++) {
        int sc = SDL_SCANCODE_1 + (i - 1);
        uint8_t down = st[sc] ? 1 : 0;
        if(down && !s_prev_keys[i]) {
            vg_model_set_scenario((vg_scenario_t)i);
        }
        s_prev_keys[i] = down;
    }

    if(st[SDL_SCANCODE_ESCAPE] && !s_prev_keys[0]) {
        vg_nav_back();
    }
    s_prev_keys[0] = st[SDL_SCANCODE_ESCAPE] ? 1 : 0;
}
#endif /* !VG_HMI_BOARD */

static void on_key(lv_event_t * e)
{
    uint32_t key;
    if(lv_event_get_code(e) != LV_EVENT_KEY) return;
    key = lv_event_get_key(e);

    if(key == LV_KEY_ESC) {
        vg_nav_back();
        return;
    }
    if(key >= '1' && key <= '6') {
        vg_model_set_scenario((vg_scenario_t)(key - '0'));
    }
}

void vg_app_init(void)
{
    lv_group_t * g;
    lv_indev_t * indev;
#ifndef VG_HMI_BOARD
    int i;

    for(i = 0; i < 7; i++) s_prev_keys[i] = 0;
#endif

    vg_theme_init();
    vg_model_init();
    vg_shell_create();
    vg_nav_goto(VG_PAGE_HOME, NULL);
#ifdef VG_VOICE_ROUND
    /* Lives on lv_layer_top(), so it stays across page switches. */
    vg_voice_overlay_create();
#endif

    g = lv_group_get_default();
    if(g == NULL) {
        g = lv_group_create();
        lv_group_set_default(g);
    }
    lv_group_add_obj(g, lv_screen_active());
    lv_obj_add_event_cb(lv_screen_active(), on_key, LV_EVENT_KEY, NULL);

    indev = lv_indev_get_next(NULL);
    while(indev) {
        if(lv_indev_get_type(indev) == LV_INDEV_TYPE_KEYPAD) {
            lv_indev_set_group(indev, g);
        }
        indev = lv_indev_get_next(indev);
    }
    lv_group_focus_obj(lv_screen_active());

#ifndef VG_HMI_BOARD
    lv_timer_create(scenario_key_poll, 50, NULL);
#endif
}

void vg_app_boot(int scenario, const char * page)
{
    if(scenario >= 1 && scenario <= 6) {
        vg_model_set_scenario((vg_scenario_t)scenario);
    }
    if(page == NULL || page[0] == '\0' || strcmp(page, "home") == 0) {
        return;
    }
    if(strcmp(page, "alarm") == 0) {
        vg_nav_goto(VG_PAGE_ALARM, NULL);
    }
    else if(strcmp(page, "diagnosis") == 0 || strcmp(page, "diag") == 0) {
        vg_nav_goto(VG_PAGE_DIAGNOSIS, NULL);
    }
    else if(strcmp(page, "logs") == 0 || strcmp(page, "log") == 0) {
        vg_nav_goto(VG_PAGE_LOGS, NULL);
    }
    else if(strcmp(page, "device") == 0) {
        vg_nav_goto(VG_PAGE_DEVICE, NULL);
    }
    else if(strcmp(page, "trend") == 0) {
        vg_nav_goto(VG_PAGE_TREND, NULL);
    }
    else if(strcmp(page, "add_sensor") == 0 || strcmp(page, "add") == 0) {
        vg_nav_goto(VG_PAGE_ADD_SENSOR, NULL);
    }
    else if(strcmp(page, "system") == 0 || strcmp(page, "sys") == 0) {
        vg_nav_goto(VG_PAGE_SYSTEM, NULL);
    }
    else if(strcmp(page, "ota") == 0) {
        vg_nav_goto(VG_PAGE_OTA, NULL);
    }
    else if(strcmp(page, "report") == 0) {
        vg_nav_goto(VG_PAGE_REPORT, NULL);
    }
    else if(strcmp(page, "discover") == 0 || strcmp(page, "scan") == 0) {
        vg_nav_goto(VG_PAGE_DISCOVER, NULL);
    }
}
