/****************************************************************************
 * app/velaguard/host_tests/test_voice_session.c
 *
 * BVO-08: the interaction contract in executable form.  Every case drives the
 * state machine with an explicit clock; nothing here needs a board.
 ****************************************************************************/

#include <stdio.h>
#include <string.h>

#include "vg_voice_session.h"

static int g_fail;

static void expect(int cond, const char *msg)
{
  if (!cond)
    {
      fprintf(stderr, "FAIL: %s\n", msg);
      g_fail++;
    }
}

static void to_asr(struct vg_voice_session *s, uint32_t *t)
{
  uint32_t a;

  a = vg_vs_press(s, *t);
  expect((a & VG_VS_ACT_START_RECORD) != 0, "press starts capture");
  expect(vg_vs_state(s) == VG_VS_RECORDING, "state recording");

  *t += 2000;
  a = vg_vs_release(s, *t);
  expect((a & VG_VS_ACT_START_ASR) != 0, "release starts asr");
  expect((a & VG_VS_ACT_STOP_RECORD) != 0, "release stops capture");
  expect(vg_vs_state(s) == VG_VS_ASR, "state asr");
}

static void test_happy_path(void)
{
  struct vg_voice_session s;
  uint32_t t = 1000;
  uint32_t gen;
  uint32_t a;

  vg_vs_init(&s, t);
  to_asr(&s, &t);
  gen = vg_vs_generation(&s);

  a = vg_vs_asr_result(&s, gen, true, "当前有哪些异常", t);
  expect(a == VG_VS_ACT_QUEUE_AGENT, "transcript queues the agent");
  expect(vg_vs_state(&s) == VG_VS_WAIT_AGENT, "state wait_agent");

  a = vg_vs_agent_started(&s, gen, t);
  expect(a == 0, "agent start needs no action");
  expect(vg_vs_state(&s) == VG_VS_QUERYING, "state querying");

  a = vg_vs_agent_result(&s, gen, true, "共 2 项异常：UPS负载 92%", t);
  expect(a == VG_VS_ACT_START_TTS, "answer starts tts");
  expect(vg_vs_state(&s) == VG_VS_TTS, "state tts");
  gen = vg_vs_generation(&s);

  a = vg_vs_tts_result(&s, gen, true, t);
  expect(a == 0, "tts success needs no action");
  expect(vg_vs_state(&s) == VG_VS_PLAYING, "state playing");

  a = vg_vs_play_done(&s, gen, t);
  expect((a & VG_VS_ACT_SHOW_RESULT) != 0, "playback end shows the answer");
  expect(vg_vs_state(&s) == VG_VS_RESULT, "state result");

  expect(vg_vs_result_left_s(&s, t) == 15, "fifteen seconds to read");

  a = vg_vs_tick(&s, t + 14999);
  expect(a == 0, "answer still visible before fifteen seconds");

  a = vg_vs_tick(&s, t + 15000);
  expect((a & VG_VS_ACT_HIDE_OVERLAY) != 0, "answer hides at fifteen seconds");
  expect(vg_vs_state(&s) == VG_VS_IDLE, "state idle after hide");
}

static void test_replacement_and_late_result(void)
{
  struct vg_voice_session s;
  uint32_t t = 1000;
  uint32_t old_gen;
  uint32_t a;

  vg_vs_init(&s, t);
  to_asr(&s, &t);
  old_gen = vg_vs_generation(&s);
  (void)vg_vs_asr_result(&s, old_gen, true, "UPS负载", t);
  (void)vg_vs_agent_started(&s, old_gen, t);

  /* A second question while the first is running replaces it. */

  a = vg_vs_press(&s, t + 1000);
  expect((a & VG_VS_ACT_CANCEL_AGENT) != 0, "replacement cancels the old query");
  expect((a & VG_VS_ACT_START_RECORD) != 0, "replacement starts recording");
  expect(vg_vs_generation(&s) != old_gen, "replacement bumps generation");

  /* The old answer arrives late and must be ignored. */

  a = vg_vs_agent_result(&s, old_gen, true, "旧回答", t + 2000);
  expect(a == 0, "late answer is dropped");
  expect(vg_vs_state(&s) == VG_VS_RECORDING, "late answer does not change state");
}

