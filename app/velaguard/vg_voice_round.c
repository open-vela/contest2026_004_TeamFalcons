/****************************************************************************
 * app/velaguard/vg_voice_round.c
 *
 * BVO-11/12, operations-assistant update of 2026-10-25: the voice
 * round from the button to the speaker.
 *
 * One worker thread owns the pipeline.  The button callback only posts an
 * event and raises the cancel flag, so a press during recognition, waiting or
 * playback is noticed within one transport slice rather than at the end of
 * the stage.
 *
 * The recognized question goes to the on-board agent as-is, with the
 * previous exchange as follow-up context.  The agent (read-only tool scope
 * enforced in C on the agent side) composes the answer and a spoken summary;
 * vg_va_split_reply bounds and validates that contract here.  The display
 * text is marked as an AI answer so it stays distinguishable from the
 * rule-based alarm page.
 ****************************************************************************/

#include <nuttx/config.h>

#if defined(CONFIG_VG_VOICE_LOOPBACK) && defined(CONFIG_EXAMPLES_AI_AGENT_VELA)

#include <errno.h>
#include <nuttx/arch.h>
#include <nuttx/sched.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <syslog.h>
#include <time.h>
#include <unistd.h>

#include "tools/tool_provider.h"
#include "vg_agent_round.h"
#include "vg_agent_tools.h"
#ifdef CONFIG_VG_BUS_DISCOVER
#  include "vg_discover.h"
#endif
#include "vg_ptt_board.h"
#include "vg_voice.h"
#include "vg_voice_answer.h"
#include "vg_voice_cloud.h"
#include "vg_voice_facts.h"   /* request-active probe for cancel settle */
#ifdef CONFIG_VG_VOICE_TTS_STREAM
#  include "vg_tts_player.h"
#  include "vg_voice_pcm.h"
#endif
#include "vg_voice_round.h"
#include "vg_voice_session.h"

/* The worker owns the whole call chain: a 4 KB response buffer, the 4 KB body
 * piece and the 2 KB TLS read buffer inside the transport, the TLS handshake,
 * and the render buffers.  16 KB was not enough headroom for that stack. */

/* mbedTLS handshake plus the ASR body builder measured around 10 KB of frame
 * at the fault site, but the handshake's deepest path is not observable from
 * the dump; 48 KB leaves room for the certificate parse. */
#define VR_STACK_SIZE   49152
#define VR_PRIORITY     118
#define VR_TICK_MS      50
#define VR_AGENT_POLL_MS 10
#define VR_PLAY_EXTRA_MS 5000u

struct voice_round
{
  bool                 started;
  pthread_t            thread;
  sem_t                wake;
  struct vg_voice_session s;
  volatile int         cancel;
  volatile bool        press_pending;
  volatile bool        submit_pending;
  volatile bool        end_pending;
  volatile bool        retry_pending;
  volatile bool        hold_pending;
  volatile bool        hold_value;
  volatile bool        mute_pending;
  volatile bool        mute_value;
  volatile bool        quit;
  uint32_t             record_generation;
  uint32_t             clip_generation;
  uint32_t             asr_requests;
  uint32_t             tts_requests;
  uint32_t             end_requested_ms;
  char                 asr_src[80];
  bool                 muted;
  bool                 hold;

  /* Follow-up context for the operations assistant: the previous question
   * and a bounded excerpt of its answer, valid for VG_VS_IDLE_MS and cleared
   * on a manual end, a table change or a restart.  Older entries are skipped
   * at request-build time, so a stale context never rides along. */

  uint32_t             ctx_ms;
  char                 ctx_question[VG_VS_TEXT_MAX];
  char                 ctx_answer[384];

  pthread_mutex_t      ui_lock;
  struct vg_voice_ui   ui;
};

static struct voice_round g_vr;
static pthread_mutex_t g_start_lock = PTHREAD_MUTEX_INITIALIZER;
static bool g_round_ready;

static uint32_t vr_now_ms(void)
{
  struct timespec ts;

  if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
    {
      return 0;
    }

  return (uint32_t)((uint64_t)ts.tv_sec * 1000u
                    + (uint64_t)ts.tv_nsec / 1000000u);
}

/* Stage timing for the round.  The numbers are relative to the release
 * that started the budget, so the gaps between the stages show up in the
 * serial log instead of only in the total. */

static uint32_t vr_elapsed_ms(void)
{
  uint32_t now = vr_now_ms();

  if (g_vr.s.submit_ms == 0 || now < g_vr.s.submit_ms)
    {
      return 0;
    }

  return now - g_vr.s.submit_ms;
}

static void vr_log_stage(const char *stage)
{
  syslog(LOG_INFO, "[vgvoice] stage=%s t=%u mono_ms=%u\n", stage,
         (unsigned)vr_elapsed_ms(), (unsigned)vr_now_ms());
}

/* ── UI snapshot ────────────────────────────────────────────────── */

static void vr_ui_set_status(const char *status)
{
  pthread_mutex_lock(&g_vr.ui_lock);
  snprintf(g_vr.ui.status, sizeof(g_vr.ui.status), "%s",
           status != NULL ? status : "");
  pthread_mutex_unlock(&g_vr.ui_lock);
}

static void vr_ui_publish(void)
{
  struct vg_voice_session *s = &g_vr.s;
  uint32_t now = vr_now_ms();

  pthread_mutex_lock(&g_vr.ui_lock);
  g_vr.ui.generation = s->generation;
  g_vr.ui.state = (uint32_t)s->state;
  g_vr.ui.muted = s->muted;
  g_vr.ui.speech_failed = s->speech_failed;
  g_vr.ui.record_left_ms =
      (s->state == VG_VS_RECORDING)
          ? (uint32_t)(VG_VS_RECORD_MAX_MS -
                       (now - s->record_ms > VG_VS_RECORD_MAX_MS
                            ? VG_VS_RECORD_MAX_MS
                            : now - s->record_ms))
          : 0;
  g_vr.ui.budget_left_ms = vg_vs_remaining_ms(s, now);
  g_vr.ui.result_left_ms = vg_vs_result_left_s(s, now) * 1000u;
  g_vr.ui.visible = (s->state != VG_VS_IDLE);
  snprintf(g_vr.ui.question, sizeof(g_vr.ui.question), "%s", s->text);
  /* s->answer holds the spoken summary; the overlay keeps the full display
   * text that was rendered from the same snapshot.  Only fall back to the
   * summary when no display text has been produced yet. */

  if (!s->have_answer)
    {
      g_vr.ui.answer[0] = '\0';
    }
  else if (g_vr.ui.answer[0] == '\0')
    {
      snprintf(g_vr.ui.answer, sizeof(g_vr.ui.answer), "%s", s->answer);
    }
  pthread_mutex_unlock(&g_vr.ui_lock);
}

