/****************************************************************************
 * gui/main/ui/widgets/vg_voice_overlay.c
 *
 * BVO-12: the cross-page voice panel.
 *
 * It lives on lv_layer_top(), so it is above every page and survives a page
 * switch: the round keeps its deadline and its answer while the user moves
 * between screens.  The panel never drives the pipeline; it polls the round's
 * published snapshot and sends only two commands back (end, retry speech).
 ****************************************************************************/

#include "vg_voice_overlay.h"

/* The panel is part of the board HMI only when the voice round is built.  The
 * simulator compiles the same ui/ tree but has no round, so the body is empty
 * there and CMake also drops the file. */

#ifdef VG_VOICE_ROUND

#include "theme/vg_theme.h"
#include "vg_display.h"
#include "vg_voice_pref.h"
#include "vg_voice_round.h"

#include <pthread.h>
#include <stdio.h>
#include <string.h>

#define VOICE_PANEL_W  (VG_DISP_W - 2 * VG_PAGE_PAD)
#define VOICE_PANEL_MAX_H 170
#define VOICE_ANSWER_MAX_H 92

static lv_obj_t * s_panel;
static lv_obj_t * s_status;
static lv_obj_t * s_question;
static lv_obj_t * s_answer;
static lv_obj_t * s_error;
static lv_obj_t * s_close;
static lv_obj_t * s_mute;
static lv_obj_t * s_mute_label;
static lv_obj_t * s_retry;
static lv_obj_t * s_answer_box;
static lv_timer_t * s_timer;

/* BVO-12 layout snapshot, written from the LVGL thread and copied out by the
 * NSH diagnostic.  Measuring from another thread would race the layout. */

static char s_dbg[512];
static pthread_mutex_t s_dbg_lock = PTHREAD_MUTEX_INITIALIZER;

static void overlay_hide(void)
{
    if(s_panel) {
        lv_obj_add_flag(s_panel, LV_OBJ_FLAG_HIDDEN);
    }
}

static void overlay_show(void)
{
    if(s_panel) {
        lv_obj_remove_flag(s_panel, LV_OBJ_FLAG_HIDDEN);
    }
}

static void close_cb(lv_event_t * e)
{
    LV_UNUSED(e);
    vg_voice_round_end();
}

static void retry_cb(lv_event_t * e)
{
    LV_UNUSED(e);
    vg_voice_round_retry_tts();
}

static void mute_cb(lv_event_t * e)
{
    bool muted;
    LV_UNUSED(e);

    muted = !vg_voice_round_muted();
    vg_voice_pref_set_muted(muted);
}

/* Reading the list must not be cut off after 15 s.  The session restarts the
 * hold timer when the finger leaves. */

static void scroll_cb(lv_event_t * e)
{
    lv_event_code_t code = lv_event_get_code(e);

    if(code == LV_EVENT_SCROLL_BEGIN) {
        vg_voice_round_hold(true);
    }
    else if(code == LV_EVENT_SCROLL_END) {
        vg_voice_round_hold(false);
    }
}

static void status_text(const struct vg_voice_ui * ui, char * buf, size_t cap)
{
    unsigned secs;

    switch(ui->state) {
        case 1: /* VG_VS_RECORDING */
            secs = (ui->record_left_ms + 999) / 1000;
            snprintf(buf, cap, "录音中 · 最长 %u 秒", secs);
            break;
        case 2: /* VG_VS_STOPPING */
            snprintf(buf, cap, "正在停止播放");
            break;
        case 3: /* VG_VS_ASR */
            secs = (ui->budget_left_ms + 999) / 1000;
            snprintf(buf, cap, "识别中 · 剩余 %u 秒", secs);
            break;
        case 4: /* VG_VS_WAIT_AGENT */
        case 5: /* VG_VS_QUERYING */
            secs = (ui->budget_left_ms + 999) / 1000;
            snprintf(buf, cap, "查询中 · 剩余 %u 秒", secs);
            break;
        case 6: /* VG_VS_TTS */
            secs = (ui->budget_left_ms + 999) / 1000;
            snprintf(buf, cap, "合成语音 · 剩余 %u 秒", secs);
            break;
        case 7: /* VG_VS_PLAYING */
            snprintf(buf, cap, "播报中 · 按 USER1 打断");
            break;
        case 8: /* VG_VS_RESULT */
            if(ui->speech_failed) {
                snprintf(buf, cap, "播报失败 · 文字保留");
            }
            else {
                secs = (ui->result_left_ms + 999) / 1000;
                snprintf(buf, cap, "回答 · %u 秒后收起", secs);
            }
            break;
        case 9: /* VG_VS_ERROR */
            snprintf(buf, cap, "未完成");
            break;
        default:
            snprintf(buf, cap, "%s", ui->status);
            break;
    }
}

