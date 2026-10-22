/****************************************************************************
 * app/velaguard/vg_voice_ptt.c
 *
 * 把 USER1 按键事件接到本地录放闭环。
 *
 * 行为（执行方案第 2 节与本轮任务书）：
 *   - 按下开始录音，松开结束并回放本次录音；
 *   - 15 秒上限只结束、回放一次；持续按住不重复触发，松手后才接受下一次；
 *   - 回放期间再次按下，先停止播放并确认硬件与缓冲回收，再开始新录音。
 *
 * 时序分工：
 *   vg_ptt 状态机判定去抖、15 秒上限和松手门控；本层只把按下/提交翻译成
 *   录音请求。请求投递给常驻语音任务后立即返回，所以按键任务在回放期间
 *   仍能收到下一次按下——这正是"回放中按键先停播"能做到的前提。
 ****************************************************************************/

#include <nuttx/config.h>

#ifdef CONFIG_VG_VOICE_LOOPBACK

#include <stdbool.h>
#include <stdio.h>

#include "vg_ptt.h"
#include "vg_ptt_board.h"
#include "vg_voice.h"
#include "vg_voice_ptt.h"

/****************************************************************************
 * Private Data
 ****************************************************************************/

static bool g_started;
static uint32_t g_button_generation;
static uint32_t g_button_press_ms;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/**
  * @brief  按键事件处理。在 vgptt 任务里执行，不得阻塞。
  * @param  rec  事件记录。
  * @param  arg  未使用。
  */
static void voice_ptt_handler(const struct vg_ptt_event_record *rec,
                              void *arg)
{
  (void)arg;

  if (rec->type == VG_PTT_EV_BEGIN)
    {
      int ret;
      g_button_generation = 0;
      g_button_press_ms = rec->press_ms;
      ret = vg_voice_record_begin_owned(&g_button_generation);

      if (ret < 0)
        {
          printf("vgvoice: begin refused: %d\n", ret);
        }

      return;
    }

  if (rec->type == VG_PTT_EV_SUBMIT)
    {
      /* 松手与 15 秒上限都走这里。vg_ptt 保证一轮只提交一次，所以不会
       * 重复回放；持续按住时它在 HELD 状态里不再产生提交。
       */

      if (g_button_generation && rec->press_ms == g_button_press_ms)
        {
          (void)vg_voice_finish_owned(g_button_generation, false);
          g_button_generation = 0;
        }
    }
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int vg_voice_ptt_start(void)
{
  int ret;

  if (g_started)
    {
      return 0;
    }

  ret = vg_voice_init();
  if (ret < 0)
    {
      printf("vgvoice: init failed: %d\n", ret);
      return ret;
    }

  /* 回调在按键任务创建时固定，因此必须由本函数完成启动。若按键任务已被
   * 别处用默认处理启动，这里的回调不会生效，所以开机路径要先调用本函数，
   * 再由它启动按键任务。
   */

  ret = vg_ptt_start(NULL, voice_ptt_handler, NULL);
  if (ret < 0)
    {
      printf("vgvoice: ptt start failed: %d\n", ret);
      return ret;
    }

  g_started = true;
  return 0;
}

#endif /* CONFIG_VG_VOICE_LOOPBACK */