static void vr_ui_error(const char *text)
{
  pthread_mutex_lock(&g_vr.ui_lock);
  snprintf(g_vr.ui.error, sizeof(g_vr.ui.error), "%s",
           text != NULL ? text : "");
  g_vr.ui.visible = true;
  pthread_mutex_unlock(&g_vr.ui_lock);
}

static void vr_ui_clear_error(void)
{
  pthread_mutex_lock(&g_vr.ui_lock);
  g_vr.ui.error[0] = '\0';
  pthread_mutex_unlock(&g_vr.ui_lock);
}

/* ── button callback ────────────────────────────────────────────── */

/* Runs in the button task.  Nothing here may block: the pipeline stages poll
 * the cancel flag, and the worker reads the pending event. */

static void vr_button(const struct vg_ptt_event_record *rec, void *arg)
{
  (void)arg;

  if (rec->type == VG_PTT_EV_BEGIN)
    {
      g_vr.asr_src[0] = '\0';
      g_vr.cancel = 1;
      g_vr.press_pending = true;
      sem_post(&g_vr.wake);
    }
  else if (rec->type == VG_PTT_EV_SUBMIT)
    {
      g_vr.submit_pending = true;
      sem_post(&g_vr.wake);
    }
}

/* ── stages ─────────────────────────────────────────────────────── */

/* Wait for the recorder to close the WAV.  Bounded: a stuck audio task must
 * not hold the whole round. */

static bool vr_wait_voice_idle(uint32_t limit_ms)
{
  return vg_voice_wait_idle(limit_ms);
}

/* Recognise the recording.  Returns true and fills text on success. */

static bool vr_stage_asr(char *text, size_t cap)
{
  struct vg_vc_info info;
  enum vg_vc_result rc;
  vela_req_ctl_t ctl;

  ctl.deadline_ms = g_vr.s.deadline_ms;
  ctl.cancel = &g_vr.cancel;

  while (vg_agent_round_voice_reserve() != 0)
    {
      if (g_vr.cancel) return false;
      if ((int32_t)(g_vr.s.deadline_ms - vr_now_ms()) <= 0)
        { vr_ui_error("等待查询超时，请重试"); return false; }
      usleep(20000);
    }

  vr_ui_set_status("识别中");
  vr_ui_publish();
  vr_log_stage("asr_begin");

  __atomic_fetch_add(&g_vr.asr_requests, 1, __ATOMIC_RELAXED);

  /* The recording is still in RAM: upload it from there.  A file is only
   * read when a caller named one, which is the diagnostic path. */

  {
    size_t bytes = 0;
    int16_t *pcm = NULL;
    if (g_vr.asr_src[0] != '\0')
      {
        uint32_t prep_t0 = vr_now_ms();

        (void)vg_voice_finish_owned(g_vr.clip_generation, true);
        syslog(LOG_INFO, "[vgvoice] clip_prep src=file bytes=0 ms=%u\n",
               (unsigned)(vr_now_ms() - prep_t0));
        rc = vg_voice_cloud_asr(g_vr.asr_src, &ctl, text, cap, &info);
      }
    else
      {
        uint32_t prep_t0 = vr_now_ms();
        int take_rc = vg_voice_clip_take_owned(g_vr.clip_generation,
                                               &pcm, &bytes);

        /* The take is the handoff the round waits on: recorder drain plus
         * the copy of up to 15.5 s of RAM audio.  bytes stays 0 when the
         * take failed, which the line then records as such. */
        syslog(LOG_INFO, "[vgvoice] clip_prep src=ram bytes=%u ms=%u\n",
               (unsigned)bytes, (unsigned)(vr_now_ms() - prep_t0));
        if (take_rc == 0)
          {
            rc = vg_voice_cloud_asr_buffer(pcm, bytes, &ctl, text, cap,
                                           &info);
            /* Volatile stores keep raw-audio cleanup before free
             * observable. */
            volatile unsigned char *wipe = (volatile unsigned char *)pcm;
            for (size_t i = 0; i < bytes; i++) wipe[i] = 0;
            free(pcm);
          }
        else
          {
            memset(&info, 0, sizeof(info));
            rc = VG_VC_IO;
          }
      }
  }

  syslog(LOG_INFO, "[vgvoice] asr rc=%s status=%d req=%u resp=%u ms=%u\n",
         vg_vc_result_name(rc), info.status, (unsigned)info.req_bytes,
         (unsigned)info.resp_bytes, (unsigned)info.elapsed_ms);
  vr_log_stage("asr_end");

  if (rc == VG_VC_CANCELLED)
    {
      return false;
    }

  if (rc != VG_VC_OK)
    {
      if (rc == VG_VC_EMPTY)
        {
          vr_ui_error("没有听清，请再说一次");
        }
      else if (rc == VG_VC_TIMEOUT)
        {
          vr_ui_error("识别超时，请重试");
        }
      else if (rc == VG_VC_HTTP)
        {
          vr_ui_error("识别服务返回错误，请重试");
        }
      else
        {
          vr_ui_error("识别失败，请重试");
        }

      return false;
    }

  return true;
}

/* Ask the on-board agent.  Returns true when the round finished inside the
 * budget and the answer is in display/speech. */

static bool vr_wait_agent(uint32_t deadline_ms)
{
  for (;;)
    {
      enum vg_agent_round_state st;

      /* Drive the channel here as well.  The HMI worker ticks every 100 ms,
       * but it can be busy with a report or advice write; a question that
       * sits QUEUED past the budget is indistinguishable from a model
       * timeout.  The submission guard keeps the two tickers from pushing
       * the same request twice. */

      vg_agent_round_tick();

      if (g_vr.cancel)
        {
          return false;
        }

      st = vg_agent_round_state();

      if (st == VG_AGENT_ROUND_DONE)
        {
          return true;
        }

      if (st == VG_AGENT_ROUND_ERROR)
        {
          return false;
        }

      if ((int32_t)(deadline_ms - vr_now_ms()) <= 0)
        {
          return false;
        }

      usleep(VR_AGENT_POLL_MS * 1000);
    }
}

