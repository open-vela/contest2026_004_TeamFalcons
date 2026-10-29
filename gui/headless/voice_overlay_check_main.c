/* 用最大长度快照运行实际语音浮层，保留真实 LVGL、字体、排版和绘制代码。
 * 语音流水线由测试快照替代。配合 ASan/UBSan 检查越界和对象生命周期，
 * 无法覆盖板端 LTDC、SDRAM 仲裁或 DMA 路径。
 */
#include "harness_common.h"
#include "widgets/vg_voice_overlay.h"
#include "vg_voice_round.h"
#include <stdio.h>
#include <string.h>

static struct vg_voice_ui s_ui;
static unsigned s_end_calls;

bool vg_voice_round_ui(struct vg_voice_ui *out)
{
    *out = s_ui;
    return true;
}

void vg_voice_round_end(void) { s_end_calls++; }
void vg_voice_round_retry_tts(void) { s_ui.speech_failed = false; }
bool vg_voice_round_muted(void) { return s_ui.muted; }
void vg_voice_pref_set_muted(bool muted) { s_ui.muted = muted; }
void vg_voice_round_hold(bool held) { (void)held; }

static lv_obj_t * find_text_button(lv_obj_t * o, const char * text)
{
    uint32_t i;

    if(lv_obj_check_type(o, &lv_button_class) && lv_obj_get_child_count(o) > 0) {
        lv_obj_t * lab = lv_obj_get_child(o, 0);
        if(lab && lv_obj_check_type(lab, &lv_label_class) &&
           strcmp(lv_label_get_text(lab), text) == 0) {
            return o;
        }
    }
    for(i = 0; i < lv_obj_get_child_count(o); i++) {
        lv_obj_t * found = find_text_button(lv_obj_get_child(o, (int32_t)i), text);
        if(found) return found;
    }
    return NULL;
}

/* The panel overlays the page on lv_layer_top(), so the check searches the
 * top layer rather than the active screen.  Each control must lie on the
 * 480x272 screen and keep its label inside the button it belongs to. */
static bool expect_button_geometry(const char * text)
{
    lv_obj_t * btn = find_text_button(lv_layer_top(), text);
    lv_obj_t * lab;
    lv_area_t b;
    lv_area_t l;
    char what[96];
    bool ok = true;

    if(btn == NULL) {
        return hg_expect(false, "overlay button exists");
    }

    lv_obj_get_coords(btn, &b);
    snprintf(what, sizeof(what), "button %s inside screen", text);
    ok &= hg_expect(b.x1 >= 0 && b.y1 >= 0 && b.x2 < HG_W && b.y2 < HG_H, what);

    lab = lv_obj_get_child(btn, 0);
    lv_obj_get_coords(lab, &l);
    snprintf(what, sizeof(what), "label %s inside button", text);
    ok &= hg_expect(l.x1 >= b.x1 && l.x2 <= b.x2 &&
                    l.y1 >= b.y1 && l.y2 <= b.y2, what);
    return ok;
}

static void expect_overlay_buttons(const char * mute_text)
{
    expect_button_geometry(mute_text);
    expect_button_geometry("重试播报");
    expect_button_geometry("×");
}

/* Exercises the real close button through the pointer indev.  The snapshot
 * stays visible after the end request to mimic a worker still winding down;
 * the panel must remain dismissed until the round actually settles. */
static bool expect_close_dismisses(void)
{
    char debug[512];
    lv_obj_t * btn;
    lv_area_t b;
    unsigned ends = s_end_calls;
    bool ok = true;

    btn = find_text_button(lv_layer_top(), "×");
    if(!hg_expect(btn != NULL, "close button present")) {
        return false;
    }

    lv_obj_get_coords(btn, &b);
    hg_queue_click((b.x1 + b.x2) / 2, (b.y1 + b.y2) / 2);
    hg_pump(12);
    ok &= hg_expect(s_end_calls == ends + 1, "close button ends the round");
    vg_voice_overlay_debug(debug, sizeof(debug));
    ok &= hg_expect(strstr(debug, "hidden") != NULL, "close hides the overlay");

    s_ui.visible = true;
    hg_pump(4);
    vg_voice_overlay_debug(debug, sizeof(debug));
    ok &= hg_expect(strstr(debug, "hidden") != NULL,
                    "dismissed overlay stays hidden while the round winds down");

    s_ui.visible = false;
    hg_pump(4);
    s_ui.generation++;
    s_ui.visible = true;
    return ok;
}

static void fill_text(char *out, size_t cap)
{
    /* 使用三字节 UTF-8 文本，填到容量边界并保留结尾空字符。 */
    size_t n = 0;
    while(n + 3 < cap) {
        memcpy(out + n, "温", 3);
        n += 3;
    }
    while(n + 1 < cap) out[n++] = 'x';
    out[n] = '\0';
}

int main(int argc, char **argv)
{
    bool ok = true;
    char debug[512];
    unsigned round, state;
    (void)argc;
    (void)argv;

    hg_init();
    fill_text(s_ui.status, sizeof(s_ui.status));
    fill_text(s_ui.question, sizeof(s_ui.question));
    fill_text(s_ui.answer, sizeof(s_ui.answer));
    fill_text(s_ui.error, sizeof(s_ui.error));

    for(round = 0; round < 20; round++) {
        vg_voice_overlay_create();
        s_ui.visible = true;
        for(state = VG_VS_RECORDING; state <= VG_VS_ERROR; state++) {
            s_ui.state = state;
            s_ui.muted = state & 1;
            s_ui.speech_failed = state & 1;
            s_ui.record_left_ms = 15000;
            s_ui.budget_left_ms = 60000;
            s_ui.result_left_ms = 15000;
            hg_pump(4);
            vg_voice_overlay_debug(debug, sizeof(debug));
            if(strstr(debug, "overlay: panel=") == NULL) {
                fprintf(stderr, "FAIL no panel snapshot round=%u state=%u\n",
                        round, state);
                ok = false;
            }
            if(round == 0) {
                expect_overlay_buttons((state & 1) ? "播报:关" : "播报:开");
                if(state == VG_VS_RECORDING || state == VG_VS_ASR ||
                   state == VG_VS_RESULT || state == VG_VS_ERROR) {
                    ok &= expect_close_dismisses();
                    hg_pump(4);
                }
            }
        }
        /* 等待排版和绘制完成；测量期间保持快照不变。 */
        hg_pump(4);
        {
            uint32_t before = hg_flush_count();
            hg_pump(16);
            ok &= hg_expect(hg_flush_count() == before,
                            "unchanged voice snapshot does not redraw");
            snprintf(s_ui.answer, sizeof(s_ui.answer), "answer %u", round);
            hg_pump(4);
            ok &= hg_expect(hg_flush_count() > before,
                            "changed voice answer is rendered");
            fill_text(s_ui.answer, sizeof(s_ui.answer));
        }
        s_ui.visible = false;
        hg_pump(4);
        vg_voice_overlay_debug(debug, sizeof(debug));
        ok &= hg_expect(strstr(debug, "hidden") != NULL,
                        "voice panel hides after the round");
        if(round == 0) {
            int cx = 0, cy = 0;
            ok &= hg_expect(hg_click_btn("报告"),
                            "page button under the hidden overlay is clickable");
            ok &= hg_expect(hg_find_btn_center("刷新", 0, &cx, &cy),
                            "hidden overlay no longer blocks page navigation");
        }
        vg_voice_overlay_destroy();
        hg_pump(4);
    }
    ok &= hg_expect(true, "180 voice states with maximum text rendered");
    return ok ? 0 : 1;
}
