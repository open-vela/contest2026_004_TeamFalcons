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

#endif /* __APP_VELAGUARD_VG_RUNTIME_H */