/* The request is the recognized text plus, when it is still fresh, the
 * previous exchange.  Behaviour rules (read-only tools, side-by-side
 * candidates, the trailing spoken-summary line) live in the agent-side voice
 * scope prompt, not here: repeating them per request would only grow the
 * message and the injection surface.  Bounded to keep room for the question
 * inside the agent request buffer. */

#define VR_REQ_MAX 1152

static bool vr_ctx_valid(uint32_t now)
{
  int32_t age;

  if (g_vr.ctx_ms == 0)
    {
      return false;
    }

  age = (int32_t)(now - g_vr.ctx_ms);
  return age >= 0 && (uint32_t)age < VG_VS_IDLE_MS;
}

static bool vr_build_request(char *question, size_t qcap)
{
  const char *text = g_vr.s.text;
  size_t off = 0;
  int n;

  if (text == NULL || text[0] == '\0')
    {
      return false;
    }

  if (vr_ctx_valid(vr_now_ms()) &&
      (g_vr.ctx_question[0] != '\0' || g_vr.ctx_answer[0] != '\0'))
    {
      n = snprintf(question + off, qcap - off, "上一问:%s\n上一答:%s\n",
                   g_vr.ctx_question, g_vr.ctx_answer);
      if (n < 0 || (size_t)n >= qcap - off)
        {
          /* The context alone must never crowd out the question. */

          off = 0;
        }
      else
        {
          off += (size_t)n;
        }
    }

  n = snprintf(question + off, qcap - off, "用户本次提问:%s", text);
  return n > 0 && (size_t)n < qcap - off;
}

/* Keep a bounded, UTF-8-safe excerpt of the display answer for the next
 * follow-up.  The prefix copy backs off from a cut inside a character. */

static void vr_ctx_save(const char *question, const char *display)
{
  size_t n;

  snprintf(g_vr.ctx_question, sizeof(g_vr.ctx_question), "%s",
           question != NULL ? question : "");

  n = display != NULL ? strlen(display) : 0;
  if (n >= sizeof(g_vr.ctx_answer))
    {
      n = sizeof(g_vr.ctx_answer) - 1;
      while (n > 0 && ((unsigned char)display[n] & 0xC0) == 0x80)
        {
          n--;
        }
    }

  if (display != NULL)
    {
      memcpy(g_vr.ctx_answer, display, n);
    }

  g_vr.ctx_answer[n] = '\0';
  g_vr.ctx_ms = vr_now_ms();
}

static void vr_ctx_clear(void)
{
  g_vr.ctx_ms = 0;
  g_vr.ctx_question[0] = '\0';
  g_vr.ctx_answer[0] = '\0';
}

/* ── action execution ───────────────────────────────────────────── */

static void vr_log_settled(void)
{
  syslog(LOG_INFO, "[vgvoice] round_settled gen=%u state=%u failed=%u elapsed_ms=%u\n",
         (unsigned)g_vr.s.generation, (unsigned)g_vr.s.state,
         g_vr.s.speech_failed, (unsigned)(vr_now_ms() - g_vr.s.submit_ms));
}

static void vr_log_cancel_settled(void)
{
  uint32_t started=__atomic_load_n(&g_vr.end_requested_ms,__ATOMIC_RELAXED);
  if (!started || g_vr.s.state != VG_VS_IDLE || vg_voice_facts_request_active()) return;
  struct vg_voice_stats audio;
  vg_voice_get_stats(&audio);
  if (audio.state != VG_VOICE_IDLE || vg_voice_clip_bytes() != 0) return;
  __atomic_store_n(&g_vr.end_requested_ms,0,__ATOMIC_RELAXED);
  syslog(LOG_INFO,"[vgvoice] cancel_settled elapsed_ms=%u retained_pcm=0 controlled_active=0\n",
         (unsigned)(vr_now_ms()-started));
}

static void vr_show_result(void)
{
  vg_agent_round_voice_release();
  struct vg_voice_session *s = &g_vr.s;

  if (s->muted)
    syslog(LOG_INFO, "[vgvoice] text_ready gen=%u elapsed_ms=%u\n",
           (unsigned)s->generation, (unsigned)(vr_now_ms() - s->submit_ms));
  vr_ui_set_status(s->speech_failed ? "播报失败，可重试播报" : "");
  vr_ui_publish();
  vr_log_settled();
}

static void vr_show_error(void)
{
  vg_agent_round_voice_release();
  vr_ui_set_status("出错了");
  vr_ui_publish();
  vr_log_settled();
}

/* Ask the on-board agent.  The raw question is submitted with its deadline;
 * the reply is split into display text and spoken summary and validated
 * here.  A reply that does not pass the split is refused rather than shown,
 * because there is no snapshot left to render a fallback from. */

