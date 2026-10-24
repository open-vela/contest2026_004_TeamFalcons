/****************************************************************************
 * app/velaguard/vg_voice_round.c
 *
 * BVO-11/12: the voice round from the button to the speaker.
 *
 * One worker thread owns the pipeline.  The button callback only posts an
 * event and raises the cancel flag, so a press during recognition, waiting or
 * playback is noticed within one transport slice rather than at the end of
 * the stage.
 *
 * The answer shown and the answer spoken come from the snapshot the agent's
 * read-only tool published, not from the model's prose.  The model still runs
 * the round and calls the tool; its text is used only when every number in it
 * appears in that snapshot.
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
#include "vg_agent_query.h"
#include "vg_agent_round.h"
#ifdef CONFIG_VG_BUS_DISCOVER
#  include "vg_discover.h"
#endif
#include "vg_agent_tools.h"
#include "vg_ptt_board.h"
#include "vg_voice.h"
#include "vg_voice_answer.h"
#include "vg_voice_cloud.h"
#include "vg_voice_facts.h"
#include "vg_voice_intent.h"
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
  syslog(LOG_INFO, "[vgvoice] stage=%s t=%u\n", stage,
         (unsigned)vr_elapsed_ms());
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

/* Build the question and the deterministic answer for one recognised text.
 * Returns false with a user-facing reason when the request cannot be
 * answered inside the three-intent contract. */

static bool vr_build_question(const char *text, char *question, size_t qcap,
                              char *tool, size_t tcap,
                              char *tool_json, size_t jcap,
                              char *target_id, size_t icap,
                              char *reason, size_t rcap)
{
  struct vg_voice_intent_result intent;
  struct vg_agent_point points[VG_AGENT_QUERY_MAX];
  struct vg_agent_query_result res;
  char token[VG_AGENT_QUERY_STR_MAX];
  const char *point_id;
  int n = 0;
  int rc;

  if (target_id && icap > 0)
    {
      target_id[0] = '\0';
    }

  if (vg_vi_classify(text, &intent) == VG_VI_REFUSED)
    {
      snprintf(reason, rcap, "语音只能查询当前异常、点位读数和告警恢复");
      return false;
    }

  if (intent.intent == VG_VI_UNKNOWN)
    {
      snprintf(reason, rcap, "没有听清要查什么，请再说一次");
      return false;
    }

  if (intent.intent == VG_VI_ANOMALY)
    {
      snprintf(question, qcap,
               "现在有哪些异常？请调用 vg_anomaly_read 获取完整列表，"
               "然后简要说明。");
      snprintf(tool, tcap, "vg_anomaly_read");
      snprintf(tool_json, jcap, "{}");
      return true;
    }

  rc = vg_agent_tools_collect(points, VG_AGENT_QUERY_MAX, &n);
  if (rc != VG_AGENT_QUERY_OK && rc != VG_AGENT_QUERY_NO_SAMPLE)
    {
      snprintf(reason, rcap, "已确认点表不可读，无法查询");
      return false;
    }

  if (n <= 0)
    {
      snprintf(reason, rcap, "还没有已确认点位");
      return false;
    }

  rc = vg_vi_resolve_point(text, points, n, &res, token, sizeof(token));

  if (rc == VG_AGENT_QUERY_AMBIG)
    {
      snprintf(reason, rcap, "有多个点位匹配，请报点位 ID");
      return false;
    }

  if (rc == VG_AGENT_QUERY_NOT_FOUND && vg_vi_is_followup(text))
    {
      point_id = vg_vs_context_point(&g_vr.s);
      if (point_id == NULL || point_id[0] == '\0')
        {
          snprintf(reason, rcap, "请先报点位 ID 或完整名称");
          return false;
        }
    }
  else if (rc == VG_AGENT_QUERY_OK)
    {
      point_id = points[res.index].id;
      vg_vs_set_context_point(&g_vr.s, point_id, vr_now_ms());
    }
  else
    {
      snprintf(reason, rcap, "没有找到这个点位，请报点位 ID 或完整名称");
      return false;
    }

  if (target_id && icap > 0)
    {
      snprintf(target_id, icap, "%s", point_id);
    }

  if (intent.intent == VG_VI_RECOVERY)
    {
      snprintf(question, qcap,
               "查询点位 %s 的告警是否已经恢复，请调用 vg_alarm_recovery。",
               point_id);
      snprintf(tool, tcap, "vg_alarm_recovery");
      snprintf(tool_json, jcap, "{\"point_id\":\"%s\"}",
               point_id);
    }
  else
    {
      snprintf(question, qcap,
               "查询点位 %s 的当前读数，请调用 vg_point_read。",
               point_id);
      snprintf(tool, tcap, "vg_point_read");
      snprintf(tool_json, jcap, "{\"query\":\"%s\"}",
               point_id);
    }

  return true;
}

