/****************************************************************************
 * app/velaguard/vg_runtime.h
 *
 * Since-boot runtime report inputs: per-point online time + anomaly events.
 ****************************************************************************/

#ifndef __APP_VELAGUARD_VG_RUNTIME_H
#define __APP_VELAGUARD_VG_RUNTIME_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>

#ifndef VG_RUNTIME_MAX_POINTS
#  define VG_RUNTIME_MAX_POINTS 32
#endif

#ifndef VG_RUNTIME_EVENT_MAX
#  define VG_RUNTIME_EVENT_MAX 32
#endif

#ifndef VG_RUNTIME_REPORT_POINTS
#  define VG_RUNTIME_REPORT_POINTS 8
#endif

#ifndef VG_RUNTIME_REPORT_EVENTS
#  define VG_RUNTIME_REPORT_EVENTS 8
#endif

enum vg_runtime_kind
{
  VG_RUNTIME_KIND_NONE = 0,
  VG_RUNTIME_KIND_OFFLINE,
  VG_RUNTIME_KIND_THRESHOLD
};

enum vg_runtime_action
{
  VG_RUNTIME_ACT_RAISE = 0,
  VG_RUNTIME_ACT_CLEAR
};

void vg_runtime_init(void);
/* 由已确认表发布时调用；更换配置会使运行态和事件历史失效，不制造 CLEAR。
 * 之后仅接受同一配置代际的采样，旧代际和无代际采样均不能写入。 */
void vg_runtime_config_changed(uint32_t version);
int vg_runtime_note_sample_versioned(uint32_t version, const char *id,
                            uint8_t slave, bool online, float value,
                            enum vg_runtime_kind kind, float threshold,
                            const char *cmp, const char *level);

/* Note one sample after HMI/model update. kind is active alarm kind or NONE. */
void vg_runtime_note_sample(const char *id, uint8_t slave, bool online,
                            float value, enum vg_runtime_kind kind,
                            float threshold, const char *cmp,
                            const char *level);

/* Format Agent-facing dump into out (NUL-terminated). Returns bytes written. */
int vg_runtime_format_dump(char *out, size_t out_sz);

/* Print dump lines to FILE (NSH / Agent). */
void vg_runtime_fprint_dump(FILE *fp);

/* Print the screen report: 通信 / 点位在线 / 异常, scannable. */
void vg_runtime_fprint_report(FILE *fp);

/* Same report into a caller buffer, for the agent tool that answers a spoken
 * 运行报告 question.  Truncates at cap and NUL-terminates.  Returns bytes
 * written (excluding NUL), or 0 when out/cap is unusable. */
int vg_runtime_format_report(char *out, size_t cap);

uint32_t vg_runtime_uptime_s(void);

/* Write the screen report to path. Returns 0 or -errno. */
int vg_runtime_write_report(const char *path);

/* ── Anomaly and recovery queries (BVO-09) ─────────────────────────
 *
 * The report path above is bounded for the screen: it prints at most eight
 * points and eight events.  A voice answer must not reuse it, because eight
 * is a display budget, not the set of anomalies.  These functions return the
 * whole set.
 *
 * Categories are flags on one entry per point, so a point that is offline and
 * also past the stale window is one anomaly, not two, and the total counts
 * points.  A point appears here only after vg_runtime_note_sample() has seen
 * it at least once, which the acquisition loop does for every confirmed point
 * on every cycle.
 */

#define VG_RUNTIME_ANOMALY_F_THRESHOLD 0x01u
#define VG_RUNTIME_ANOMALY_F_OFFLINE   0x02u
#define VG_RUNTIME_ANOMALY_F_STALE     0x04u

struct vg_runtime_anomaly
{
  char     id[24];
  uint8_t  slave;
  uint8_t  flags;       /* VG_RUNTIME_ANOMALY_F_* */
  uint8_t  rank;        /* 0 crit, 1 warn, 2 offline, 3 stale */
  bool     has_value;
  float    value;
  float    threshold;
  char     cmp[4];
  char     level[8];
  uint32_t age_ms;      /* since the last successful sample */
  uint32_t since_ms;    /* how long the point has been in this state */
};

/* Fill out[0..max) with the current anomalies, worst first, and always store
 * the full count in *total_out (which may exceed max).  Returns the number of
 * entries written, or -1 on a bad argument.  stale_ms is the freshness bound;
 * 0 disables the stale category.
 */

int vg_runtime_anomalies(struct vg_runtime_anomaly *out, int max,
                         int *total_out, uint32_t stale_ms);

/* Recovery is answered from the event ring only.  A point that is normal now
 * but whose RAISE and CLEAR were both evicted reports UNKNOWN: current
 * normality is not evidence that an alarm was ever raised.
 *
 * ACTIVE              the latest retained event for this point/kind is RAISE
 * RECOVERED           CLEAR found and its matching RAISE is still retained
 * RECOVERED_PARTIAL   CLEAR found but the RAISE is gone; the duration is not
 *                     known, so has_duration is false
 * UNKNOWN             no retained event and no active state
 */

enum vg_runtime_recovery_state
{
  VG_RUNTIME_RECOVERY_UNKNOWN = 0,
  VG_RUNTIME_RECOVERY_ACTIVE,
  VG_RUNTIME_RECOVERY_RECOVERED,
  VG_RUNTIME_RECOVERY_RECOVERED_PARTIAL
};

struct vg_runtime_recovery
{
  char     id[24];
  enum vg_runtime_kind kind;
  enum vg_runtime_recovery_state state;
  bool     history_complete;
  bool     has_duration;
  uint32_t duration_ms;
  uint32_t raise_ms;
  uint32_t clear_ms;
  time_t   raise_wall;
  time_t   clear_wall;
  float    value;
  float    threshold;
  char     cmp[4];
  uint32_t episode;
};

int vg_runtime_recovery(const char *id, enum vg_runtime_kind kind,
                        struct vg_runtime_recovery *out);

/* Identifier of this power cycle.  The event ring lives in RAM, so every
 * retained event belongs to this boot; the value lets an answer say which
 * boot it is describing instead of implying history across restarts. */

uint32_t vg_runtime_boot_id(void);

/* Number of events the ring had to overwrite.  A non-zero value means the
 * oldest events are gone, which is what turns a would-be RECOVERED into
 * RECOVERED_PARTIAL. */

uint32_t vg_runtime_events_dropped(void);

/* Number of points the runtime has seen at least once.  The acquisition
 * loop feeds one full cycle at a time, so this is 0 until the first cycle
 * ends.  An empty anomaly list with a non-empty point table means the
 * first cycle has not finished, which is not the same answer as no
 * anomalies. */

int vg_runtime_points_seen(void);

#endif /* __APP_VELAGUARD_VG_RUNTIME_H */