static uint32_t vr_stage_agent(const char *text, uint32_t gen)
{
  char question[VR_REQ_MAX];
  char display[VG_VOICE_DISPLAY_MAX];
  char speech[VG_VS_TEXT_MAX];
  uint32_t config_version = g_vr.s.config_version;
  const char *reply;
  size_t reply_len;
  uint32_t tool_base;
  bool split_ok;

  (void)text;   /* the session's recognized text is the source of record */
  vr_log_stage("agent_begin");

  if (!vr_build_request(question, sizeof(question)))
    {
      vr_ui_error("没有听清要问什么，请再说一次");
      return vg_vs_agent_result(&g_vr.s, gen, false, "empty question",
                                vr_now_ms());
    }

  /* Snapshot before the round: if the model answers with numbers but no vg
   * tool executed in between, the numbers have no board source and the
   * reply is refused rather than shown. */

  tool_base = vg_agent_tools_exec_count();

  for (;;)
    {
      int queued;

      if (g_vr.cancel)
        {
          return 0;
        }

      if ((int32_t)(g_vr.s.deadline_ms - vr_now_ms()) <= 0)
        {
          vr_ui_error("等待查询超时");
          return vg_vs_agent_result(&g_vr.s, gen, false,
                                    "agent queue timeout", vr_now_ms());
        }

      queued = vg_agent_round_queue_voice(question, g_vr.s.deadline_ms);
      if (queued == 0)
        {
          break;
        }

      if (queued != -EBUSY)
        {
          vr_ui_error("查询暂不可用，请稍后再试");
          return vg_vs_agent_result(&g_vr.s, gen, false,
                                    "agent unavailable", vr_now_ms());
        }

      /* Keep the one pending voice question within its original budget.
       * The background owner finishes and consumes its own result. */

      vr_ui_set_status("等待后台任务");
      vr_ui_publish();
      vg_agent_round_tick();
      usleep(VR_AGENT_POLL_MS * 1000u);
    }

  vr_ui_set_status("查询中");
  vr_ui_publish();
  vr_log_stage("agent_queued");

  vg_vs_agent_started(&g_vr.s, gen, vr_now_ms());

  if (!vr_wait_agent(g_vr.s.deadline_ms))
    {
      /* The round may still be running; its reply is discarded because the
       * generation no longer matches once this call returns an error. */

      if (g_vr.cancel)
        {
          return 0;
        }

      /* Give up the agent slot: otherwise the discarded round keeps the client
       * busy and the next question is refused for up to its 300 s timeout. */

      vg_agent_round_abandon_owned(VG_AGENT_ROUND_OWNER_VOICE);

      vr_ui_error((int32_t)(g_vr.s.deadline_ms - vr_now_ms()) <= 0
                    ? "查询超时" : "查询失败，请重试");
      return vg_vs_agent_result(&g_vr.s, gen, false, "agent failed",
                                vr_now_ms());
    }

  /* Read the reply before clear(): clearing a voice-owned result wipes the
   * reply buffer along with the request. */

  reply = vg_agent_round_last_reply();
  reply_len = reply != NULL ? strlen(reply) : 0;
  split_ok = vg_va_split_reply(reply, display, sizeof(display),
                               speech, sizeof(speech));
  vg_agent_round_clear();
  vr_log_stage("agent_reply");

  syslog(LOG_INFO, "[vgvoice] agent reply bytes=%u display=%u speech=%u split=%d\n",
         (unsigned)reply_len, (unsigned)strlen(display),
         (unsigned)strlen(speech), split_ok ? 1 : 0);

  if (g_vr.cancel)
    {
      return 0;
    }

  if ((int32_t)(g_vr.s.deadline_ms - vr_now_ms()) <= 0)
    {
      vr_ui_error("查询超时");
      return vg_vs_agent_result(&g_vr.s, gen, false, "agent timeout",
                                vr_now_ms());
    }

#ifdef CONFIG_VG_BUS_DISCOVER
  if (vg_live_points_gen() != config_version)
    {
      vr_ui_error("点表已变更，请重新提问");
      return vg_vs_agent_result(&g_vr.s, gen, false, "config changed",
                                vr_now_ms());
    }
#endif

  if (!split_ok)
    {
      vr_ui_error("AI 回答未能通过校验，请重试");
      return vg_vs_agent_result(&g_vr.s, gen, false, "reply invalid",
                                vr_now_ms());
    }

  {
    bool tools_ran = vg_agent_tools_exec_count() != tool_base;

    syslog(LOG_INFO, "[vgvoice] data basis tools_ran=%d numeric=%d\n",
           tools_ran ? 1 : 0,
           vg_va_numeric_present(display) ? 1 : 0);

    if (!tools_ran && vg_va_numeric_present(display))
      {
        vr_ui_error("回答缺少设备数据依据，请重试");
        return vg_vs_agent_result(&g_vr.s, gen, false, "no tool basis",
                                  vr_now_ms());
      }
  }

  /* The AI marker keeps the assistant's prose distinguishable from the
   * rule-based alarm page (AGENTS.md V5). */

  pthread_mutex_lock(&g_vr.ui_lock);
  snprintf(g_vr.ui.answer, sizeof(g_vr.ui.answer), "AI 回答\n%s", display);
  pthread_mutex_unlock(&g_vr.ui_lock);

  vr_ctx_save(g_vr.s.text, display);

  return vg_vs_agent_result(&g_vr.s, gen, true, speech, vr_now_ms());
}

#ifdef CONFIG_VG_VOICE_TTS_STREAM

/* Streaming playback adapter: the cloud sink hands over 24 kHz mono
 * pieces; the incremental resampler turns them into the 16 kHz mono the
 * ring player takes. The shared bound includes the resampler's carry
 * samples and keeps the adapter off the large-stack failure path. */

#define VR_TTS_FEED_SAMPLES VG_VC_RS_CHUNK_SAMPLES

struct vr_tts_feed
{
  struct vg_pcm_rs24_16 rs;
  int16_t out[VR_TTS_FEED_SAMPLES];
  uint32_t mono_bytes;
};

static int vr_tts_feed_push(void *ctx, const uint8_t *pcm24k, size_t bytes)
{
  struct vr_tts_feed *f = (struct vr_tts_feed *)ctx;
  long n = vg_pcm_rs24_16_feed(&f->rs, pcm24k, bytes, f->out,
                               VR_TTS_FEED_SAMPLES);

  if (n < 0)
    {
      return -1;
    }

  if (n > 0 && vg_tts_player_push((const uint8_t *)f->out,
                                  (size_t)n * 2u) != 0)
    {
      return -1;
    }

  if (n > 0) f->mono_bytes += (uint32_t)n * 2u;

  return 0;
}

/* 播放线程在 START 成功后记录启动时间，并通过加锁的 UI 字段更新状态。
 * 这表示驱动接受启动，不等同于外部声学测量；不修改轮次状态机。 */

static void vr_tts_started(void *arg)
{
  uint32_t gen = *(const uint32_t *)arg;

  vr_ui_set_status("播报中");

  syslog(LOG_INFO, "[vgvoice] first_playable gen=%u elapsed_ms=%u\n",
         (unsigned)gen, (unsigned)(vr_now_ms() - g_vr.s.submit_ms));
  vr_log_stage("play_start");
}

#endif /* CONFIG_VG_VOICE_TTS_STREAM */