/* Both display and speech use the private snapshot captured for this read. */
static bool vr_render_answer(const struct vg_voice_facts *f, char *display,
                             size_t dcap, char *speech, size_t scap)
{
  if (f == NULL || f->seq == 0) return false;
  display[0] = '\0';
  speech[0] = '\0';
  if (f->has_recovery)
    vg_va_render_recovery(f, display, dcap, speech, scap);
  else if (f->has_point)
    vg_va_render_point(f, display, dcap, speech, scap);
  else
    vg_va_render_anomalies(f, display, dcap, speech, scap);
  return true;
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

/* Execute the resolved read-only tool and render the answer from the
 * facts it publishes.  The tool runs through the agent tool registry, so
 * the same permission check, rate limit and audit path apply as in a
 * ReAct round, and the answer never comes from the model text. */

static uint32_t vr_answer_from_tool(uint32_t gen, const char *tool,
                                    const char *tool_json,
                                    uint32_t config_version)
{
  char display[VG_VOICE_DISPLAY_MAX];
  char speech[VG_VS_TEXT_MAX];
  struct vg_voice_facts facts;
  char out[2048];
  bool captured;
  int rc;

  if (!vg_voice_facts_capture_begin(&facts))
    {
      vr_ui_error("数据查询忙，请重试");
      return vg_vs_agent_result(&g_vr.s, gen, false, "facts busy",
                                vr_now_ms());
    }

  out[0] = '\0';
  rc = tool_registry_execute(tool, tool_json, out, sizeof(out));
  captured = vg_voice_facts_capture_end();
  syslog(LOG_INFO, "[vgvoice] intent read %s rc=%d captured=%d seq=%u\n",
         tool, rc, captured, (unsigned)facts.seq);
  vr_log_stage("facts_end");
  if (g_vr.cancel) return 0;

#ifdef CONFIG_VG_BUS_DISCOVER
  if (vg_live_points_gen() != config_version)
    {
      vr_ui_error("点表已变更，请重新提问");
      return vg_vs_agent_result(&g_vr.s, gen, false, "config changed",
                                vr_now_ms());
    }
#endif

  facts.config_version = config_version;
  if (rc != 0 || !captured ||
      (int32_t)(g_vr.s.deadline_ms - vr_now_ms()) <= 0 ||
      !vr_render_answer(&facts, display, sizeof(display), speech,
                         sizeof(speech)))
    {
      vr_ui_error("没有拿到可核对的数据，请重试");
      return vg_vs_agent_result(&g_vr.s, gen, false, "no facts",
                                vr_now_ms());
    }

  /* The session keeps only a short copy for its own bookkeeping; the full
   * list is published to the overlay separately. */

  pthread_mutex_lock(&g_vr.ui_lock);
  snprintf(g_vr.ui.answer, sizeof(g_vr.ui.answer), "%s", display);
  pthread_mutex_unlock(&g_vr.ui_lock);

  return vg_vs_agent_result(&g_vr.s, gen, true, speech, vr_now_ms());
}

static uint32_t vr_stage_agent(const char *text, uint32_t gen)
{
  char question[320];
  char reason[128];
  char tool[32];
  char tool_json[96];
  char target_id[48];
  char display[VG_VOICE_DISPLAY_MAX];
  char speech[VG_VS_TEXT_MAX];
  struct vg_voice_facts agent_facts;
  bool capturing = false;
  bool captured = false;
  uint32_t config_version = g_vr.s.config_version;
  tool[0] = '\0';
  tool_json[0] = '\0';
  target_id[0] = '\0';
  vr_log_stage("agent_begin");

  if (!vr_build_question(text, question, sizeof(question), tool, sizeof(tool),
                         tool_json, sizeof(tool_json), target_id,
                         sizeof(target_id), reason, sizeof(reason)))
    {
      vr_ui_error(reason);
      return vg_vs_agent_result(&g_vr.s, gen, false, reason, vr_now_ms());
    }

#ifdef CONFIG_VG_VOICE_DIRECT_TOOL
  /* The intent and the exact point are already resolved here, and the
   * answer is rendered from the published facts, so a model round would
   * only repeat that choice while spending 5 to 9 s of the 30 s budget
   * on the request.  The read-only tool runs directly instead. */

  vr_ui_set_status("查询中");
  vr_ui_publish();
  vg_vs_agent_started(&g_vr.s, gen, vr_now_ms());
  vr_log_stage("tool_direct");

  return vr_answer_from_tool(gen, tool, tool_json, config_version);
#else

  for (;;)
    {
      int queued;
      if (g_vr.cancel)
        {
          if (capturing)
            {
              vg_voice_facts_capture_end();
            }
          return 0;
        }
      if ((int32_t)(g_vr.s.deadline_ms - vr_now_ms()) <= 0)
        {
          if (capturing)
            {
              vg_voice_facts_capture_end();
            }
          vr_ui_error("等待查询超时");
          return vg_vs_agent_result(&g_vr.s, gen, false,
                                    "agent queue timeout", vr_now_ms());
        }
      queued = vg_agent_round_queue_voice(question, g_vr.s.deadline_ms);
      if (queued == 0)
        {
          vg_voice_facts_set_config_version(config_version);
          capturing = vg_voice_facts_capture_request(&agent_facts,
                          vg_agent_round_voice_serial(), config_version);
          break;
        }
      if (queued != -EBUSY)
        {
          if (capturing)
            {
              vg_voice_facts_capture_end();
            }
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
      if (capturing)
        {
          vg_voice_facts_capture_end();
        }

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

  {
    vg_agent_round_clear();
    vr_log_stage("agent_reply");
    if (capturing)
      {
        captured = vg_voice_facts_capture_end();
      }
    if (g_vr.cancel) return 0;
    if ((int32_t)(g_vr.s.deadline_ms - vr_now_ms()) <= 0)
      {
        vr_ui_error("查询超时");
        return vg_vs_agent_result(&g_vr.s, gen, false, "agent timeout",
                                  vr_now_ms());
      }
  }

  /* If the agent tool invocation already published matching facts, reuse
   * them directly to save 400-800 ms of redundant tool execution. */

  bool facts_match = false;
  if (captured && agent_facts.seq != 0)
    {
      if (strcmp(tool, "vg_anomaly_read") == 0)
        {
          facts_match = agent_facts.anomaly_seen > 0;
        }
      else if (strcmp(tool, "vg_alarm_recovery") == 0)
        {
          facts_match = agent_facts.has_recovery &&
                        agent_facts.recovery.kind == VG_RUNTIME_KIND_THRESHOLD &&
                        strcmp(agent_facts.recovery.id, target_id) == 0;
        }
      else if (strcmp(tool, "vg_point_read") == 0)
        {
          facts_match = agent_facts.has_point &&
                        strcmp(agent_facts.point_id, target_id) == 0;
        }
    }

  if (facts_match)
    {
      syslog(LOG_INFO, "[vgvoice] reused agent facts for %s seq=%u\n",
             tool, (unsigned)agent_facts.seq);
      syslog(LOG_INFO, "[vgvoice] intent read %s rc=0 captured=1 seq=%u\n",
             tool, (unsigned)agent_facts.seq);
      vr_log_stage("facts_end");

#ifdef CONFIG_VG_BUS_DISCOVER
      if (vg_live_points_gen() != config_version)
        {
          vr_ui_error("点表已变更，请重新提问");
          return vg_vs_agent_result(&g_vr.s, gen, false, "config changed",
                                    vr_now_ms());
        }
#endif

      agent_facts.config_version = config_version;
      if ((int32_t)(g_vr.s.deadline_ms - vr_now_ms()) > 0 &&
          vr_render_answer(&agent_facts, display, sizeof(display), speech,
                           sizeof(speech)))
        {
          pthread_mutex_lock(&g_vr.ui_lock);
          snprintf(g_vr.ui.answer, sizeof(g_vr.ui.answer), "%s", display);
          pthread_mutex_unlock(&g_vr.ui_lock);

          return vg_vs_agent_result(&g_vr.s, gen, true, speech, vr_now_ms());
        }
    }

  /* Fallback: read once more via the tool and capture only this thread's
   * publication if the model tool did not supply matching facts. */

  return vr_answer_from_tool(gen, tool, tool_json, config_version);
#endif
}

#ifdef CONFIG_VG_VOICE_TTS_STREAM

/* Streaming playback adapter: the cloud sink hands over 24 kHz mono
 * pieces; the incremental resampler turns them into the 16 kHz mono the
 * ring player takes.  One decode chunk is at most VC_PCM_CHUNK bytes
 * (8192 samples in, 5463 out), so this output size never rejects one. */

#define VR_TTS_FEED_SAMPLES 5472

struct vr_tts_feed
{
  struct vg_pcm_rs24_16 rs;
  int16_t out[VR_TTS_FEED_SAMPLES];
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

  return 0;
}

/* Runs on the player thread the moment the driver starts consuming.  The
 * two log lines are the real first-sound markers; the UI status was set
 * before the request, so nothing here touches shared round state beyond
 * the submit anchor, which is stable for the whole TTS stage. */

static void vr_tts_started(void *arg)
{
  uint32_t gen = *(const uint32_t *)arg;

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

    if (vg_tts_player_start(vg_tts_player_prebuf_ms(), &g_vr.cancel,
                            vr_tts_started, &started_gen) == 0)
      {
        struct vr_tts_feed feed;
        struct vg_tts_player_stats pstats;
        uint32_t actions;
        uint32_t limit;
        int wr;

        /* The span anchor the phase scripts read; the real first-sound
         * marker is the play_start stage the player thread logs. */

        vr_log_stage("play_posted");
        vr_ui_set_status("播报中");
        vr_ui_publish();

        __atomic_fetch_add(&g_vr.tts_requests, 1, __ATOMIC_RELAXED);
        vg_pcm_rs24_16_init(&feed.rs);
        rc = vg_voice_cloud_tts_stream(g_vr.s.answer, vr_tts_feed_push,
                                       &feed, &ctl, &info);

        syslog(LOG_INFO,
               "[vgvoice] tts rc=%s status=%d pcm=%u resp=%u ms=%u "
               "stream=1 partial=%u\n",
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

        limit = info.audio_bytes / 32u + VR_PLAY_EXTRA_MS;
        wr = vg_tts_player_wait(limit, &pstats);

        info.played_bytes = pstats.played_bytes;
        info.underruns = pstats.underruns;
        info.stall_ms = pstats.stall_ms;

        syslog(LOG_INFO,
               "[vgvoice] tts_player played=%u pushed=%u underruns=%u "
               "stall_ms=%u restarts=%u complete=%u failed=%u err=%d "
               "leaked=%u ms=%u\n",
               (unsigned)pstats.played_bytes,
               (unsigned)pstats.pushed_bytes,
               (unsigned)pstats.underruns, (unsigned)pstats.stall_ms,
               (unsigned)pstats.restarts, pstats.complete ? 1u : 0u,
               pstats.failed ? 1u : 0u, pstats.err,
               (unsigned)pstats.leaked_buffers,
               (unsigned)(vr_now_ms() - g_vr.s.submit_ms));

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
          /* END_SILENT 会在采集槽里留下原始 PCM 等待上传取用；取消路径
           * 没有后续取用，必须显式清零释放，否则残留音频留在内存。 */
          if (g_vr.clip_generation != 0)
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
       * table that may no longer contain the points it names. */

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
