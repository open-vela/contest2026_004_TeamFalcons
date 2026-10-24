/****************************************************************************
 * app/velaguard/vg_voice_cloud.h
 *
 * BVO-11: cloud speech adaptation.
 *
 *   ASR   the recorded samples are turned into one mono WAV in pieces,
 *         base64-encoded and sent as one JSON request; the transcript
 *         comes back in the reply.  The samples can come from a file or
 *         from the recording that is still in RAM.
 *   TTS   the answer text is synthesised as 24 kHz mono PCM, resampled to
 *         the 16 kHz stereo the player already accepts, and written to a
 *         WAV the caller can hand to NxPlayer.
 *
 * Both calls take the same vela_req_ctl_t as the rest of the voice round, so
 * recognition and synthesis spend the remaining 30 s budget instead of each
 * starting a new one.  Neither call retries: a retry inside a fixed budget
 * only makes the failure later.
 *
 * The raw recording is deleted as soon as recognition ends, whatever the
 * result.  TTS intermediate PCM is deleted after conversion.  Neither file
 * is ever written to a log.
 ****************************************************************************/

#ifndef __APP_VELAGUARD_VG_VOICE_CLOUD_H
#define __APP_VELAGUARD_VG_VOICE_CLOUD_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <agent_http.h>

#include "vg_voice_pref.h"

#define VG_VC_TEXT_MAX 512

#ifndef CONFIG_VG_VOICE_ASR_MODEL
#  define CONFIG_VG_VOICE_ASR_MODEL "mimo-v2.5-asr"
#endif

#ifndef CONFIG_VG_VOICE_TTS_MODEL
#  define CONFIG_VG_VOICE_TTS_MODEL "mimo-v2.5-tts"
#endif

#ifndef CONFIG_VG_VOICE_TTS_VOICE
#  define CONFIG_VG_VOICE_TTS_VOICE "冰糖"
#endif

enum vg_vc_result
{
  VG_VC_OK = 0,
  VG_VC_EMPTY,      /* recognition returned no text */
  VG_VC_CANCELLED,
  VG_VC_TIMEOUT,
  VG_VC_NET,        /* DNS, TCP, TLS or socket */
  VG_VC_HTTP,       /* the endpoint answered with an error status */
  VG_VC_FORMAT,     /* reply was not the documented shape */
  VG_VC_IO,         /* local file */
  VG_VC_NO_CRED,    /* no API key configured */
  VG_VC_NO_TEXT,    /* TTS asked to speak an empty string */
  VG_VC_TRUNCATED   /* TTS stream cut before its terminator */
};

struct vg_vc_info
{
  int      status;       /* HTTP status, 0 when no reply */
  uint32_t req_bytes;
  uint32_t resp_bytes;
  uint32_t audio_bytes;  /* TTS: PCM before resampling */
  uint32_t elapsed_ms;
  /* Streaming playback only; the round fills them from the player stats
   * once playback ends. */
  uint32_t played_bytes; /* stereo bytes handed to the driver */
  uint32_t underruns;
  uint32_t stall_ms;
  uint8_t  partial;      /* failure path: some audio was already audible */
  uint8_t  streamed;
};

const char *vg_vc_result_name(enum vg_vc_result r);

/* Recognise one recorded clip into text.  Returns VG_VC_OK and a NUL
 * terminated transcript, or VG_VC_EMPTY when the service heard nothing. */
enum vg_vc_result vg_voice_cloud_asr(const char *clip_path,
                                     const vela_req_ctl_t *ctl,
                                     char *text, size_t cap,
                                     struct vg_vc_info *info);

/* Recognise a recording that is still in RAM.  The samples are 16 kHz mono
 * 16-bit; the request carries them as one mono WAV.  This is the path the
 * product round uses, so the audio never has to be written to the eMMC
 * twice. */
enum vg_vc_result vg_voice_cloud_asr_buffer(const int16_t *mono,
                                            size_t mono_bytes,
                                            const vela_req_ctl_t *ctl,
                                            char *text, size_t cap,
                                            struct vg_vc_info *info);

/* Synthesise text into wav_out at 16 kHz stereo.  Returns VG_VC_OK only when
 * the whole file was written; on failure no partial file is left behind. */
enum vg_vc_result vg_voice_cloud_tts(const char *text, const char *wav_out,
                                     const vela_req_ctl_t *ctl,
                                     struct vg_vc_info *info);

/* Streaming variant: decoded 24 kHz mono PCM is handed to push(ctx, ...)
 * as it arrives, so playback can start while the SSE stream is open.  The
 * consumer must accept whole-sample chunks and may block on its own queue
 * (the transport then applies TCP backpressure); it returns non-zero to
 * fail the request.  Returns VG_VC_OK only when the stream completed
 * cleanly; VG_VC_TRUNCATED when it was cut, with info->partial set
 * whenever any audio had already been delivered. */
enum vg_vc_result vg_voice_cloud_tts_stream(
    const char *text,
    int (*push)(void *ctx, const uint8_t *pcm24k, size_t bytes), void *push_ctx,
    const vela_req_ctl_t *ctl, struct vg_vc_info *info);

/* Remove the recording and every intermediate file.  Called after the round
 * ends, on success, failure and cancel alike. */
#ifdef CONFIG_VG_VOICE_TEST_FIXTURE
int vg_voice_cloud_fixture(const char *mode);
#endif

void vg_voice_cloud_cleanup(void);

#endif /* __APP_VELAGUARD_VG_VOICE_CLOUD_H */