static uint32_t vr_stage_tts(uint32_t gen)
{
  struct vg_vc_info info;
  enum vg_vc_result rc;
  vela_req_ctl_t ctl;

  ctl.deadline_ms = g_vr.s.deadline_ms;
  ctl.cancel = &g_vr.cancel;

  /* 重试播报也要暂停已恢复的后台请求，且沿用本次播报预算。 */
  while (vg_agent_round_voice_reserve() != 0)
    {
      if (g_vr.cancel) return 0;
      if ((int32_t)(g_vr.s.deadline_ms - vr_now_ms()) <= 0)
        return vg_vs_tts_result(&g_vr.s, gen, false, vr_now_ms());
      usleep(20000);
    }

  vr_ui_set_status("合成语音");
  vr_ui_publish();
  vr_log_stage("tts_begin");

#ifdef CONFIG_VG_VOICE_TTS_STREAM
  {
    uint32_t started_gen = gen;

    int player_rc = vg_tts_player_start(vg_tts_player_prebuf_ms(),
                                       ctl.deadline_ms, &g_vr.cancel,
                                       vr_tts_started, &started_gen);
    if (player_rc == 0)
      {
        struct vr_tts_feed feed;
        struct vg_tts_player_stats pstats;
        uint32_t actions;
        uint32_t limit;
        int wr;

        /* The span anchor the phase scripts read; the real first-sound
         * marker is the play_start stage the player thread logs. */

        vr_log_stage("play_posted");
        vr_ui_set_status("缓冲语音");
        vr_ui_publish();

        __atomic_fetch_add(&g_vr.tts_requests, 1, __ATOMIC_RELAXED);
        vg_pcm_rs24_16_init(&feed.rs);
        feed.mono_bytes = 0;
        rc = vg_voice_cloud_tts_stream(g_vr.s.answer, vr_tts_feed_push,
                                       &feed, &ctl, &info);

        syslog(LOG_INFO,
               "[vgvoice] tts rc=%s status=%d pcm=%u resp=%u ms=%u "
               "stream=1 delivered_partial=%u\n",
               vg_vc_result_name(rc), info.status,
               (unsigned)info.audio_bytes, (unsigned)info.resp_bytes,
               (unsigned)info.elapsed_ms, (unsigned)info.partial);
        vr_log_stage("tts_end");

        if (rc != VG_VC_OK)
          {
            /* Truncation, cancellation or transport failure: stop the
             * device right away and report a failed playback; whatever
             * was audible is already marked partial above. */

            vg_tts_player_abort();
            (void)vg_tts_player_wait(3000, &pstats);
            syslog(LOG_INFO, "[vgvoice] tts_playback_failure started=%u "
                   "partial=%u failed=%u err=%d leaked=%u\n",
                   pstats.started, pstats.started, pstats.failed,
                   pstats.err, pstats.leaked_buffers);

            if (g_vr.cancel)
              {
                return 0;
              }

            return vg_vs_tts_result(&g_vr.s, gen, false, vr_now_ms());
          }

        vg_tts_player_eof();
        actions = vg_vs_tts_result(&g_vr.s, gen, true, vr_now_ms());

        /* Drain: everything pushed has to finish.  The bound is the
         * audio duration itself (32 mono bytes per ms) plus the same
         * slack the WAV wait uses; completion comes from the driver. */

        limit = (feed.mono_bytes + 31u) / 32u + VR_PLAY_EXTRA_MS;
        wr = vg_tts_player_wait(limit, &pstats);

        info.played_bytes = pstats.played_bytes;
        info.underruns = pstats.underruns;
        info.stall_ms = pstats.stall_ms;

        syslog(LOG_INFO,
               "[vgvoice] tts_player played=%u pushed=%u underruns=%u "
               "stall_ms=%u restarts=%u complete=%u failed=%u err=%d "
               "leaked=%u ms=%u returned=%u ready_delay_ms=%u\n",
               (unsigned)pstats.played_bytes,
               (unsigned)pstats.pushed_bytes,
               (unsigned)pstats.underruns, (unsigned)pstats.stall_ms,
               (unsigned)pstats.restarts, pstats.complete ? 1u : 0u,
               pstats.failed ? 1u : 0u, pstats.err,
               (unsigned)pstats.leaked_buffers,
               (unsigned)(vr_now_ms() - g_vr.s.submit_ms),
               (unsigned)pstats.returned_bytes,
               (unsigned)pstats.start_delay_ms);

        if (g_vr.cancel)
          {
            return 0;
          }

        if (wr != 0 || !pstats.complete || pstats.failed)
          {
            actions |= vg_vs_tts_result(&g_vr.s, gen, false, vr_now_ms());
          }
        else
          {
            vr_log_stage("play_done");
            actions |= vg_vs_play_done(&g_vr.s, gen, vr_now_ms());
          }

        return actions;
      }
    if (player_rc == -EBUSY)
      {
        /* 未回收的播放会话仍持有设备，不能让 WAV 回退抢占同一设备。 */
        return vg_vs_tts_result(&g_vr.s, gen, false, vr_now_ms());
      }
  }

  /* The player could not open the device: this round falls back to the
   * WAV path below.  The decision happens before the request is sent,
   * so no synthesised audio is lost. */
#endif /* CONFIG_VG_VOICE_TTS_STREAM */

  __atomic_fetch_add(&g_vr.tts_requests, 1, __ATOMIC_RELAXED);
  rc = vg_voice_cloud_tts(g_vr.s.answer, VG_VC_TTS_WAV_PATH, &ctl, &info);

  syslog(LOG_INFO, "[vgvoice] tts rc=%s status=%d pcm=%u resp=%u ms=%u\n",
         vg_vc_result_name(rc), info.status, (unsigned)info.audio_bytes,
         (unsigned)info.resp_bytes, (unsigned)info.elapsed_ms);
  vr_log_stage("tts_end");

  if (g_vr.cancel)
    {
      unlink(VG_VC_TTS_WAV_PATH);
      return 0;
    }

  if (rc != VG_VC_OK)
    {
      /* The text is still valid; only playback failed. */

      return vg_vs_tts_result(&g_vr.s, gen, false, vr_now_ms());
    }

  if (g_vr.cancel)
    {
      unlink(VG_VC_TTS_WAV_PATH);
      return 0;
    }

  if ((int32_t)(g_vr.s.deadline_ms - vr_now_ms()) <= 0 ||
      vg_voice_play_file(VG_VC_TTS_WAV_PATH) != 0)
    {
      unlink(VG_VC_TTS_WAV_PATH);
      return vg_vs_tts_result(&g_vr.s, gen, false, vr_now_ms());
    }

  syslog(LOG_INFO, "[vgvoice] first_playable gen=%u elapsed_ms=%u\n",
         (unsigned)gen, (unsigned)(vr_now_ms() - g_vr.s.submit_ms));
  vr_log_stage("play_posted");
  vr_ui_set_status("播报中");
  vr_ui_publish();

  {
    uint32_t actions = vg_vs_tts_result(&g_vr.s, gen, true, vr_now_ms());
    uint32_t limit = 60000;
    struct stat st;

    if (stat(VG_VC_TTS_WAV_PATH, &st) == 0 && st.st_size > 44)
      {
        /* 16 kHz stereo 16-bit: 64000 bytes per second. */

        limit = (uint32_t)((st.st_size - 44) / 64) + VR_PLAY_EXTRA_MS;
      }

    bool idle = false;
    uint32_t started = vr_now_ms();
    while (!g_vr.cancel && vr_now_ms() - started < limit)
      {
        if ((idle = vr_wait_voice_idle(100))) break;
      }
    bool completed = idle;
    if (!idle)
      {
        vg_voice_cancel();
        idle = vr_wait_voice_idle(2000);
      }
    if (idle) unlink(VG_VC_TTS_WAV_PATH);
    if (g_vr.cancel) return 0;

    struct vg_voice_stats played;
    vg_voice_get_stats(&played);
    if (!completed || !idle || played.last_result != VG_VOICE_OK)
      actions |= vg_vs_tts_result(&g_vr.s, gen, false, vr_now_ms());
    else
      {
        /* The explicit playback-end marker the round metrics are read
         * from; the settle below it is a fallback with coarser
         * quantisation. */
        vr_log_stage("play_done");
        actions |= vg_vs_play_done(&g_vr.s, gen, vr_now_ms());
      }
    return actions;
  }
}

