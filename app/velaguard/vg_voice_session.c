/****************************************************************************
 * app/velaguard/vg_voice_session.c
 *
 * Pure decisions for the push-to-talk session.  No threads, no devices, no
 * clocks of its own: every entry point takes the monotonic millisecond value
 * the caller read, so the rules are reproducible under host test.
 ****************************************************************************/

#include "vg_voice_session.h"

#include <string.h>

/* Wrap-safe elapsed test.  The board clock is 32-bit milliseconds and wraps
 * roughly every 49 days; a plain "now >= start + limit" would break there. */

static bool reached(uint32_t now, uint32_t start, uint32_t limit)
{
  return (int32_t)(now - start) >= (int32_t)limit;
}

static uint32_t elapsed(uint32_t now, uint32_t start)
{
  int32_t d = (int32_t)(now - start);

  return (d > 0) ? (uint32_t)d : 0;
}

static void copy_str(char *dst, size_t cap, const char *src)
{
  if (dst == NULL || cap == 0)
    {
      return;
    }

  if (src == NULL)
    {
      dst[0] = '\0';
      return;
    }

  strncpy(dst, src, cap - 1);
  dst[cap - 1] = '\0';
}

static bool in_flight(enum vg_vs_state st)
{
  return st == VG_VS_ASR || st == VG_VS_WAIT_AGENT ||
         st == VG_VS_QUERYING || st == VG_VS_TTS;
}

static uint32_t cancel_actions(enum vg_vs_state st)
{
  switch (st)
    {
      case VG_VS_RECORDING:
      case VG_VS_STOPPING:
        return VG_VS_ACT_STOP_RECORD;

      case VG_VS_ASR:
        return VG_VS_ACT_CANCEL_ASR;

      case VG_VS_WAIT_AGENT:
      case VG_VS_QUERYING:
        return VG_VS_ACT_CANCEL_AGENT;

      case VG_VS_TTS:
        return VG_VS_ACT_STOP_TTS;

      case VG_VS_PLAYING:
        return VG_VS_ACT_STOP_PLAYBACK;

      default:
        return 0;
    }
}

static void clear_context(struct vg_voice_session *s)
{
  s->point_id[0] = '\0';
  s->text[0] = '\0';
}

void vg_vs_init(struct vg_voice_session *s, uint32_t now_ms)
{
  if (s == NULL)
    {
      return;
    }

  memset(s, 0, sizeof(*s));
  s->state = VG_VS_IDLE;
  s->now_ms = now_ms;
  s->budget_ms = VG_VS_BUDGET_MS;
  s->last_active_ms = now_ms;
}

void vg_vs_set_muted(struct vg_voice_session *s, bool muted)
{
  if (s != NULL)
    {
      s->muted = muted;
    }
}

bool vg_vs_muted(const struct vg_voice_session *s)
{
  return (s != NULL) ? s->muted : false;
}

uint32_t vg_vs_press(struct vg_voice_session *s, uint32_t now_ms)
{
  uint32_t actions = 0;
  enum vg_vs_state prev;

  if (s == NULL)
    {
      return 0;
    }

  s->now_ms = now_ms;
  prev = s->state;

  /* One physical press is already one begin; a second press while the
   * recorder is open is either a bounce or a press the button layer has
   * already coalesced. */

  if (prev == VG_VS_RECORDING || prev == VG_VS_STOPPING)
    {
      return 0;
    }

  /* Any press abandons the previous question.  The generation bump makes
   * every outstanding result stale before the new recording starts. */

  actions |= cancel_actions(prev);
  s->generation++;

  if (prev == VG_VS_PLAYING)
    {
      /* Capture and playback share the audio device: wait for the real stop
       * before asking for the recorder. */

      s->state = VG_VS_STOPPING;
      return actions | VG_VS_ACT_STOP_PLAYBACK;
    }

  if (prev == VG_VS_RESULT || prev == VG_VS_ERROR || prev == VG_VS_IDLE)
    {
      actions |= VG_VS_ACT_HIDE_OVERLAY;
    }

  s->speech_failed = false;
  s->have_answer = false;
  s->answer[0] = '\0';
  s->state = VG_VS_RECORDING;
  s->record_ms = now_ms;
  s->last_active_ms = now_ms;

  return actions | VG_VS_ACT_START_RECORD;
}

