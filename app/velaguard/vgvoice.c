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
#include <pthread.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#include "vg_voice.h"

#ifdef CONFIG_EXAMPLES_AI_AGENT_VELA
#include "vg_voice_cloud.h"
#include "vg_voice_pref.h"
#include "vg_voice_round.h"
#ifdef VG_VOICE_ROUND
#include "vg_voice_overlay.h"
#endif

/* The cloud adapter is the same code the round uses.  These two commands make
 * it reachable without a button press, so the request shape and the audio
 * conversion can be checked from NSH with a recorded or supplied file. */

static uint32_t vgvoice_now_ms(void)
{
  struct timespec ts;

  if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
    {
      return 0;
    }

  return (uint32_t)((uint64_t)ts.tv_sec * 1000u
                    + (uint64_t)ts.tv_nsec / 1000000u);
}
#endif

#ifdef CONFIG_EXAMPLES_AI_AGENT_VELA
#ifdef CONFIG_NETDB_DNSCLIENT
#include <nuttx/net/dns.h>
#include <arpa/inet.h>
static int vgvoice_dns_budget(void *arg)
{
  vela_req_ctl_t *ctl = arg;
  if (agent_http_cancelled(ctl)) return -ECANCELED;
  return agent_http_budget_ms(ctl, vgvoice_now_ms());
}
#endif
struct vgvoice_probe_cancel
{
  volatile int cancel;
  volatile bool done;
  uint32_t at_ms;
};

static void *vgvoice_probe_cancel_worker(void *arg)
{
  struct vgvoice_probe_cancel *c = arg;
  while (!c->done)
    {
      if ((int32_t)(vgvoice_now_ms() - c->at_ms) >= 0)
        {
          c->cancel = 1;
          break;
        }
      usleep(10000);
    }
  return NULL;
}
#endif

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
#ifdef CONFIG_EXAMPLES_AI_AGENT_VELA
      printf("       vgvoice asr [wav] [budget_ms] | tts <text> | play <wav>\n");
      printf("       vgvoice mute [on|off] | round [press|release|ask <wav>|end|text|retry|stack]\n");
#endif
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

#ifdef CONFIG_EXAMPLES_AI_AGENT_VELA
  if (strcmp(cmd, "asr") == 0)
    {
      const char *path = (argc >= 3) ? argv[2] : VG_VOICE_WAV_PATH;
      struct vg_vc_info info;
      vela_req_ctl_t ctl;
      char text[VG_VC_TEXT_MAX];
      enum vg_vc_result rc;
      unsigned long budget = 30000;

      /* An optional budget lets the deadline path run on the board without a
       * button press: a short budget must end the request quickly, and the
       * next call must still work on the reclaimed connection. */

      if (argc >= 4)
        {
          char *end;

          budget = strtoul(argv[3], &end, 10);
          if (*end || budget < 100 || budget > 120000)
            {
              printf("vgvoice: budget out of range\n");
              return EXIT_FAILURE;
            }
        }

      ctl.deadline_ms = vgvoice_now_ms() + (uint32_t)budget;
      ctl.cancel = NULL;

      rc = vg_voice_cloud_asr(path, &ctl, text, sizeof(text), &info);
      printf("vgvoice: asr rc=%s status=%d req=%u resp=%u ms=%u\n",
             vg_vc_result_name(rc), info.status, (unsigned)info.req_bytes,
             (unsigned)info.resp_bytes, (unsigned)info.elapsed_ms);
      printf("vgvoice: asr text=%s\n", text[0] ? text : "(empty)");
      return (rc == VG_VC_OK || rc == VG_VC_EMPTY) ? EXIT_SUCCESS
                                                   : EXIT_FAILURE;
    }

  if (strcmp(cmd, "tts") == 0 && argc >= 3)
    {
      struct vg_vc_info info;
      struct stat st;
      vela_req_ctl_t ctl;
      char text[VG_VC_TEXT_MAX];
      enum vg_vc_result rc;
      size_t used = 0;
      int i;

      text[0] = '\0';
      for (i = 2; i < argc; i++)
        {
          int n = snprintf(text + used, sizeof(text) - used, "%s%s",
                           (i > 2) ? " " : "", argv[i]);

          if (n < 0 || (size_t)n >= sizeof(text) - used)
            {
              printf("vgvoice: text too long\n");
              return EXIT_FAILURE;
            }

          used += (size_t)n;
        }

      ctl.deadline_ms = vgvoice_now_ms() + 30000u;
      ctl.cancel = NULL;

      rc = vg_voice_cloud_tts(text, VG_VC_TTS_WAV_PATH, &ctl, &info);
      printf("vgvoice: tts rc=%s status=%d pcm=%u resp=%u ms=%u\n",
             vg_vc_result_name(rc), info.status, (unsigned)info.audio_bytes,
             (unsigned)info.resp_bytes, (unsigned)info.elapsed_ms);

      if (stat(VG_VC_TTS_WAV_PATH, &st) == 0)
        {
          printf("vgvoice: tts wav=%ld bytes\n", (long)st.st_size);
        }

      return (rc == VG_VC_OK) ? EXIT_SUCCESS : EXIT_FAILURE;
    }

  if (strcmp(cmd, "mute") == 0)
    {
      /* Same entry the overlay uses, so the preference file and the running
       * round change together.  Without a board-side command, the reboot
       * persistence of the switch could only be checked by touching the
       * screen. */

      if (argc < 3)
        {
          printf("vgvoice: muted=%d\n", vg_voice_round_muted() ? 1 : 0);
          return EXIT_SUCCESS;
        }

      if (strcmp(argv[2], "on") == 0)
        {
          vg_voice_pref_set_muted(true);
        }
      else if (strcmp(argv[2], "off") == 0)
        {
          vg_voice_pref_set_muted(false);
        }
      else
        {
          printf("vgvoice: mute expects on or off\n");
          return EXIT_FAILURE;
        }

      printf("vgvoice: muted=%d\n", vg_voice_round_muted() ? 1 : 0);
      return EXIT_SUCCESS;
    }

