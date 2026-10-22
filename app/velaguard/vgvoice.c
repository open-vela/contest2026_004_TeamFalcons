/****************************************************************************
 * app/velaguard/vgvoice.c
 *
 * BVO-05 本地音频闭环的 NSH 诊断入口。
 *
 * 用法：
 *   vgvoice status        打印状态与统计（一行一项，便于脚本按关键字判定）
 *   vgvoice start         开始一轮录音，等价于按下 USER1
 *   vgvoice stop          结束录音并回放
 *   vgvoice cancel        取消当前录音，丢弃数据且不回放
 *   vgvoice wait <ms>     等语音任务回到空闲，便于脚本串联
 *
 * 这些命令用于操作者不在场时复核通路；正常验收仍以真实按键为准。命令
 * 不做任何自动确认，也不触碰点表、配置或 MQTT。
 ****************************************************************************/

#include <nuttx/config.h>

#ifdef CONFIG_VG_VOICE_LOOPBACK

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vg_voice.h"

/**
  * @brief  打印状态与统计。
  */
static void vgvoice_status(void)
{
  struct vg_voice_stats s;

  vg_voice_get_stats(&s);

  printf("vgvoice: state=%s\n", vg_voice_state_name(s.state));
  printf("vgvoice: rounds=%u played=%u cancelled=%u too_short=%u "
         "failures=%u timeouts=%u\n",
         (unsigned)s.rounds, (unsigned)s.played, (unsigned)s.cancelled,
         (unsigned)s.too_short, (unsigned)s.failures, (unsigned)s.timeouts);
  printf("vgvoice: last result=%s ms=%u samples=%u bytes=%u\n",
         vg_voice_result_name(s.last_result), (unsigned)s.last_ms,
         (unsigned)s.last_samples, (unsigned)s.last_bytes);
  printf("vgvoice: level peak=%u rms=%u\n",
         (unsigned)s.peak_abs, (unsigned)s.rms);
  printf("vgvoice: dma dropped=%u overruns=%u clockerrors=%u errors=%u\n",
         (unsigned)s.dma_dropped, (unsigned)s.dma_overruns,
         (unsigned)s.dma_clockerrors, (unsigned)s.dma_errors);
  printf("vgvoice: round_ms=%u temp_left=%u worker=%u\n",
         (unsigned)s.round_ms, (unsigned)s.temp_files_left,
         (unsigned)s.worker_pid);
  printf("vgvoice: capture halves=%u drained=%u overwritten=%u wraps=%u "
         "elapsed_ms=%u rate=%u B/s\n",
         (unsigned)s.cap_halves, (unsigned)s.cap_drained,
         (unsigned)s.cap_overwrites, (unsigned)s.cap_wraps,
         (unsigned)s.cap_elapsed_ms, (unsigned)s.cap_rate_bps);
  printf("vgvoice: tail_bytes=%u stage_ms=%u\n",
         (unsigned)s.cap_tailbytes, (unsigned)s.stage_ms);
  printf("vgvoice: generation=%u queue_highwater=%u copy_max_us=%u filter_max_us=%u\n",
         (unsigned)s.generation, (unsigned)s.queue_highwater,
         (unsigned)s.copy_max_us, (unsigned)s.filter_max_us);
  printf("vgvoice: content zcr=%u clip=%u tick_us=10000\n",
         (unsigned)s.zcr, (unsigned)s.clip_samples);
}

/**
  * @brief  NSH 入口。
  * @param  argc  参数个数。
  * @param  argv  参数向量；argv[1] 为子命令。
  * @retval 0 成功；1 失败或用法错误。
  */
int main(int argc, FAR char *argv[])
{
  const char *cmd;

  if (argc < 2)
    {
      printf("usage: vgvoice status|record <ms>|start|stop|cancel|wait <ms>\n");
      return EXIT_FAILURE;
    }

  cmd = argv[1];

  if (strcmp(cmd, "status") == 0)
    {
      vgvoice_status();
      return EXIT_SUCCESS;
    }

  if (strcmp(cmd, "record") == 0 && argc == 3)
    {
      char *end;
      unsigned long ms = strtoul(argv[2], &end, 10);
      if (*end || ms < 100 || ms > VG_VOICE_MAX_MS ||
          vg_voice_record_for((uint32_t)ms) < 0)
        {
          printf("vgvoice: invalid duration or record refused\n");
          return EXIT_FAILURE;
        }
      printf("vgvoice: timed record queued ms=%lu\n", ms);
      return EXIT_SUCCESS;
    }

  if (strcmp(cmd, "start") == 0)
    {
      if (vg_voice_record_begin() < 0)
        {
          printf("vgvoice: start failed\n");
          return EXIT_FAILURE;
        }

      vgvoice_status();
      return EXIT_SUCCESS;
    }

  if (strcmp(cmd, "stop") == 0)
    {
      (void)vg_voice_finish(false);
      printf("vgvoice: stop requested\n");
      return EXIT_SUCCESS;
    }

  if (strcmp(cmd, "cancel") == 0)
    {
      (void)vg_voice_cancel();
      printf("vgvoice: cancel requested\n");
      return EXIT_SUCCESS;
    }

  if (strcmp(cmd, "wait") == 0)
    {
      long ms = (argc >= 3) ? strtol(argv[2], NULL, 0) : 20000;

      if (ms <= 0 || ms > 120000)
        {
          printf("vgvoice: wait out of range\n");
          return EXIT_FAILURE;
        }

      if (!vg_voice_wait_idle((uint32_t)ms))
        {
          printf("vgvoice: wait timed out after %ld ms\n", ms);
          vgvoice_status();
          return EXIT_FAILURE;
        }

      printf("vgvoice: idle after wait\n");
      vgvoice_status();
      return EXIT_SUCCESS;
    }

  printf("usage: vgvoice status|record <ms>|start|stop|cancel|wait <ms>\n");
  return EXIT_FAILURE;
}

#endif /* CONFIG_VG_VOICE_LOOPBACK */