uint32_t vg_vs_release(struct vg_voice_session *s, uint32_t now_ms)
{
  uint32_t dur;

  if (s == NULL || s->state != VG_VS_RECORDING)
    {
      return 0;
    }

  s->now_ms = now_ms;
  dur = elapsed(now_ms, s->record_ms);

  if (dur < VG_VS_RECORD_MIN_MS)
    {
      /* Half a press is not a question.  Drop the buffer and say so. */

      s->state = VG_VS_ERROR;
      s->error_ms = now_ms;
      s->last_active_ms = now_ms;
      s->generation++;
      copy_str(s->answer, sizeof(s->answer), "recording too short");
      return VG_VS_ACT_STOP_RECORD | VG_VS_ACT_SHOW_ERROR;
    }

  s->state = VG_VS_ASR;
  s->submit_ms = now_ms;
  s->budget_ms = VG_VS_BUDGET_MS;
  s->deadline_ms = now_ms + VG_VS_BUDGET_MS;
  s->last_active_ms = now_ms;

  return VG_VS_ACT_STOP_RECORD | VG_VS_ACT_START_ASR;
}

uint32_t vg_vs_record_cap(struct vg_voice_session *s, uint32_t now_ms)
{
  if (s == NULL || s->state != VG_VS_RECORDING)
    {
      return 0;
    }

  if (!reached(now_ms, s->record_ms, VG_VS_RECORD_MAX_MS))
    {
      return 0;
    }

  /* The cap submits exactly like a release; the button layer keeps the press
   * gated until the key is released, so this cannot repeat. */

  s->now_ms = now_ms;
  s->state = VG_VS_ASR;
  s->submit_ms = now_ms;
  s->budget_ms = VG_VS_BUDGET_MS;
  s->deadline_ms = now_ms + VG_VS_BUDGET_MS;
  s->last_active_ms = now_ms;

  return VG_VS_ACT_STOP_RECORD | VG_VS_ACT_START_ASR;
}

uint32_t vg_vs_audio_stopped(struct vg_voice_session *s, uint32_t now_ms)
{
  if (s == NULL || s->state != VG_VS_STOPPING)
    {
      return 0;
    }

  s->now_ms = now_ms;
  s->state = VG_VS_RECORDING;
  s->record_ms = now_ms;
  s->last_active_ms = now_ms;
  return VG_VS_ACT_START_RECORD;
}

uint32_t vg_vs_asr_result(struct vg_voice_session *s, uint32_t generation,
                          bool ok, const char *text, uint32_t now_ms)
{
  if (s == NULL || s->state != VG_VS_ASR || generation != s->generation)
    {
      return 0;
    }

  s->now_ms = now_ms;

  if (!ok)
    {
      s->state = VG_VS_ERROR;
      s->error_ms = now_ms;
      s->last_active_ms = now_ms;
      s->generation++;
      copy_str(s->answer, sizeof(s->answer), "speech recognition failed");
      return VG_VS_ACT_SHOW_ERROR;
    }

  if (text == NULL || text[0] == '\0')
    {
      /* An empty transcript is a real result, not a failure to retry: asking
       * the agent about nothing would waste a round and answer nothing. */

      s->state = VG_VS_ERROR;
      s->error_ms = now_ms;
      s->last_active_ms = now_ms;
      s->generation++;
      copy_str(s->answer, sizeof(s->answer), "no speech recognized");
      return VG_VS_ACT_SHOW_ERROR;
    }

  copy_str(s->text, sizeof(s->text), text);
  s->state = VG_VS_WAIT_AGENT;
  return VG_VS_ACT_QUEUE_AGENT;
}

uint32_t vg_vs_agent_started(struct vg_voice_session *s, uint32_t generation,
                             uint32_t now_ms)
{
  if (s == NULL || s->state != VG_VS_WAIT_AGENT ||
      generation != s->generation)
    {
      return 0;
    }

  s->now_ms = now_ms;
  s->state = VG_VS_QUERYING;
  s->last_active_ms = now_ms;
  return 0;
}

uint32_t vg_vs_agent_result(struct vg_voice_session *s, uint32_t generation,
                            bool ok, const char *answer, uint32_t now_ms)
{
  if (s == NULL ||
      (s->state != VG_VS_WAIT_AGENT && s->state != VG_VS_QUERYING) ||
      generation != s->generation)
    {
      return 0;
    }

  s->now_ms = now_ms;

  if (!ok || answer == NULL || answer[0] == '\0')
    {
      s->state = VG_VS_ERROR;
      s->error_ms = now_ms;
      s->last_active_ms = now_ms;
      s->generation++;
      copy_str(s->answer, sizeof(s->answer), "answer unavailable");
      return VG_VS_ACT_SHOW_ERROR;
    }

  copy_str(s->answer, sizeof(s->answer), answer);
  s->have_answer = true;
  s->last_active_ms = now_ms;

  if (s->muted)
    {
      /* Muted: the text is the whole answer, so no TTS request is made. */

      s->state = VG_VS_RESULT;
      s->result_ms = now_ms;
      s->held = false;
      s->speech_failed = false;
      s->generation++;
      return VG_VS_ACT_SHOW_RESULT;
    }

  s->state = VG_VS_TTS;
  s->speech_failed = false;
  return VG_VS_ACT_START_TTS;
}

