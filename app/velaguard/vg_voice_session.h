/****************************************************************************
 * app/velaguard/vg_voice_session.h
 *
 * BVO-08: push-to-talk session state machine.
 *
 * The module decides what should happen; it performs no I/O.  Every entry
 * point returns a bitmask of actions the caller must carry out, in the order
 * the bits are documented.  Keeping the decisions here is what makes the
 * timing rules testable with a chosen clock instead of a board.
 *
 * Ownership:
 *   - The button layer (vg_ptt) only reports press, release and the 15 s cap.
 *   - Audio capture and playback live in vg_voice.
 *   - ASR, the agent round and TTS live in the cloud voice module.
 *   - The overlay reads state and text; it never drives the pipeline.
 *
 * Generation:
 *   Every user question carries one generation.  A cancel, a replacement or
 *   an explicit end bumps it, and every result entry point drops a generation
 *   that no longer matches.  A late reply therefore cannot overwrite a newer
 *   question, and a background agent round is never cancelled by this module:
 *   it has no generation here at all.
 ****************************************************************************/

#ifndef __APP_VELAGUARD_VG_VOICE_SESSION_H
#define __APP_VELAGUARD_VG_VOICE_SESSION_H

#include <stdbool.h>
#include <stdint.h>

#define VG_VS_RECORD_MAX_MS      15000u  /* hard cap from the interaction contract */
#define VG_VS_RECORD_MIN_MS        300u  /* below this, nothing was said */
#define VG_VS_BUDGET_MS          30000u  /* submit -> recognition done */
#define VG_VS_AGENT_BUDGET_MS    60000u  /* recognition done -> answer text (ops assistant, 2026-10-25) */
#define VG_VS_TTS_BUDGET_MS      30000u  /* answer text -> complete speech file */
#define VG_VS_RETRY_BUDGET_MS    15000u  /* speech-only retry after the first deadline */
#define VG_VS_RESULT_HOLD_MS     15000u  /* answer stays after speech ends */
#define VG_VS_IDLE_MS           300000u  /* follow-up context expires after this */
#define VG_VS_CANCEL_GRACE_MS     2000u  /* target for physical reclaim, BVO-07 */

#define VG_VS_TEXT_MAX            512
#define VG_VS_POINT_MAX            48

enum vg_vs_state
{
  VG_VS_IDLE = 0,
  VG_VS_RECORDING,
  VG_VS_STOPPING,     /* playback must stop before capture may start */
  VG_VS_ASR,
  VG_VS_WAIT_AGENT,   /* accepted, waiting in the single user-question slot */
  VG_VS_QUERYING,     /* handed to the agent */
  VG_VS_TTS,
  VG_VS_PLAYING,
  VG_VS_RESULT,
  VG_VS_ERROR
};

/* Actions are bits so one call can ask for a stop and a start together.
 * The caller executes them in this order: stop playback, stop capture,
 * cancel transport, start capture, start ASR, queue the agent, start TTS,
 * publish the result. */

#define VG_VS_ACT_STOP_PLAYBACK   (1u << 0)
#define VG_VS_ACT_STOP_RECORD     (1u << 1)
#define VG_VS_ACT_CANCEL_ASR      (1u << 2)
#define VG_VS_ACT_CANCEL_AGENT    (1u << 3)
#define VG_VS_ACT_STOP_TTS        (1u << 4)
#define VG_VS_ACT_START_RECORD    (1u << 5)
#define VG_VS_ACT_START_ASR       (1u << 6)
#define VG_VS_ACT_QUEUE_AGENT     (1u << 7)
#define VG_VS_ACT_START_TTS       (1u << 8)
#define VG_VS_ACT_SHOW_RESULT     (1u << 9)
#define VG_VS_ACT_SHOW_ERROR      (1u << 10)
#define VG_VS_ACT_HIDE_OVERLAY    (1u << 11)
#define VG_VS_ACT_RELEASE_AUDIO   (1u << 12)

struct vg_voice_session
{
  enum vg_vs_state state;
  uint32_t generation;
  uint32_t now_ms;

  uint32_t submit_ms;       /* when the question was submitted */
  uint32_t deadline_ms;     /* current bounded request deadline */
  uint32_t budget_ms;       /* deadline offset from submit_ms; TTS adds its own span */
  uint32_t record_ms;       /* when capture started */
  uint32_t result_ms;       /* when the answer became visible */
  uint32_t error_ms;
  uint32_t last_active_ms;  /* for the idle timeout */