/* Runs in the LVGL thread.  Reports geometry, the answer scroll range and a
 * hit test on each button center, so a board run can show the panel fits the
 * screen and the controls are reachable without a person looking at it. */

static void overlay_measure(void)
{
    char buf[512];
    lv_area_t a;
    lv_area_t b;
    lv_display_t * disp;
    int n = 0;

    if(s_panel == NULL) {
        n = snprintf(buf, sizeof(buf), "overlay: absent\n");
    }
    else {
        lv_obj_get_coords(s_panel, &a);
        disp = lv_obj_get_display(s_panel);
        n = snprintf(buf, sizeof(buf),
                     "overlay: panel=%dx%d at %d,%d screen=%dx%d visible=%d\n",
                     (int)lv_obj_get_width(s_panel),
                     (int)lv_obj_get_height(s_panel),
                     (int)a.x1, (int)a.y1,
                     (int)lv_display_get_horizontal_resolution(disp),
                     (int)lv_display_get_vertical_resolution(disp),
                     lv_obj_has_flag(s_panel, LV_OBJ_FLAG_HIDDEN) ? 0 : 1);

        if(s_answer_box != NULL && n < (int)sizeof(buf)) {
            n += snprintf(buf + n, sizeof(buf) - n,
                          "overlay: answer_box=h%d scroll_bottom=%d label_h=%d\n",
                          (int)lv_obj_get_height(s_answer_box),
                          (int)lv_obj_get_scroll_bottom(s_answer_box),
                          (int)lv_obj_get_height(s_answer));
        }

        if(s_mute != NULL && n < (int)sizeof(buf)) {
            lv_obj_get_coords(s_mute, &b);
            lv_point_t p = { (b.x1 + b.x2) / 2, (b.y1 + b.y2) / 2 };
            n += snprintf(buf + n, sizeof(buf) - n,
                          "overlay: mute=%dx%d at %d,%d hit=%d\n",
                          (int)lv_obj_get_width(s_mute), (int)lv_obj_get_height(s_mute),
                          (int)b.x1, (int)b.y1, lv_obj_hit_test(s_mute, &p) ? 1 : 0);
        }

        if(s_retry != NULL && n < (int)sizeof(buf)) {
            lv_obj_get_coords(s_retry, &b);
            lv_point_t p = { (b.x1 + b.x2) / 2, (b.y1 + b.y2) / 2 };
            n += snprintf(buf + n, sizeof(buf) - n,
                          "overlay: retry=%dx%d at %d,%d visible=%d hit=%d\n",
                          (int)lv_obj_get_width(s_retry),
                          (int)lv_obj_get_height(s_retry),
                          (int)b.x1, (int)b.y1,
                          lv_obj_has_flag(s_retry, LV_OBJ_FLAG_HIDDEN) ? 0 : 1,
                          lv_obj_hit_test(s_retry, &p) ? 1 : 0);
        }

        if(s_close != NULL && n < (int)sizeof(buf)) {
            lv_obj_get_coords(s_close, &b);
            lv_point_t p = { (b.x1 + b.x2) / 2, (b.y1 + b.y2) / 2 };
            n += snprintf(buf + n, sizeof(buf) - n,
                          "overlay: close=%dx%d at %d,%d hit=%d\n",
                          (int)lv_obj_get_width(s_close), (int)lv_obj_get_height(s_close),
                          (int)b.x1, (int)b.y1, lv_obj_hit_test(s_close, &p) ? 1 : 0);
        }
    }

    pthread_mutex_lock(&s_dbg_lock);
    snprintf(s_dbg, sizeof(s_dbg), "%s", buf);
    pthread_mutex_unlock(&s_dbg_lock);
}

