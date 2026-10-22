/****************************************************************************
 * app/velaguard/vg_voice_cloud.h
 *
 * BVO-11: cloud speech adaptation.
 *
 *   ASR   the recorded WAV is downmixed, base64-encoded in pieces and sent
 *         as one JSON request; the transcript comes back in the reply.
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
  VG_VC_NO_TEXT     /* TTS asked to speak an empty string */
};

struct vg_vc_info
{
  int      status;       /* HTTP status, 0 when no reply */
  uint32_t req_bytes;
  uint32_t resp_bytes;
  uint32_t audio_bytes;  /* TTS: PCM before resampling */
  uint32_t elapsed_ms;
};

const char *vg_vc_result_name(enum vg_vc_result r);

/* Recognise one recorded clip into text.  Returns VG_VC_OK and a NUL
 * terminated transcript, or VG_VC_EMPTY when the service heard nothing. */
enum vg_vc_result vg_voice_cloud_asr(const char *clip_path,
                                     const vela_req_ctl_t *ctl,
                                     char *text, size_t cap,
                                     struct vg_vc_info *info);

/* Synthesise text into wav_out at 16 kHz stereo.  Returns VG_VC_OK only when
 * the whole file was written; on failure no partial file is left behind. */
enum vg_vc_result vg_voice_cloud_tts(const char *text, const char *wav_out,
                                     const vela_req_ctl_t *ctl,
                                     struct vg_vc_info *info);

/* Remove the recording and every intermediate file.  Called after the round
 * ends, on success, failure and cancel alike. */
void vg_voice_cloud_cleanup(void);

#endif /* __APP_VELAGUARD_VG_VOICE_CLOUD_H */