uint32_t vg_vs_tts_result(struct vg_voice_session *s, uint32_t generation,
                          bool ok, uint32_t now_ms)
{
  if (s == NULL || s->state != VG_VS_TTS || generation != s->generation)
    {
      return 0;
    }

  s->now_ms = now_ms;

  if (!ok)
    {
      /* The answer itself is still valid.  Keep it on screen and offer a
       * playback retry; do not run the agent again. */

      s->state = VG_VS_RESULT;
      s->result_ms = now_ms;
      s->held = false;
      s->speech_failed = true;
      s->last_active_ms = now_ms;
      s->generation++;
      return VG_VS_ACT_SHOW_RESULT;
    }

  s->state = VG_VS_PLAYING;
  s->last_active_ms = now_ms;
  return 0;
}

uint32_t vg_vs_play_done(struct vg_voice_session *s, uint32_t generation,
                         uint32_t now_ms)
{
  if (s == NULL || s->state != VG_VS_PLAYING || generation != s->generation)
    {
      return 0;
    }

  s->now_ms = now_ms;
  s->state = VG_VS_RESULT;
  s->result_ms = now_ms;
  s->held = false;
  s->speech_failed = false;
  s->last_active_ms = now_ms;
  s->generation++;
  return VG_VS_ACT_SHOW_RESULT;
}

uint32_t vg_vs_retry_tts(struct vg_voice_session *s, uint32_t now_ms)
{
  if (s == NULL || s->state != VG_VS_RESULT || !s->have_answer ||
      !s->speech_failed || s->muted)
    {
      return 0;
    }

  s->now_ms = now_ms;
  s->state = VG_VS_TTS;

  /* The first deadline has already been spent, so reusing it would make the
   * retry button dead on arrival.  A retry is a new user action: give the
   * speech stage its own bounded budget and restart the shared deadline at
   * that smaller value. */

  s->submit_ms = now_ms;
  s->budget_ms = VG_VS_RETRY_BUDGET_MS;
  s->deadline_ms = now_ms + VG_VS_RETRY_BUDGET_MS;
  s->speech_failed = false;
  s->generation++;
  return VG_VS_ACT_START_TTS;
}

uint32_t vg_vs_tick(struct vg_voice_session *s, uint32_t now_ms)
{
  uint32_t actions = 0;

  if (s == NULL)
    {
      return 0;
    }

  s->now_ms = now_ms;

  if (s->state == VG_VS_RECORDING)
    {
      return vg_vs_record_cap(s, now_ms);
    }

  if (in_flight(s->state) && reached(now_ms, s->submit_ms, s->budget_ms))
    {
      /* The budget is one deadline for the whole chain, not per stage. */

      actions |= cancel_actions(s->state);

      if (s->state == VG_VS_TTS && s->have_answer)
        {
          s->state = VG_VS_RESULT;
          s->result_ms = now_ms;
          s->held = false;
          s->speech_failed = true;
          s->last_active_ms = now_ms;
          s->generation++;
          return actions | VG_VS_ACT_SHOW_RESULT;
        }

      s->state = VG_VS_ERROR;
      s->error_ms = now_ms;
      s->last_active_ms = now_ms;
      s->generation++;
      copy_str(s->answer, sizeof(s->answer), "timed out");
      return actions | VG_VS_ACT_SHOW_ERROR;
    }

  if (s->state == VG_VS_RESULT && !s->held &&
      reached(now_ms, s->result_ms, VG_VS_RESULT_HOLD_MS))
    {
      /* Hiding is a display event only; the context survives so a follow-up
       * can still name the last point. */

      s->state = VG_VS_IDLE;
      s->last_active_ms = now_ms;
      s->generation++;
      return VG_VS_ACT_HIDE_OVERLAY;
    }

  if ((s->state == VG_VS_IDLE || s->state == VG_VS_RESULT ||
       s->state == VG_VS_ERROR) &&
      reached(now_ms, s->last_active_ms, VG_VS_IDLE_MS))
    {
      clear_context(s);
      s->have_answer = false;
      s->answer[0] = '\0';
      s->speech_failed = false;

      if (s->state != VG_VS_IDLE)
        {
          s->state = VG_VS_IDLE;
          s->generation++;
          actions |= VG_VS_ACT_HIDE_OVERLAY;
        }
    }

  return actions;
}

