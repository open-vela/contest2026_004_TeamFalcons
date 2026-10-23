/****************************************************************************
 * app/velaguard/vg_voice_facts.h
 *
 * BVO-10: the snapshot a spoken answer is built from.
 *
 * Tools publish what they read under one sequence number. A voice round
 * performs its resolved read with a thread-owned capture, then renders both
 * display and speech from that private copy. Model prose is not displayed.
 *
 * The snapshot is process memory, replaced as a whole.  It is not a log and
 * is never written to eMMC.
 ****************************************************************************/

#ifndef __APP_VELAGUARD_VG_VOICE_FACTS_H
#define __APP_VELAGUARD_VG_VOICE_FACTS_H

#include <stdbool.h>
#include <stdint.h>

#include "vg_runtime.h"

struct vg_voice_facts
{
  uint32_t seq;             /* bumped on every publish */
  uint32_t boot_id;
  uint32_t config_version;
  uint32_t data_age_ms;     /* age of the live snapshot this came from */

  /* Anomaly answer. */
  int      anomaly_total;   /* points, including those beyond anomaly_n */
  int      anomaly_seen;    /* points the runtime has sampled at least once */
  int      anomaly_n;
  struct vg_runtime_anomaly anomalies[VG_RUNTIME_MAX_POINTS];

  /* Recovery answer. */
  bool     has_recovery;
  struct vg_runtime_recovery recovery;

  /* Point answer. */
  bool     has_point;
  char     point_id[48];
  char     point_name[48];
  char     point_unit[8];
  bool     point_online;
  bool     point_has_value;
  float    point_value;
  uint32_t point_age_ms;
  bool     point_has_warn;
  float    point_warn;
  bool     point_has_crit;
  float    point_crit;
};

void vg_voice_facts_publish_anomalies(const struct vg_runtime_anomaly *items,
                                      int n, int total, int seen,
                                      uint32_t age_ms);
void vg_voice_facts_publish_recovery(const struct vg_runtime_recovery *rec,
                                     uint32_t age_ms);
void vg_voice_facts_publish_point(const char *id, const char *name,
                                  const char *unit, bool online,
                                  bool has_value, float value,
                                  uint32_t age_ms, bool has_warn, float warn,
                                  bool has_crit, float crit);

/* Copy the newest snapshot.  Returns false when nothing has been published. */
bool vg_voice_facts_copy(struct vg_voice_facts *out);

/* Copy only when the newest publication still carries seq. This is a
 * sequence lookup, not request identity; use capture for a voice answer. */
bool vg_voice_facts_copy_seq(uint32_t seq, struct vg_voice_facts *out);

uint32_t vg_voice_facts_seq(void);
void     vg_voice_facts_set_config_version(uint32_t version);

/* Capture only publications made synchronously by the calling thread.  The
 * caller owns out until end; other publishers cannot replace this copy.
 * A failed/blocked tool with no publication makes end return false. */
bool vg_voice_facts_capture_begin(struct vg_voice_facts *out);
bool vg_voice_facts_capture_end(void);

#endif /* __APP_VELAGUARD_VG_VOICE_FACTS_H */