static void test_playback_interrupt(void)
{
  struct vg_voice_session s;
  uint32_t t = 1000;
  uint32_t gen;
  uint32_t a;

  vg_vs_init(&s, t);
  to_asr(&s, &t);
  gen = vg_vs_generation(&s);
  (void)vg_vs_asr_result(&s, gen, true, "UPS负载", t);
  (void)vg_vs_agent_result(&s, gen, true, "负载 40%", t);
  gen = vg_vs_generation(&s);
  (void)vg_vs_tts_result(&s, gen, true, t);
  expect(vg_vs_state(&s) == VG_VS_PLAYING, "playing before interrupt");

  a = vg_vs_press(&s, t + 100);
  expect((a & VG_VS_ACT_STOP_PLAYBACK) != 0, "press stops playback");
  expect((a & VG_VS_ACT_START_RECORD) == 0, "capture waits for the real stop");
  expect(vg_vs_state(&s) == VG_VS_STOPPING, "state stopping");

  a = vg_vs_audio_stopped(&s, t + 300);
  expect((a & VG_VS_ACT_START_RECORD) != 0, "capture starts after stop");
  expect(vg_vs_state(&s) == VG_VS_RECORDING, "state recording");
}

/* A background round has no generation in this module.  Its result must not
 * be mistaken for the queued user question, and queueing a question must not
 * cancel the background work. */

static void test_background_isolation(void)
{
  struct vg_voice_session s;
  uint32_t t = 1000;
  uint32_t gen;
  uint32_t a;

  vg_vs_init(&s, t);
  to_asr(&s, &t);
  gen = vg_vs_generation(&s);
  (void)vg_vs_asr_result(&s, gen, true, "UPS负载", t);

  a = vg_vs_agent_result(&s, 0, true, "日报", t);
  expect(a == 0, "background result is not the user question");
  expect(vg_vs_state(&s) == VG_VS_WAIT_AGENT, "queued question unchanged");

  a = vg_vs_press(&s, t + 100);
  expect((a & VG_VS_ACT_CANCEL_AGENT) != 0, "replacement cancels the user slot");
  expect(vg_vs_state(&s) == VG_VS_RECORDING, "replacement records again");
}

/* A restart must not inherit a session, a context or a deadline. */

static void test_restart_clears(void)
{
  struct vg_voice_session s;
  uint32_t t = 1000;

  vg_vs_init(&s, t);
  vg_vs_set_context_point(&s, "ups_load", t);
  (void)vg_vs_press(&s, t);

  vg_vs_init(&s, t + 5000);
  expect(vg_vs_state(&s) == VG_VS_IDLE, "restart is idle");
  expect(vg_vs_context_point(&s)[0] == '\0', "restart clears the context");
  expect(vg_vs_remaining_ms(&s, t + 5000) == 0, "restart clears the deadline");
}

static void test_mute_skips_tts(void)
{
  struct vg_voice_session s;
  uint32_t t = 1000;
  uint32_t gen;
  uint32_t a;

  vg_vs_init(&s, t);
  vg_vs_set_muted(&s, true);
  to_asr(&s, &t);
  gen = vg_vs_generation(&s);
  (void)vg_vs_asr_result(&s, gen, true, "UPS负载", t);

  a = vg_vs_agent_result(&s, gen, true, "负载 40%", t);
  expect(a == VG_VS_ACT_SHOW_RESULT, "muted answer shows without tts");
  expect(vg_vs_state(&s) == VG_VS_RESULT, "state result");
  expect(vg_vs_retry_tts(&s, t) == 0, "muted session does not retry tts");
}

static void test_empty_transcript(void)
{
  struct vg_voice_session s;
  uint32_t t = 1000;
  uint32_t gen;
  uint32_t a;

  vg_vs_init(&s, t);
  to_asr(&s, &t);
  gen = vg_vs_generation(&s);

  a = vg_vs_asr_result(&s, gen, true, "", t);
  expect((a & VG_VS_ACT_SHOW_ERROR) != 0, "empty transcript reports an error");
  expect((a & VG_VS_ACT_QUEUE_AGENT) == 0, "empty transcript never queues the agent");
  expect(vg_vs_state(&s) == VG_VS_ERROR, "state error");
}

static void test_tts_failure_retries_speech_only(void)
{
  struct vg_voice_session s;
  uint32_t t = 1000;
  uint32_t gen;
  uint32_t a;

  vg_vs_init(&s, t);
  to_asr(&s, &t);
  gen = vg_vs_generation(&s);
  (void)vg_vs_asr_result(&s, gen, true, "UPS负载", t);
  (void)vg_vs_agent_result(&s, gen, true, "负载 40%", t);
  gen = vg_vs_generation(&s);

  a = vg_vs_tts_result(&s, gen, false, t);
  expect((a & VG_VS_ACT_SHOW_RESULT) != 0, "tts failure keeps the text");
  expect(vg_vs_state(&s) == VG_VS_RESULT, "state result");
  expect(s.speech_failed == true, "speech marked failed");

  a = vg_vs_retry_tts(&s, t + 1000);
  expect((a & VG_VS_ACT_START_TTS) != 0, "retry starts tts");
  expect((a & VG_VS_ACT_QUEUE_AGENT) == 0, "retry does not rerun the agent");
  expect(vg_vs_state(&s) == VG_VS_TTS, "state tts after retry");
}