#ifdef CONFIG_NETDB_DNSCLIENT
  if (strcmp(cmd, "dnsprobe") == 0)
    {
      char *end;
      unsigned long budget, port, cancel_after = 0;
      struct sockaddr_in server = {0};
      struct sockaddr_storage address[4];
      struct dns_query_control_s control = {0};
      struct vgvoice_probe_cancel cancellation = {0};
      vela_req_ctl_t ctl;
      pthread_t cancel_thread;
      uint32_t start;
      int n = 4;
      int rc;
      if (argc != 6 && argc != 7)
        {
          printf("vgvoice: dnsprobe host server-ip port budget_ms [cancel_ms]\n");
          return EXIT_FAILURE;
        }
      if (inet_pton(AF_INET, argv[3], &server.sin_addr) != 1)
        return EXIT_FAILURE;
      port = strtoul(argv[4], &end, 10);
      if (*end || !port || port > 65535) return EXIT_FAILURE;
      budget = strtoul(argv[5], &end, 10);
      if (*end || !budget || budget > 30000) return EXIT_FAILURE;
      if (argc == 7)
        {
          cancel_after = strtoul(argv[6], &end, 10);
          if (*end || !cancel_after || cancel_after > budget)
            return EXIT_FAILURE;
        }
      start = vgvoice_now_ms();
      ctl.deadline_ms = start + (uint32_t)budget;
      if (ctl.deadline_ms == 0) ctl.deadline_ms = 1;
      ctl.cancel = &cancellation.cancel;
      if (cancel_after)
        {
          pthread_attr_t attr;
          cancellation.at_ms = start + (uint32_t)cancel_after;
          pthread_attr_init(&attr);
          pthread_attr_setstacksize(&attr, 2048);
          rc = pthread_create(&cancel_thread, &attr,
                              vgvoice_probe_cancel_worker, &cancellation);
          pthread_attr_destroy(&attr);
          if (rc) return EXIT_FAILURE;
        }
      server.sin_family = AF_INET;
      server.sin_port = htons((uint16_t)port);
      control.budget_ms = vgvoice_dns_budget;
      control.arg = &ctl;
      control.server = (const struct sockaddr *)&server;
      control.serverlen = sizeof(server);
      rc = dns_query_controlled(argv[2], address, &n, &control);
      cancellation.done = true;
      if (cancel_after) pthread_join(cancel_thread, NULL);
      printf("vgvoice: dnsprobe rc=%d count=%d ms=%u\n", rc, n,
             (unsigned)(vgvoice_now_ms() - start));
      return rc == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
    }