  uint32_t config_version;
  bool     muted;
  bool     held;            /* result display paused while the user scrolls */
  bool     speech_failed;   /* text is valid, playback is not */
  bool     have_answer;

  char point_id[VG_VS_POINT_MAX];  /* last explicit point, for follow-ups */
  char text[VG_VS_TEXT_MAX];       /* last recognized question */
  char answer[VG_VS_TEXT_MAX];     /* validated answer, display copy */
};

void vg_vs_init(struct vg_voice_session *s, uint32_t now_ms);

/* Preference changes are allowed in any state; mute only affects questions
 * that have not reached TTS yet. */
void vg_vs_set_muted(struct vg_voice_session *s, bool muted);
bool vg_vs_muted(const struct vg_voice_session *s);

/* Button and cap.  Press in RECORDING is ignored; vg_ptt already guarantees
 * one begin per physical press. */
uint32_t vg_vs_press(struct vg_voice_session *s, uint32_t now_ms);
uint32_t vg_vs_release(struct vg_voice_session *s, uint32_t now_ms);
uint32_t vg_vs_record_cap(struct vg_voice_session *s, uint32_t now_ms);

/* Playback has really stopped; only meaningful in VG_VS_STOPPING. */
uint32_t vg_vs_audio_stopped(struct vg_voice_session *s, uint32_t now_ms);

/* Pipeline results.  A generation that does not match is a late result and
 * returns 0 without touching state. */
uint32_t vg_vs_asr_result(struct vg_voice_session *s, uint32_t generation,
                          bool ok, const char *text, uint32_t now_ms);
uint32_t vg_vs_agent_started(struct vg_voice_session *s, uint32_t generation,
                             uint32_t now_ms);
uint32_t vg_vs_agent_result(struct vg_voice_session *s, uint32_t generation,
                            bool ok, const char *answer, uint32_t now_ms);
uint32_t vg_vs_tts_result(struct vg_voice_session *s, uint32_t generation,
                          bool ok, uint32_t now_ms);
uint32_t vg_vs_play_done(struct vg_voice_session *s, uint32_t generation,
                         uint32_t now_ms);

/* Retry playback only: the validated answer and its sample time stay. */
uint32_t vg_vs_retry_tts(struct vg_voice_session *s, uint32_t now_ms);

/* Periodic driver: recording cap, the 30 s budget, result auto-hide and the
 * two-minute idle release. */
uint32_t vg_vs_tick(struct vg_voice_session *s, uint32_t now_ms);

/* Scrolling the answer pauses the 15 s hide timer.  Releasing it restarts
 * the timer from the release moment, so a reader is not cut off mid-list. */
void vg_vs_result_hold(struct vg_voice_session *s, bool held, uint32_t now_ms);

/* Explicit end (long press / on-screen close).  Releases context and audio. */
uint32_t vg_vs_end(struct vg_voice_session *s, uint32_t now_ms);

/* A page switch must not reset the deadline, the answer or the context. */
void vg_vs_page_changed(struct vg_voice_session *s, uint32_t now_ms);

/* Context.  Setting a new point replaces the previous one.  A config version
 * change invalidates it: a point id from another table is not a fact. */
void vg_vs_set_context_point(struct vg_voice_session *s, const char *point_id,
                             uint32_t now_ms);
void vg_vs_set_config_version(struct vg_voice_session *s, uint32_t version);
const char *vg_vs_context_point(const struct vg_voice_session *s);

/* Milliseconds left before the current deadline; 0 on expiry or when no
 * request is in flight. ASR and query share 30 s; a valid answer gets a
 * separate bounded TTS window. */
uint32_t vg_vs_remaining_ms(const struct vg_voice_session *s, uint32_t now_ms);

/* Seconds the answer has left on screen; 0 when it is not showing. */
uint32_t vg_vs_result_left_s(const struct vg_voice_session *s, uint32_t now_ms);

enum vg_vs_state vg_vs_state(const struct vg_voice_session *s);
const char *vg_vs_state_name(enum vg_vs_state state);
uint32_t vg_vs_generation(const struct vg_voice_session *s);

#endif /* __APP_VELAGUARD_VG_VOICE_SESSION_H */