static void test_budget(void)
{
  struct vg_voice_session s;
  uint32_t t = 1000;
  uint32_t gen;
  uint32_t a;

  vg_vs_init(&s, t);
  to_asr(&s, &t);
  gen = vg_vs_generation(&s);

  expect(vg_vs_remaining_ms(&s, t) == VG_VS_BUDGET_MS,
         "budget starts at thirty seconds");
  expect(vg_vs_remaining_ms(&s, t + 10000) == 20000, "budget counts down");

  a = vg_vs_tick(&s, t + 30000);
  expect((a & VG_VS_ACT_CANCEL_ASR) != 0, "expired asr is cancelled");
  expect((a & VG_VS_ACT_SHOW_ERROR) != 0, "expired asr reports an error");
  expect(vg_vs_state(&s) == VG_VS_ERROR, "state error on expiry");
  (void)gen;

  /* The same deadline applies after the answer text exists: keep the text,
   * fail only the speech. */

  vg_vs_init(&s, t);
  to_asr(&s, &t);
  gen = vg_vs_generation(&s);
  (void)vg_vs_asr_result(&s, gen, true, "UPS负载", t);
  (void)vg_vs_agent_result(&s, gen, true, "负载 40%", t);
  expect(vg_vs_state(&s) == VG_VS_TTS, "waiting for tts");

  a = vg_vs_tick(&s, t + 30000);
  expect((a & VG_VS_ACT_STOP_TTS) != 0, "expired tts is stopped");
  expect((a & VG_VS_ACT_SHOW_RESULT) != 0, "text survives tts expiry");
  expect(vg_vs_state(&s) == VG_VS_RESULT, "state result after tts expiry");
  expect(s.speech_failed == true, "speech failure recorded");
}

/* A retry after the first deadline must get its own bounded budget.  Reusing
 * the spent deadline made the button fail instantly on the board. */

static void test_retry_budget(void)
{
  struct vg_voice_session s;
  uint32_t t = 1000;
  uint32_t gen;
  uint32_t a;

  vg_vs_init(&s, t);
  to_asr(&s, &t);
  gen = vg_vs_generation(&s);
  (void)vg_vs_asr_result(&s, gen, true, "现在有哪些异常", t);
  (void)vg_vs_agent_result(&s, gen, true, "共 0 项", t);

  a = vg_vs_tick(&s, t + 30000);
  expect((a & VG_VS_ACT_SHOW_RESULT) != 0, "first budget expires into result");
  expect(s.speech_failed, "speech failed");

  a = vg_vs_retry_tts(&s, t + 31000);
  expect((a & VG_VS_ACT_START_TTS) != 0, "retry starts speech");
  expect(vg_vs_state(&s) == VG_VS_TTS, "state tts after retry");
  expect(vg_vs_remaining_ms(&s, t + 31000) == VG_VS_RETRY_BUDGET_MS,
         "retry gets a fresh bounded budget");

  expect(vg_vs_tick(&s, t + 31000 + VG_VS_RETRY_BUDGET_MS - 1) == 0,
         "retry budget does not expire early");

  a = vg_vs_tick(&s, t + 31000 + VG_VS_RETRY_BUDGET_MS);
  expect((a & VG_VS_ACT_SHOW_RESULT) != 0, "retry budget expires into result");
  expect(s.speech_failed, "retry failure recorded again");

  a = vg_vs_retry_tts(&s, t + 50000);
  expect((a & VG_VS_ACT_START_TTS) != 0, "a second retry also starts");
  expect(vg_vs_remaining_ms(&s, t + 50000) == VG_VS_RETRY_BUDGET_MS,
         "each retry is bounded on its own");
}

static void test_record_cap_and_short(void)
{
  struct vg_voice_session s;
  uint32_t t = 1000;
  uint32_t a;

  vg_vs_init(&s, t);
  (void)vg_vs_press(&s, t);

  expect(vg_vs_record_cap(&s, t + 14999) == 0, "cap not reached early");

  a = vg_vs_record_cap(&s, t + 15000);
  expect((a & VG_VS_ACT_START_ASR) != 0, "cap submits once");
  expect(vg_vs_state(&s) == VG_VS_ASR, "state asr at the cap");

  expect(vg_vs_record_cap(&s, t + 16000) == 0, "cap does not repeat");

  vg_vs_init(&s, t);
  (void)vg_vs_press(&s, t);
  a = vg_vs_release(&s, t + 100);
  expect((a & VG_VS_ACT_SHOW_ERROR) != 0, "too short press reports an error");
  expect((a & VG_VS_ACT_START_ASR) == 0, "too short press does not submit");
}

