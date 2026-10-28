/****************************************************************************
 * app/velaguard/vgptt.c
 *
 * NSH 诊断命令：启动/停止 USER1 按住说话线程，读取按放统计。
 *
 * 用法：
 *   vgptt start            启动按键线程（已在运行则无动作）
 *   vgptt stop             停止按键线程
 *   vgptt status           打印统计与状态机当前状态
 *   vgptt reset            清零计数（不影响状态机）
 *
 * 本命令只做事件层：没有录音、没有网络。BVO-03 起由录音层替换默认处理。
 ****************************************************************************/

#include <nuttx/config.h>

#ifdef CONFIG_VG_PTT

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "vg_ptt_board.h"

/**
  * @brief  打印统计，一行一项，便于串口脚本按关键字判定。
  * @param  s  统计快照。
  * @retval None
  */
static void print_status(const struct vg_ptt_stats *s)
{
  printf("vgptt: running=%s opened=%s state=%s\n",
         s->running ? "yes" : "no",
         s->opened ? "yes" : "no",
         vg_ptt_state_name(s->state));
  printf("vgptt: begins=%u submits=%u timeouts=%u last_ms=%u\n",
         (unsigned)s->begins, (unsigned)s->submits,
         (unsigned)s->timeouts, (unsigned)s->last_duration_ms);
  printf("vgptt: samples=%u raw_high=%u events=%u timeouts_wake=%u "
         "poll_err=%u read_err=%u\n",
         (unsigned)s->samples, (unsigned)s->raw_highs,
         (unsigned)s->wake_events, (unsigned)s->wake_timeouts,
         (unsigned)s->poll_errors, (unsigned)s->read_errors);
}

/**
  * @brief  vgptt NSH 入口。
  * @param  argc  参数个数。
  * @param  argv  参数向量；argv[1] 为子命令。
  * @retval 0  成功。
  * @retval 1  用法或运行时错误。
  */
int main(int argc, FAR char *argv[])
{
  struct vg_ptt_stats stats;

  if (argc < 2 || strcmp(argv[1], "status") == 0)
    {
      vg_ptt_get_stats(&stats);
      print_status(&stats);
      return 0;
    }

  if (strcmp(argv[1], "start") == 0)
    {
      int ret = vg_ptt_start(NULL, NULL, NULL);

      if (ret < 0)
        {
          printf("vgptt: start failed: %d\n", ret);
          return 1;
        }

      printf("vgptt: started\n");
      return 0;
    }

  if (strcmp(argv[1], "stop") == 0)
    {
      if (vg_ptt_stop(3000) < 0)
        {
          printf("vgptt: stop did not complete\n");
          return 1;
        }

      printf("vgptt: stopped\n");
      return 0;
    }

  if (strcmp(argv[1], "reset") == 0)
    {
      vg_ptt_reset_stats();
      printf("vgptt: counters cleared\n");
      return 0;
    }

  if (strcmp(argv[1], "level") == 0)
    {
      /* 直接读一次电平，不动状态机、不计数。连续执行时按住按键应看到
       * pressed=1，从而把"引脚读不到"和"中断没送达"分开。 */

      bool pressed = false;
      int ret = vg_ptt_probe_level(&pressed);

      if (ret < 0)
        {
          printf("vgptt: level read failed: %d\n", ret);
          return 1;
        }

      printf("vgptt: level pressed=%d\n", pressed ? 1 : 0);
      return 0;
    }

  if (strcmp(argv[1], "watch") == 0)
    {
      /* 连续采样并只打印电平变化，用于确认按键是否真的到达引脚。
       * 用法：vgptt watch [秒数]，默认 10 秒。 */

      int seconds = (argc >= 3) ? atoi(argv[2]) : 10;
      int loops;
      int i;
      int last = -1;
      int changes = 0;

      if (seconds <= 0 || seconds > 120)
        {
          printf("vgptt: watch seconds must be 1..120\n");
          return 1;
        }

      loops = seconds * 20;
      printf("vgptt: watching level for %d s (== press now ==)\n", seconds);

      for (i = 0; i < loops; i++)
        {
          bool pressed = false;

          if (vg_ptt_probe_level(&pressed) == 0)
            {
              int now = pressed ? 1 : 0;

              if (now != last)
                {
                  printf("vgptt: t=%dms level=%d\n", (i * 50), now);
                  last = now;
                  changes++;
                }
            }

          usleep(50 * 1000);
        }

      printf("vgptt: watch done changes=%d final=%d\n", changes, last);
      return 0;
    }

  printf("Usage:\n");
  printf("  vgptt start|stop|status|reset|level|watch [sec]\n");
  return 1;
}

#endif /* CONFIG_VG_PTT */