#endif

  if (strcmp(cmd, "netprobe") == 0)
    {
      /* NSH-only transport diagnostic: always use a fixed invalid test
       * credential.  Never load provision, and never print the reply body.
       * This exercises the real TLS/HTTP path against a local fault server
       * without changing the product endpoint or exposing its API key. */

      char *end;
      unsigned long budget;
      unsigned long port;
      vela_req_ctl_t ctl;
      uint32_t start;
      size_t received = 0;
      int status = 0;
      int rc;
      struct vgvoice_probe_cancel cancellation = {0};
      pthread_t cancel_thread;
      unsigned long cancel_after = 0;

      if ((argc != 6 && argc != 7) || strpbrk(argv[2], "\r\n \t") != NULL ||
          argv[4][0] != '/' || strpbrk(argv[4], "\r\n \t") != NULL)
        {
          printf("vgvoice: netprobe host port /path budget_ms [cancel_ms]\n");
          return EXIT_FAILURE;
        }

      port = strtoul(argv[3], &end, 10);
      if (*end != '\0' || port == 0 || port > 65535)
        {
          return EXIT_FAILURE;
        }

      budget = strtoul(argv[5], &end, 10);
      if (*end != '\0' || budget == 0 || budget > 30000)
        {
          return EXIT_FAILURE;
        }

      if (argc == 7)
        {
          cancel_after = strtoul(argv[6], &end, 10);
          if (*end != '\0' || cancel_after == 0 || cancel_after > budget)
            {
              return EXIT_FAILURE;
            }
        }

      start = vgvoice_now_ms();
      if (cancel_after != 0)
        {
          pthread_attr_t attr;
          cancellation.at_ms = start + (uint32_t)cancel_after;
          pthread_attr_init(&attr);
          pthread_attr_setstacksize(&attr, 2048);
          rc = pthread_create(&cancel_thread, &attr,
                              vgvoice_probe_cancel_worker, &cancellation);
          pthread_attr_destroy(&attr);
          if (rc != 0)
            {
              return EXIT_FAILURE;
            }
        }
      ctl.deadline_ms = start + (uint32_t)budget;
      if (ctl.deadline_ms == 0)
        {
          ctl.deadline_ms = 1;
        }

      ctl.cancel = &cancellation.cancel;
      rc = agent_http_probe(argv[2], argv[3], argv[4], &ctl,
                            &received, &status);
      cancellation.done = true;
      if (cancel_after != 0)
        {
          pthread_join(cancel_thread, NULL);
        }
      printf("vgvoice: netprobe rc=%d status=%d bytes=%u ms=%u\n", rc,
             status, (unsigned)received,
             (unsigned)(vgvoice_now_ms() - start));
      return rc == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
    }

  if (strcmp(cmd, "round") == 0)
    {
      struct vg_voice_ui ui;

      if (argc >= 4 && strcmp(argv[2], "ask") == 0)
        {
          vg_voice_round_set_asr_source(argv[3]);
          vg_voice_round_press_sim(true);
          printf("vgvoice: round ask synthetic src=%s\n", argv[3]);
          return EXIT_SUCCESS;
        }

      if (argc >= 3 && strcmp(argv[2], "press") == 0)
        {
          vg_voice_round_press_sim(true);
          printf("vgvoice: round press synthetic\n");
          return EXIT_SUCCESS;
        }

      if (argc >= 3 && strcmp(argv[2], "release") == 0)
        {
          vg_voice_round_press_sim(false);
          printf("vgvoice: round release synthetic\n");
          return EXIT_SUCCESS;
        }

      if (argc >= 3 && strcmp(argv[2], "stack") == 0)
        {
          size_t size = 0;
          size_t used = 0;
          size_t headroom = 0;

          if (vg_voice_round_stack(&size, &used, &headroom) != 0)
            {
              printf("vgvoice: stack measurement unavailable\n");
              return EXIT_FAILURE;
            }

          printf("vgvoice: stack size=%u used=%u headroom=%u\n",
                 (unsigned)size, (unsigned)used, (unsigned)headroom);
          return EXIT_SUCCESS;
        }

      if (argc >= 3 && strcmp(argv[2], "text") == 0)
        {
          if (!vg_voice_round_ui(&ui))
            {
              printf("vgvoice: round not started\n");
              return EXIT_FAILURE;
            }

          printf("q=%s\n", ui.question);
          printf("a=%s\n", ui.answer);

          if (ui.error[0] != '\0')
            {
              printf("e=%s\n", ui.error);
            }

          return EXIT_SUCCESS;
        }

      if (argc >= 3 && strcmp(argv[2], "overlay") == 0)
        {
          char buf[600];

#ifdef VG_VOICE_ROUND
          (void)vg_voice_overlay_debug(buf, sizeof(buf));
          printf("%s", buf);
          return EXIT_SUCCESS;
#else
          printf("vgvoice: overlay not built\n");
          return EXIT_FAILURE;
#endif
        }

      if (argc == 4 && strcmp(argv[2], "hold") == 0 &&
          (strcmp(argv[3], "on") == 0 || strcmp(argv[3], "off") == 0))
        {
          vg_voice_round_hold(strcmp(argv[3], "on") == 0);
          printf("vgvoice: round hold=%s requested\n", argv[3]);
          return EXIT_SUCCESS;
        }

      if (argc == 3 && strcmp(argv[2], "counts") == 0)
        {
          uint32_t asr, tts;
          if (vg_voice_round_counts(&asr, &tts) != 0) return EXIT_FAILURE;
          printf("vgvoice: requests asr=%u tts=%u\n",
                 (unsigned)asr, (unsigned)tts);
          return EXIT_SUCCESS;
        }

      if (argc >= 3 && strcmp(argv[2], "retry") == 0)
        {
          vg_voice_round_retry_tts();
          printf("vgvoice: round retry speech\n");
          return EXIT_SUCCESS;
        }

      if (argc >= 3 && strcmp(argv[2], "end") == 0)
        {
          vg_voice_round_end();
          printf("vgvoice: round end requested\n");
          return EXIT_SUCCESS;
        }

      if (argc >= 3)
        {
          fprintf(stderr, "vgvoice: unknown round command: %s\n", argv[2]);
          return EXIT_FAILURE;
        }

      if (!vg_voice_round_ui(&ui))
        {
          printf("vgvoice: round not started\n");
          return EXIT_FAILURE;
        }

      printf("vgvoice: round visible=%d state=%u gen=%u muted=%d failed=%d\n",
             ui.visible ? 1 : 0, (unsigned)ui.state, (unsigned)ui.generation,
             ui.muted ? 1 : 0, ui.speech_failed ? 1 : 0);
      printf("vgvoice: round status=%s\n", ui.status);
      printf("vgvoice: round q=%s\n", ui.question);
      printf("vgvoice: round a=%u bytes\n", (unsigned)strlen(ui.answer));

      if (ui.error[0] != '\0')
        {
          printf("vgvoice: round error=%s\n", ui.error);
        }

      return EXIT_SUCCESS;
    }
#endif

  if (strcmp(cmd, "play") == 0 && argc >= 3)
    {
      int rc = vg_voice_play_file(argv[2]);

      printf("vgvoice: play rc=%d\n", rc);
      if (rc != 0)
        {
          return EXIT_FAILURE;
        }

      if (!vg_voice_wait_idle(60000))
        {
          printf("vgvoice: play did not finish\n");
          return EXIT_FAILURE;
        }

      printf("vgvoice: play done\n");
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
#ifdef CONFIG_EXAMPLES_AI_AGENT_VELA
  printf("       vgvoice asr [wav] | tts <text> | play <wav>\n");
#endif
  return EXIT_FAILURE;
}

#endif /* CONFIG_VG_VOICE_LOOPBACK */