static void poll_cb(lv_timer_t * t)
{
    struct vg_voice_ui ui;
    char status[64];

    LV_UNUSED(t);

    if(!vg_voice_round_ui(&ui) || !ui.visible) {
        overlay_hide();
        pthread_mutex_lock(&s_dbg_lock);
        snprintf(s_dbg, sizeof(s_dbg), "overlay: hidden\n");
        pthread_mutex_unlock(&s_dbg_lock);
        return;
    }

    overlay_show();

    status_text(&ui, status, sizeof(status));
    if(ui.status[0] != '\0' && ui.state != 8 && ui.state != 9) {
        size_t n = strlen(status);
        snprintf(status + n, sizeof(status) - n, " · %s", ui.status);
    }
    lv_label_set_text(s_status, status);

    if(ui.question[0] != '\0') {
        lv_label_set_text(s_question, ui.question);
        lv_obj_remove_flag(s_question, LV_OBJ_FLAG_HIDDEN);
    }
    else {
        lv_obj_add_flag(s_question, LV_OBJ_FLAG_HIDDEN);
    }

    lv_label_set_text(s_answer, ui.answer);

    if(ui.error[0] != '\0') {
        lv_label_set_text(s_error, ui.error);
        lv_obj_remove_flag(s_error, LV_OBJ_FLAG_HIDDEN);
    }
    else {
        lv_obj_add_flag(s_error, LV_OBJ_FLAG_HIDDEN);
    }

    if(ui.speech_failed) {
        lv_obj_remove_flag(s_retry, LV_OBJ_FLAG_HIDDEN);
    }
    else {
        lv_obj_add_flag(s_retry, LV_OBJ_FLAG_HIDDEN);
    }

    /* s_mute is a button, not a label.  Calling lv_label_set_text on it
     * wrote label fields into a button object and asserted on the next
     * invalidation; the text belongs to the label inside the button. */
    if(s_mute_label) {
        lv_label_set_text(s_mute_label, ui.muted ? "播报:关" : "播报:开");

    overlay_measure();
    }
}

