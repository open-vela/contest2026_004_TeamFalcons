/****************************************************************************
 * app/velaguard/vg_voice_intent.h
 *
 * BVO-10: the closed set of things a spoken question may ask for.
 *
 *   current anomalies   rules alarms, offline points, stale data
 *   one point reading   a value, unit and freshness for one point
 *   alarm recovery      whether an alarm that was raised has cleared
 *
 * Anything else is refused before an agent round starts.  That includes every
 * write, configuration, alarm-clearing and output-control request: the voice
 * path is read-only, and the refusal has to happen here rather than in the
 * prompt, because a prompt is not an access control.
 *
 * The classifier is deterministic.  A closed three-intent set does not need a
 * model to decide which intent a sentence has, and a deterministic decision
 * is the only one a host test can pin down.
 ****************************************************************************/

#ifndef __APP_VELAGUARD_VG_VOICE_INTENT_H
#define __APP_VELAGUARD_VG_VOICE_INTENT_H

#include <stdbool.h>
#include <stddef.h>

#include "vg_agent_query.h"

enum vg_voice_intent
{
  VG_VI_UNKNOWN = 0,   /* not one of the three; ask again */
  VG_VI_ANOMALY,
  VG_VI_POINT,
  VG_VI_RECOVERY,
  VG_VI_REFUSED        /* a write, control or configuration request */
};

struct vg_voice_intent_result
{
  enum vg_voice_intent intent;
  bool has_token;                       /* a point phrase was extracted */
  char token[VG_AGENT_QUERY_STR_MAX];
};

/* Classify one recognized sentence.  out may be NULL. */
enum vg_voice_intent vg_vi_classify(const char *text,
                                    struct vg_voice_intent_result *out);

/* Remove politeness and question fillers, leaving the part that may name a
 * point.  Returns the token length, or 0 when nothing is left. */
int vg_vi_point_token(const char *text, char *out, size_t cap);

/* Map Chinese digit homophones to ASCII digits (零/〇 -> 0, 幺/一 -> 1, ...).
 * Applied to ids only: a name that legitimately contains 一 must not be
 * rewritten. */
int vg_vi_normalize_digits(const char *in, char *out, size_t cap);

/* Resolve the sentence to one point.
 *
 * Order: exact id, exact name, unique substring, then the digit-normalized
 * token against ids only.  Two points sharing a name are ambiguous; the
 * caller must ask for the id instead of choosing one.  The token that was
 * actually matched is copied to token when the caller provides a buffer.
 */
enum vg_agent_query_rc
vg_vi_resolve_point(const char *text, const struct vg_agent_point *points,
                    int n, struct vg_agent_query_result *out,
                    char *token, size_t token_cap);

#endif /* __APP_VELAGUARD_VG_VOICE_INTENT_H */