/* ── action loop ────────────────────────────────────────────────── */

#ifdef CONFIG_AI_AGENT_VOICE_CONN_SLOT

/* P2 phase 1: warm the ASR/TTS keep-alive connection while the user is
 * still speaking.  The handshake is the deep part (mbedTLS peaks around
 * 12 KB of stack; 24 KB leaves margin), so this gets its own thread —
 * the round thread is busy owning the recorder.  Never joined: the
 * transport's own deadline and cancel checks bound its life, and the
 * single-flight guard keeps one thread at a time.  A cancel observed
 * mid-handshake destroys the half-built connection; one that finished
 * before the cancel stays published and only helps the next round. */

static volatile int g_preconnect_running;

static void *vr_preconnect_thread(void *arg)
{
  vela_req_ctl_t ctl;
  agent_llm_timing_t t;
  uint32_t t0;

  (void)arg;
  ctl.deadline_ms = vr_now_ms() + 8000u;
  ctl.cancel = &g_vr.cancel;

  agent_timing_reset(&t);
  t0 = vr_now_ms();
  int rc = agent_http_voice_preconnect(&ctl, &t);

  syslog(LOG_INFO, "[vgvoice] voice_preconnect rc=%d dns=%d tls=%d ms=%u\n",
         rc, (int)t.dns_tcp_ms, (int)t.tls_ms,
         (unsigned)(vr_now_ms() - t0));

  g_preconnect_running = 0;
  return NULL;
}

static void vr_preconnect_start(void)
{
  pthread_attr_t attr;
  pthread_t tid;
  int expected = 0;

  if (!__atomic_compare_exchange_n(&g_preconnect_running, &expected, 1,
                                   false, __ATOMIC_ACQ_REL,
                                   __ATOMIC_RELAXED))
    {
      return;   /* one in flight */
    }

  pthread_attr_init(&attr);
  pthread_attr_setstacksize(&attr, 24576);

  if (pthread_create(&tid, &attr, vr_preconnect_thread, NULL) != 0)
    {
      g_preconnect_running = 0;
      return;
    }

  pthread_detach(tid);
}

#endif /* CONFIG_AI_AGENT_VOICE_CONN_SLOT */