void vg_vs_result_hold(struct vg_voice_session *s, bool held, uint32_t now_ms)
{
  if (s == NULL || s->state != VG_VS_RESULT)
    {
      return;
    }

  if (held && !s->held)
    {
      s->held = true;
      return;
    }

  if (!held && s->held)
    {
      /* Restart the full window: the reader scrolled, so the time spent
       * reading must not count against the answer. */

      s->held = false;
      s->result_ms = now_ms;
    }
}

uint32_t vg_vs_end(struct vg_voice_session *s, uint32_t now_ms)
{
  uint32_t actions;

  if (s == NULL)
    {
      return 0;
    }

  actions = cancel_actions(s->state);

  if (s->state != VG_VS_IDLE)
    {
      /* The overlay shows recording, progress, answers and errors alike;
       * ending the session has to take whichever of those is on screen. */

      actions |= VG_VS_ACT_HIDE_OVERLAY;
    }

  if (s->state == VG_VS_PLAYING || s->state == VG_VS_TTS)
    {
      actions |= VG_VS_ACT_RELEASE_AUDIO;
    }

  clear_context(s);
  s->answer[0] = '\0';
  s->have_answer = false;
  s->speech_failed = false;
  s->state = VG_VS_IDLE;
  s->now_ms = now_ms;
  s->last_active_ms = now_ms;
  s->generation++;
  return actions;
}

void vg_vs_page_changed(struct vg_voice_session *s, uint32_t now_ms)
{
  /* Deliberately nothing: the deadline, the answer and the context belong to
   * the session, not to the page that happens to be visible.  The function
   * exists so callers do not reach into the struct. */

  if (s != NULL)
    {
      s->now_ms = now_ms;
    }
}

void vg_vs_set_context_point(struct vg_voice_session *s, const char *point_id,
                             uint32_t now_ms)
{
  if (s == NULL)
    {
      return;
    }

  copy_str(s->point_id, sizeof(s->point_id), point_id);
  s->last_active_ms = now_ms;
}

void vg_vs_set_config_version(struct vg_voice_session *s, uint32_t version)
{
  if (s == NULL)
    {
      return;
    }

  if (s->config_version == version)
    {
      return;
    }

  s->config_version = version;
  s->point_id[0] = '\0';

  /* An answer in flight was built against the old table.  Finish the session
   * instead of showing facts about points that may no longer exist. */

  if (in_flight(s->state) || s->state == VG_VS_RECORDING ||
      s->state == VG_VS_STOPPING)
    {
      (void)vg_vs_end(s, s->now_ms);
    }
}

const char *vg_vs_context_point(const struct vg_voice_session *s)
{
  if (s == NULL)
    {
      return NULL;
    }

  return s->point_id;
}

uint32_t vg_vs_remaining_ms(const struct vg_voice_session *s, uint32_t now_ms)
{
  if (s == NULL || !in_flight(s->state))
    {
      return 0;
    }

  if (reached(now_ms, s->submit_ms, s->budget_ms))
    {
      return 0;
    }

  return s->budget_ms - elapsed(now_ms, s->submit_ms);
}

uint32_t vg_vs_result_left_s(const struct vg_voice_session *s, uint32_t now_ms)
{
  uint32_t used;

  if (s == NULL || s->state != VG_VS_RESULT)
    {
      return 0;
    }

  if (s->held)
    {
      return VG_VS_RESULT_HOLD_MS / 1000u;
    }

  used = elapsed(now_ms, s->result_ms);
  if (used >= VG_VS_RESULT_HOLD_MS)
    {
      return 0;
    }

  return (VG_VS_RESULT_HOLD_MS - used + 999u) / 1000u;
}

enum vg_vs_state vg_vs_state(const struct vg_voice_session *s)
{
  return (s != NULL) ? s->state : VG_VS_IDLE;
}

uint32_t vg_vs_generation(const struct vg_voice_session *s)
{
  return (s != NULL) ? s->generation : 0;
}

const char *vg_vs_state_name(enum vg_vs_state state)
{
  switch (state)
    {
      case VG_VS_RECORDING:  return "recording";
      case VG_VS_STOPPING:   return "stopping";
      case VG_VS_ASR:        return "asr";
      case VG_VS_WAIT_AGENT: return "wait_agent";
      case VG_VS_QUERYING:   return "querying";
      case VG_VS_TTS:        return "tts";
      case VG_VS_PLAYING:    return "playing";
      case VG_VS_RESULT:     return "result";
      case VG_VS_ERROR:      return "error";
      default:               return "idle";
    }
}
