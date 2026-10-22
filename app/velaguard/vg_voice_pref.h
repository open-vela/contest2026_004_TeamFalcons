/****************************************************************************
 * app/velaguard/vg_voice_pref.h
 *
 * BVO-13: the local speech preference and the file lifecycle around it.
 *
 * The switch is stored on eMMC and restored at boot, so a muted device stays
 * muted after a restart.  Muting skips the TTS request entirely rather than
 * synthesising and discarding audio.
 *
 * The recording and every intermediate file are removed when the round ends,
 * whatever the outcome.  A boot-time sweep removes anything left behind by a
 * crash, so a recording cannot survive a power cycle.  Only the preference
 * and a bounded count of stage outcomes are written; no recording, no
 * transcript and no answer text is ever persisted.
 ****************************************************************************/

#ifndef __APP_VELAGUARD_VG_VOICE_PREF_H
#define __APP_VELAGUARD_VG_VOICE_PREF_H

#include <stdbool.h>

/* Overridable so the host test can use a temporary directory; the board
 * build keeps the eMMC path. */

#ifndef VG_VOICE_DIR
#  define VG_VOICE_DIR "/data/velaguard/voice"
#endif

#define VG_VOICE_PREF_PATH VG_VOICE_DIR "/pref.txt"

/* One round owns four files.  They are declared here because this module is
 * responsible for their lifecycle: the round creates them, cleanup removes
 * them, and the boot sweep removes whatever a crash left behind. */

#define VG_VC_CLIP_PATH      VG_VOICE_DIR "/clip.wav"
#define VG_VC_CLIP_MONO_PATH VG_VOICE_DIR "/clip-mono.wav"
#define VG_VC_TTS_PCM_PATH   VG_VOICE_DIR "/tts-24k.pcm"
#define VG_VC_TTS_WAV_PATH   VG_VOICE_DIR "/tts-16k.wav"

/* Parse "muted=0" / "muted=1".  Returns 0 and sets *muted, or -EINVAL. */
int vg_voice_pref_parse(const char *text, bool *muted);

/* Read the stored preference.  Returns 0, -ENOENT when nothing is stored, or
 * another negative errno.  On -ENOENT *muted is left false. */
int vg_voice_pref_load(bool *muted);

/* Store the preference atomically (tmp + rename). */
int vg_voice_pref_save(bool muted);

/* Apply and persist.  Failures are logged, not propagated: the round still
 * uses the new value for this power cycle. */
void vg_voice_pref_set_muted(bool muted);

/* Remove stale recordings and temporary audio.  Called at boot. */
void vg_voice_pref_sweep(void);

#endif /* __APP_VELAGUARD_VG_VOICE_PREF_H */