static void vr_run_actions(uint32_t actions)
{
  int guard;

  for (guard = 0; guard < 8 && actions != 0; guard++)
    {
      uint32_t next = 0;

      if ((actions & VG_VS_ACT_STOP_PLAYBACK) != 0)
        {
          (void)vg_voice_cancel();
          if (vr_wait_voice_idle(2000))
            {
              next |= vg_vs_audio_stopped(&g_vr.s, vr_now_ms());
            }
          else
            {
              vr_ui_error("播放未能停止");
              next |= vg_vs_end(&g_vr.s, vr_now_ms());
            }
        }

      if ((actions & VG_VS_ACT_CANCEL_AGENT) != 0)
        {
          /* A new press replaced the question.  Release the agent slot so
           * the replacement can be submitted; the old reply is dropped by
           * its serial. */

          vg_agent_round_abandon_owned(VG_AGENT_ROUND_OWNER_VOICE);
        }

      if ((actions & VG_VS_ACT_STOP_RECORD) != 0)
        {
          if (g_vr.record_generation != 0)
            {
              (void)vg_voice_finish_owned_silent(g_vr.record_generation);
              g_vr.record_generation = 0;
            }
          /* 正常松手和到达录音上限会同时请求 START_ASR，必须保留 PCM
           * 和句柄供识别取用。取消或录音过短没有后续取用，才清理。 */
          if ((actions & VG_VS_ACT_START_ASR) == 0 &&
              g_vr.clip_generation != 0)
            {
              (void)vg_voice_finish_owned(g_vr.clip_generation, true);
              g_vr.clip_generation = 0;
            }
        }

      if ((actions & VG_VS_ACT_START_RECORD) != 0)
        {
          uint32_t gen = 0;
          int rc;

          /* A previous round may still be writing its WAV.  Starting a
           * recording on top of that produced a phantom capture and then
           * "录音未正常结束" at the next stage; wait for the worker to settle
           * first and fail with a clear reason if it does not. */

          if (!vg_voice_wait_idle(2000))
            {
              vr_ui_error("录音设备忙，请稍后再试");
              next |= vg_vs_end(&g_vr.s, vr_now_ms());
            }
          else if ((rc = vg_voice_record_begin_owned(&gen)) < 0)
            {
              vr_ui_error("录音启动失败");
              next |= vg_vs_end(&g_vr.s, vr_now_ms());
            }
          else
            {
              g_vr.cancel = g_vr.end_pending || g_vr.press_pending || g_vr.quit;
              g_vr.record_generation = gen;
              g_vr.clip_generation = gen;
              vr_ui_clear_error();
              vr_ui_set_status("录音中");

#ifdef CONFIG_AI_AGENT_VOICE_CONN_SLOT
              /* The round thread will be busy with the recorder until
               * release; warm the connection it needs right after. */

              vr_preconnect_start();
#endif

              pthread_mutex_lock(&g_vr.ui_lock);
              g_vr.ui.answer[0] = '\0';
              g_vr.ui.error[0] = '\0';
              pthread_mutex_unlock(&g_vr.ui_lock);
            }
        }

      if ((actions & VG_VS_ACT_START_ASR) != 0)
        {
          uint32_t gen = g_vr.s.generation;
          char text[VG_VS_TEXT_MAX];

          text[0] = '\0';

          if (g_vr.cancel) return;

          /* The recorder stops the PDM stream, drains and writes the WAV;
           * 2 s was tight enough to lose a round right after playback. */

          vr_log_stage("asr_wait_recorder");
          if (!vr_wait_voice_idle(5000))
            {
              vr_ui_error("录音未正常结束");
              next |= vg_vs_asr_result(&g_vr.s, gen, false, NULL,
                                       vr_now_ms());
            }
          else if (!vr_stage_asr(text, sizeof(text)))
            {
              if (!g_vr.cancel)
                {
                  next |= vg_vs_asr_result(&g_vr.s, gen, false, NULL,
                                           vr_now_ms());
                }
            }
          else
            {
              next |= vg_vs_asr_result(&g_vr.s, gen, true, text,
                                       vr_now_ms());
            }
        }

      if ((actions & VG_VS_ACT_QUEUE_AGENT) != 0)
        {
          if (g_vr.cancel) return;
          next |= vr_stage_agent(g_vr.s.text, g_vr.s.generation);
        }

      if ((actions & VG_VS_ACT_START_TTS) != 0)
        {
          if (g_vr.cancel) return;
          next |= vr_stage_tts(g_vr.s.generation);
        }

      if ((actions & VG_VS_ACT_SHOW_RESULT) != 0)
        {
          vr_show_result();
        }

      if ((actions & VG_VS_ACT_SHOW_ERROR) != 0)
        {
          vr_show_error();
        }

      if ((actions & VG_VS_ACT_HIDE_OVERLAY) != 0)
        {
          vg_agent_round_voice_release();
          pthread_mutex_lock(&g_vr.ui_lock);
          g_vr.ui.visible = false;
          g_vr.ui.status[0] = '\0';
          g_vr.ui.error[0] = '\0';
          g_vr.ui.answer[0] = '\0';
          g_vr.ui.question[0] = '\0';
          pthread_mutex_unlock(&g_vr.ui_lock);
        }

      if ((actions & VG_VS_ACT_RELEASE_AUDIO) != 0)
        {
          if (g_vr.clip_generation)
            (void)vg_voice_finish_owned(g_vr.clip_generation, true);
          vg_voice_cloud_cleanup();
          if (vg_voice_wait_idle(0)) unlink(VG_VC_TTS_WAV_PATH);
        }

      actions = next;
    }

  vr_ui_publish();
}

/* ── worker ─────────────────────────────────────────────────────── */

static void *vr_thread(void *arg)
{
  (void)arg;

  while (!g_vr.quit)
    {
      struct timespec ts;
      uint32_t actions;
      uint32_t now;

      clock_gettime(CLOCK_REALTIME, &ts);
      ts.tv_nsec += VR_TICK_MS * 1000000L;
      if (ts.tv_nsec >= 1000000000L)
        {
          ts.tv_sec += 1;
          ts.tv_nsec -= 1000000000L;
        }

      (void)sem_timedwait(&g_vr.wake, &ts);

      now = vr_now_ms();

      if (g_vr.press_pending)
        {
          g_vr.press_pending = false;
          actions = vg_vs_press(&g_vr.s, now);
          vr_run_actions(actions);
          now = vr_now_ms();
        }

      if (g_vr.submit_pending)
        {
          g_vr.submit_pending = false;
          actions = vg_vs_release(&g_vr.s, now);
          vr_run_actions(actions);
          now = vr_now_ms();
        }

      /* Overlay commands arrive from the LVGL thread.  They only set a flag
       * here: the worker runs the pipeline, so a retry that needs 30 s of
       * network never blocks the UI. */

      if (g_vr.end_pending)
        {
          g_vr.end_pending = false;
          vr_run_actions(vg_vs_end(&g_vr.s, now));
          now = vr_now_ms();
        }

      if (g_vr.retry_pending)
        {
          g_vr.retry_pending = false;
          g_vr.cancel = g_vr.end_pending || g_vr.press_pending || g_vr.quit;
          vr_run_actions(vg_vs_retry_tts(&g_vr.s, now));
          now = vr_now_ms();
        }

      /* The session belongs to this thread.  The overlay's hold and mute
       * switches arrive as pending values so no other thread mutates it. */

      if (g_vr.hold_pending)
        {
          g_vr.hold_pending = false;
          g_vr.hold = g_vr.hold_value;
          vg_vs_result_hold(&g_vr.s, g_vr.hold, now);
        }

      if (g_vr.mute_pending)
        {
          g_vr.mute_pending = false;
          g_vr.muted = g_vr.mute_value;
          vg_vs_set_muted(&g_vr.s, g_vr.mute_value);
        }

#ifdef CONFIG_VG_BUS_DISCOVER
      /* A committed point-table change bumps the live generation.  Feed it to
       * the session so an in-flight question ends instead of answering from a
       * table that may no longer contain the points it names, and drop the
       * follow-up context built against the old table. */

      {
        uint32_t live_gen = vg_live_points_gen();

        if (live_gen != g_vr.s.config_version)
          {
            vr_ctx_clear();
          }
      }

      vg_vs_set_config_version(&g_vr.s, vg_live_points_gen());
#endif

      actions = vg_vs_tick(&g_vr.s, now);
      vr_run_actions(actions);
      vr_log_cancel_settled();

      if (g_vr.s.state == VG_VS_RESULT && !g_vr.hold)
        {
          /* Nothing to do: the session owns the 15 s timer. */
        }
    }

  return NULL;
}