void vg_voice_overlay_create(void)
{
    lv_obj_t * top;
    lv_obj_t * row;

    if(s_panel) {
        return;
    }

    top = lv_layer_top();

    s_panel = lv_obj_create(top);
    lv_obj_set_size(s_panel, VOICE_PANEL_W, LV_SIZE_CONTENT);
    lv_obj_set_style_max_height(s_panel, VOICE_PANEL_MAX_H, 0);
    lv_obj_align(s_panel, LV_ALIGN_BOTTOM_MID, 0, -VG_PAGE_PAD);
    lv_obj_set_style_bg_color(s_panel, lv_color_hex(VG_COLOR_SURFACE), 0);
    lv_obj_set_style_bg_opa(s_panel, LV_OPA_90, 0);
    lv_obj_set_style_border_color(s_panel, lv_color_hex(VG_COLOR_ACCENT), 0);
    lv_obj_set_style_border_width(s_panel, 1, 0);
    lv_obj_set_style_radius(s_panel, VG_CARD_RADIUS, 0);
    lv_obj_set_style_pad_all(s_panel, 6, 0);
    lv_obj_set_flex_flow(s_panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(s_panel, 2, 0);
    lv_obj_add_flag(s_panel, LV_OBJ_FLAG_HIDDEN);

    /* Status row: text on the left, buttons on the right. */

    row = lv_obj_create(s_panel);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_height(row, 26);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    s_status = lv_label_create(row);
    lv_obj_set_style_text_color(s_status, lv_color_hex(VG_COLOR_TEXT), 0);
    lv_obj_set_style_text_font(s_status, vg_font_ui(), 0);
    lv_label_set_text(s_status, "");

    {
        lv_obj_t * btns = lv_obj_create(row);
        lv_obj_set_size(btns, LV_SIZE_CONTENT, 26);
        lv_obj_set_style_bg_opa(btns, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(btns, 0, 0);
        lv_obj_set_style_pad_all(btns, 0, 0);
        lv_obj_set_flex_flow(btns, LV_FLEX_FLOW_ROW);
        lv_obj_set_style_pad_column(btns, 4, 0);

        s_mute = lv_button_create(btns);
        lv_obj_set_size(s_mute, 62, 24);
        lv_obj_add_event_cb(s_mute, mute_cb, LV_EVENT_CLICKED, NULL);
        {
            s_mute_label = lv_label_create(s_mute);
            lv_obj_set_style_text_font(s_mute_label, vg_font_ui(), 0);
            lv_label_set_text(s_mute_label, "播报:开");
        }

        s_retry = lv_button_create(btns);
        lv_obj_set_size(s_retry, 62, 24);
        lv_obj_add_flag(s_retry, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_event_cb(s_retry, retry_cb, LV_EVENT_CLICKED, NULL);
        {
            lv_obj_t * lab = lv_label_create(s_retry);
            lv_obj_set_style_text_font(lab, vg_font_ui(), 0);
            lv_label_set_text(lab, "重试播报");
        }

        s_close = lv_button_create(btns);
        lv_obj_set_size(s_close, 30, 24);
        lv_obj_add_event_cb(s_close, close_cb, LV_EVENT_CLICKED, NULL);
        {
            lv_obj_t * lab = lv_label_create(s_close);
            lv_obj_set_style_text_font(lab, vg_font_ui(), 0);
            lv_label_set_text(lab, "×");
        }
    }

    s_question = lv_label_create(s_panel);
    lv_obj_set_width(s_question, LV_PCT(100));
    lv_obj_set_style_text_color(s_question, lv_color_hex(VG_COLOR_MUTED), 0);
    lv_obj_set_style_text_font(s_question, vg_font_ui(), 0);
    lv_label_set_long_mode(s_question, LV_LABEL_LONG_DOT);
    lv_obj_add_flag(s_question, LV_OBJ_FLAG_HIDDEN);

    s_error = lv_label_create(s_panel);
    lv_obj_set_width(s_error, LV_PCT(100));
    lv_obj_set_style_text_color(s_error, lv_color_hex(VG_COLOR_CRIT), 0);
    lv_obj_set_style_text_font(s_error, vg_font_ui(), 0);
    lv_label_set_long_mode(s_error, LV_LABEL_LONG_WRAP);
    lv_obj_add_flag(s_error, LV_OBJ_FLAG_HIDDEN);

    /* The answer scrolls; the panel itself does not move. */

    {
        lv_obj_t * box = lv_obj_create(s_panel);
        lv_obj_set_width(box, LV_PCT(100));
        lv_obj_set_height(box, LV_SIZE_CONTENT);
        lv_obj_set_style_max_height(box, VOICE_ANSWER_MAX_H, 0);
        lv_obj_set_style_bg_opa(box, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(box, 0, 0);
        lv_obj_set_style_pad_all(box, 0, 0);
        lv_obj_set_scroll_dir(box, LV_DIR_VER);
        lv_obj_add_event_cb(box, scroll_cb, LV_EVENT_SCROLL_BEGIN, NULL);
        lv_obj_add_event_cb(box, scroll_cb, LV_EVENT_SCROLL_END, NULL);

        s_answer_box = box;
        s_answer = lv_label_create(box);
        lv_obj_set_width(s_answer, LV_PCT(100));
        lv_obj_set_style_text_color(s_answer, lv_color_hex(VG_COLOR_TEXT), 0);
        lv_obj_set_style_text_font(s_answer, vg_font_ui(), 0);
        lv_label_set_long_mode(s_answer, LV_LABEL_LONG_WRAP);
        lv_label_set_text(s_answer, "");
    }

    s_timer = lv_timer_create(poll_cb, 100, NULL);
}

int vg_voice_overlay_debug(char *out, size_t cap)
{
    if(out == NULL || cap == 0) {
        return 0;
    }

    pthread_mutex_lock(&s_dbg_lock);
    snprintf(out, cap, "%s", s_dbg[0] != '\0' ? s_dbg : "overlay: no snapshot\n");
    pthread_mutex_unlock(&s_dbg_lock);

    return (int)strlen(out);
}

void vg_voice_overlay_destroy(void)
{
    if(s_timer) {
        lv_timer_delete(s_timer);
        s_timer = NULL;
    }
    if(s_panel) {
        lv_obj_delete(s_panel);
        s_panel = NULL;
    }
    s_status = NULL;
    s_question = NULL;
    s_answer = NULL;
    s_error = NULL;
    s_close = NULL;
    s_mute = NULL;
    s_mute_label = NULL;
    s_retry = NULL;
    s_answer_box = NULL;
}

#endif /* VG_VOICE_ROUND */
