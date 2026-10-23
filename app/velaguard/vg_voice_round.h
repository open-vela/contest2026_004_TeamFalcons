/****************************************************************************
 * app/velaguard/vg_voice_round.h
 *
 * BVO-11/12: the running voice round.
 *
 * USER1 press -> record -> release -> ASR -> on-board agent (read-only tools)
 * -> validated answer -> TTS -> Line Out, with one cross-page overlay fed by
 * the same state.
 *
 * The worker owns the pipeline.  The button callback only posts an event, and
 * the UI only reads a published snapshot, so neither the button task nor the
 * LVGL thread ever waits on the network.
 ****************************************************************************/

#ifndef __APP_VELAGUARD_VG_VOICE_ROUND_H
#define __APP_VELAGUARD_VG_VOICE_ROUND_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "vg_voice_session.h"

/* The full anomaly list does not fit the session's short answer buffer, and
 * the overlay must show all of it. */
#define VG_VOICE_DISPLAY_MAX 2560

/* What the overlay draws.  Copied out under a lock, so the UI never holds a
 * pointer into the session. */

struct vg_voice_ui
{
  bool     visible;
  bool     muted;
  bool     speech_failed;
  uint32_t generation;
  uint32_t state;                 /* enum vg_vs_state */
  uint32_t record_left_ms;        /* while recording */
  uint32_t budget_left_ms;        /* while a question is in flight */
  uint32_t result_left_ms;        /* while the answer is on screen */
  char     status[64];            /* 录音中 / 识别中 / 查询中 / 播报中 ... */
  char     question[VG_VS_TEXT_MAX];
  char     answer[VG_VOICE_DISPLAY_MAX];
  char     error[128];
};

int  vg_voice_round_start(void);
void vg_voice_round_stop(void);

/* Snapshot for the overlay.  Returns false when the round has not started. */
bool vg_voice_round_ui(struct vg_voice_ui *out);
/* Diagnostic counters, read atomically; no recording or conversation data. */
int vg_voice_round_counts(uint32_t *asr, uint32_t *tts);

/* Preference, persisted by the caller (BVO-13). */
void vg_voice_round_set_muted(bool muted);
bool vg_voice_round_muted(void);

/* Overlay interaction.  Scrolling the answer pauses the 15 s hide timer. */
void vg_voice_round_hold(bool held);

/* Explicit end from the overlay or a long press. */
void vg_voice_round_end(void);

/* NuttX stack high-water estimate.  Size and headroom exclude TLS.
 * Returns a negative errno if stopped or stack coloration is disabled. */
int vg_voice_round_stack(size_t *size, size_t *used, size_t *headroom);

/* Diagnostic entry that posts the same event the button callback posts.
 * It does not read the pin, so it cannot stand in for a real press; it exists
 * so the pipeline after the event can be exercised without hands. */
void vg_voice_round_press_sim(bool press);

/* Diagnostic: use this WAV for the next recognition instead of the microphone
 * recording.  A real button press clears it, so it cannot outlive the test. */
void vg_voice_round_set_asr_source(const char *path);

/* Retry speech only, using the answer already validated. */
void vg_voice_round_retry_tts(void);

#endif /* __APP_VELAGUARD_VG_VOICE_ROUND_H */