/* ── public ─────────────────────────────────────────────────────── */

int vg_voice_round_start(void)
{
  int ret;

  pthread_mutex_lock(&g_start_lock);

  if (g_vr.started)
    {
      pthread_mutex_unlock(&g_start_lock);
      return 0;
    }

  memset(&g_vr, 0, sizeof(g_vr));
  pthread_mutex_init(&g_vr.ui_lock, NULL);
  sem_init(&g_vr.wake, 0, 0);
  vg_vs_init(&g_vr.s, vr_now_ms());
  g_vr.s.muted = false;

  ret = vg_voice_init();
  if (ret < 0)
    {
      pthread_mutex_unlock(&g_start_lock);
      return ret;
    }

  {
    pthread_attr_t attr;

    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, VR_STACK_SIZE);
    ret = pthread_create(&g_vr.thread, &attr, vr_thread, NULL);
    pthread_attr_destroy(&attr);
  }

  if (ret != 0)
    {
      pthread_mutex_unlock(&g_start_lock);
      return -ret;
    }

  /* The button layer starts after the worker exists, so an early press is
   * never dropped on the floor. */

  ret = vg_ptt_start(NULL, vr_button, NULL);
  if (ret < 0)
    {
      g_vr.quit = true;
      sem_post(&g_vr.wake);
      pthread_join(g_vr.thread, NULL);
      pthread_mutex_unlock(&g_start_lock);
      return ret;
    }

  g_vr.started = true;
  g_round_ready = true;
  pthread_mutex_unlock(&g_start_lock);
  return 0;
}

void vg_voice_round_stop(void)
{
  pthread_mutex_lock(&g_start_lock);
  if (!g_vr.started)
    {
      pthread_mutex_unlock(&g_start_lock);
      return;
    }

  g_vr.cancel = 1;
  g_vr.quit = true;
  vg_agent_round_abandon_owned(VG_AGENT_ROUND_OWNER_VOICE);
  sem_post(&g_vr.wake);
  pthread_join(g_vr.thread, NULL);
  g_vr.started = false;
  g_round_ready = false;
  pthread_mutex_unlock(&g_start_lock);

}

int vg_voice_round_stack(size_t *size, size_t *used, size_t *headroom)
{
#ifdef CONFIG_STACK_COLORATION
  struct tcb_s *tcb;
  size_t total;
  size_t highwater;

  pthread_mutex_lock(&g_start_lock);
  if (!g_vr.started)
    {
      pthread_mutex_unlock(&g_start_lock);
      return -ENOENT;
    }

  tcb = nxsched_get_tcb(pthread_gettid_np(g_vr.thread));
  if (tcb == NULL)
    {
      pthread_mutex_unlock(&g_start_lock);
      return -ESRCH;
    }

  /* NuttX colors the stack before starting the thread.  Its adjusted base
   * excludes TLS and alignment, so headroom counts usable stack only.
   * A TCB reference plus the lifecycle lock keeps the stack alive here. */

  total = tcb->adj_stack_size;
  highwater = up_check_tcbstack(tcb, total);
  nxsched_put_tcb(tcb);
  pthread_mutex_unlock(&g_start_lock);

  if (size != NULL)
    {
      *size = total;
    }

  if (used != NULL)
    {
      *used = highwater;
    }

  if (headroom != NULL)
    {
      *headroom = total - highwater;
    }

  return 0;
#else
  return -ENOSYS;
#endif
}

int vg_voice_round_counts(uint32_t *asr, uint32_t *tts)
{
  pthread_mutex_lock(&g_start_lock);
  if (!g_vr.started)
    {
      pthread_mutex_unlock(&g_start_lock);
      return -ENOENT;
    }
  if (asr) *asr = __atomic_load_n(&g_vr.asr_requests, __ATOMIC_RELAXED);
  if (tts) *tts = __atomic_load_n(&g_vr.tts_requests, __ATOMIC_RELAXED);
  pthread_mutex_unlock(&g_start_lock);
  return 0;
}

bool vg_voice_round_ui(struct vg_voice_ui *out)
{
  if (out == NULL || !g_round_ready)
    {
      return false;
    }

  pthread_mutex_lock(&g_vr.ui_lock);
  *out = g_vr.ui;
  pthread_mutex_unlock(&g_vr.ui_lock);
  return true;
}

void vg_voice_round_set_muted(bool muted)
{
  g_vr.muted = muted;

  if (!g_vr.started)
    {
      return;
    }

  g_vr.mute_value = muted;
  g_vr.mute_pending = true;
  sem_post(&g_vr.wake);
}

bool vg_voice_round_muted(void)
{
  return g_vr.muted;
}

void vg_voice_round_hold(bool held)
{
  if (!g_vr.started)
    {
      return;
    }

  g_vr.hold_value = held;
  g_vr.hold_pending = true;
  sem_post(&g_vr.wake);
}

void vg_voice_round_press_sim(bool press)
{
  if (!g_vr.started)
    {
      return;
    }

  if (press)
    {
      g_vr.cancel = 1;
      g_vr.press_pending = true;
    }
  else
    {
      g_vr.submit_pending = true;
    }

  sem_post(&g_vr.wake);
}

void vg_voice_round_set_asr_source(const char *path)
{
  if (path == NULL)
    {
      g_vr.asr_src[0] = '\0';
      return;
    }

  snprintf(g_vr.asr_src, sizeof(g_vr.asr_src), "%s", path);
}

void vg_voice_round_end(void)
{
  if (!g_vr.started)
    {
      return;
    }

  /* A manual end closes the conversation: the next question starts without
   * the previous exchange.  An automatic result-hide does not. */

  vr_ctx_clear();
  uint32_t empty=0, now=vr_now_ms();
  __atomic_compare_exchange_n(&g_vr.end_requested_ms,&empty,now?now:1,false,
                              __ATOMIC_RELAXED,__ATOMIC_RELAXED);
  g_vr.cancel = 1;
  g_vr.end_pending = true;
  sem_post(&g_vr.wake);
}

void vg_voice_round_retry_tts(void)
{
  if (!g_vr.started)
    {
      return;
    }

  g_vr.retry_pending = true;
  sem_post(&g_vr.wake);
}

#endif /* CONFIG_VG_VOICE_LOOPBACK && CONFIG_EXAMPLES_AI_AGENT_VELA */