static void test_idle_and_page(void)
{
  struct vg_voice_session s;
  uint32_t t = 1000;
  uint32_t gen;
  uint32_t a;

  vg_vs_init(&s, t);
  to_asr(&s, &t);
  gen = vg_vs_generation(&s);
  (void)vg_vs_asr_result(&s, gen, true, "UPS负载", t);
  (void)vg_vs_agent_result(&s, gen, true, "负载 40%", t);
  expect(vg_vs_state(&s) == VG_VS_TTS, "tts before page switch");

  vg_vs_page_changed(&s, t + 5000);
  expect(vg_vs_state(&s) == VG_VS_TTS, "page switch keeps the session");
  expect(vg_vs_remaining_ms(&s, t + 5000) == 25000,
         "page switch keeps the deadline");

  vg_vs_set_context_point(&s, "ups_load", t);
  expect(strcmp(vg_vs_context_point(&s), "ups_load") == 0, "context set");

  gen = vg_vs_generation(&s);
  (void)vg_vs_tts_result(&s, gen, true, t);
  gen = vg_vs_generation(&s);
  (void)vg_vs_play_done(&s, gen, t + 1000);

  a = vg_vs_tick(&s, t + 16000);
  expect((a & VG_VS_ACT_HIDE_OVERLAY) != 0, "answer hides");
  expect(strcmp(vg_vs_context_point(&s), "ups_load") == 0,
         "hiding keeps the context");

  /* The two-minute idle window starts when the answer hid, not when the
   * question was submitted. */

  a = vg_vs_tick(&s, t + 16000 + VG_VS_IDLE_MS - 1);
  expect(vg_vs_context_point(&s)[0] != '\0', "context survives until idle");

  a = vg_vs_tick(&s, t + 16000 + VG_VS_IDLE_MS);
  expect(vg_vs_context_point(&s)[0] == '\0', "idle releases the context");
}

static void test_result_hold(void)
{
  struct vg_voice_session s;
  uint32_t t = 1000;
  uint32_t a;

  vg_vs_init(&s, t);
  s.state = VG_VS_RESULT;
  s.result_ms = t;

  vg_vs_result_hold(&s, true, t + 5000);
  a = vg_vs_tick(&s, t + 60000);
  expect((a & VG_VS_ACT_HIDE_OVERLAY) == 0, "scrolling pauses the hide timer");
  expect(vg_vs_state(&s) == VG_VS_RESULT, "answer stays while held");

  vg_vs_result_hold(&s, false, t + 61000);
  a = vg_vs_tick(&s, t + 61000 + 14999);
  expect(a == 0, "restarted timer runs");
  a = vg_vs_tick(&s, t + 61000 + 15000);
  expect((a & VG_VS_ACT_HIDE_OVERLAY) != 0, "hide after the restarted timer");
}

static void test_end_and_config_change(void)
{
  struct vg_voice_session s;
  uint32_t t = 1000;
  uint32_t gen;
  uint32_t a;

  vg_vs_init(&s, t);
  to_asr(&s, &t);
  gen = vg_vs_generation(&s);
  (void)vg_vs_asr_result(&s, gen, true, "UPS负载", t);
  vg_vs_set_context_point(&s, "ups_load", t);

  a = vg_vs_end(&s, t + 100);
  expect((a & VG_VS_ACT_CANCEL_AGENT) != 0, "end cancels the agent");
  expect((a & VG_VS_ACT_HIDE_OVERLAY) != 0, "end hides the overlay");
  expect(vg_vs_state(&s) == VG_VS_IDLE, "end returns to idle");
  expect(vg_vs_context_point(&s)[0] == '\0', "end clears the context");

  /* A configuration change invalidates an in-flight question. */

  vg_vs_init(&s, t);
  vg_vs_set_config_version(&s, 1);
  to_asr(&s, &t);
  vg_vs_set_context_point(&s, "ups_load", t);
  vg_vs_set_config_version(&s, 2);
  expect(vg_vs_state(&s) == VG_VS_IDLE, "config change ends the session");
  expect(vg_vs_context_point(&s)[0] == '\0', "config change clears the context");
}

int main(void)
{
  test_happy_path();
  test_replacement_and_late_result();
  test_playback_interrupt();
  test_background_isolation();
  test_restart_clears();
  test_mute_skips_tts();
  test_empty_transcript();
  test_tts_failure_retries_speech_only();
  test_budget();
  test_retry_budget();
  test_record_cap_and_short();
  test_idle_and_page();
  test_result_hold();
  test_end_and_config_change();

  if (g_fail != 0)
    {
      fprintf(stderr, "test_voice_session: %d failure(s)\n", g_fail);
      return 1;
    }

  printf("test_voice_session: ok\n");
  return 0;
}
