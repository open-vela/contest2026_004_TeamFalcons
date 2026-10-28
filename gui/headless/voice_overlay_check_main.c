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

bool vg_voice_round_ui(struct vg_voice_ui *out)
{
    *out = s_ui;
    return true;
}

void vg_voice_round_end(void) { s_ui.visible = false; }
void vg_voice_round_retry_tts(void) { s_ui.speech_failed = false; }
bool vg_voice_round_muted(void) { return s_ui.muted; }
void vg_voice_pref_set_muted(bool muted) { s_ui.muted = muted; }
void vg_voice_round_hold(bool held) { (void)held; }

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
        vg_voice_overlay_destroy();
        hg_pump(4);
    }
    ok &= hg_expect(true, "180 voice states with maximum text rendered");
    return ok ? 0 : 1;
}
