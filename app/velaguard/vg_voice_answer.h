/****************************************************************************
 * app/velaguard/vg_voice_answer.h
 *
 * BVO-10: turn one validated snapshot into what the user sees and hears.
 *
 * The display text may be long (the whole anomaly list); the speech text is
 * a short summary.  Both are rendered from the same struct vg_voice_facts, so
 * they cannot disagree about a value, a threshold or a total.
 *
 * A model-written answer is admitted only after vg_va_text_facts_consistent()
 * accepts every number in it.  When it does not, the caller uses the
 * deterministic text from these renderers instead of showing an invented
 * value.
 ****************************************************************************/

#ifndef __APP_VELAGUARD_VG_VOICE_ANSWER_H
#define __APP_VELAGUARD_VG_VOICE_ANSWER_H

#include <stdbool.h>
#include <stddef.h>

#include "vg_voice_facts.h"

/* All renderers return the display length, or -1 on a bad argument.  Both
 * buffers are always NUL-terminated.  A speech buffer too small for the full
 * summary is truncated at a line boundary, never mid-number. */

int vg_va_render_point(const struct vg_voice_facts *f,
                       char *display, size_t dcap,
                       char *speech, size_t scap);

int vg_va_render_anomalies(const struct vg_voice_facts *f,
                           char *display, size_t dcap,
                           char *speech, size_t scap);

int vg_va_render_recovery(const struct vg_voice_facts *f,
                          char *display, size_t dcap,
                          char *speech, size_t scap);

/* True when every number in text appears in the snapshot (values,
 * thresholds, ages, durations, the anomaly total, or an item index).  Text
 * without numbers passes: it asserts no fact. */
bool vg_va_text_facts_consistent(const struct vg_voice_facts *f,
                                 const char *text);

/* 严重 / 警告 / 离线 / 过期, by anomaly rank. */
const char *vg_va_rank_label(unsigned rank);

#endif /* __APP_VELAGUARD_VG_VOICE_ANSWER_H */
