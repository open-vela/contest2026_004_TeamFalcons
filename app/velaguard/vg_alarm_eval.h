/****************************************************************************
 * Runtime point-table alarm compare (no I/O).
 *
 * Empty cmp, or ge/le with both thresholds missing, means no analog alarm.
 * Offline counts failures in the last 8 distinct samples (minimum 5).
 ****************************************************************************/

#ifndef __APP_VELAGUARD_VG_ALARM_EVAL_H
#define __APP_VELAGUARD_VG_ALARM_EVAL_H

#include <stdint.h>

enum vg_alarm_kind
{
  VG_ALARM_KIND_NONE = 0,
  VG_ALARM_KIND_WARN,
  VG_ALARM_KIND_CRIT,
  VG_ALARM_KIND_OFFLINE
};

struct vg_alarm_rule
{
  char cmp[4];
  uint8_t has_warn;
  uint8_t has_crit;
  float warn;
  float crit;
  uint8_t fail_n;
};

struct vg_alarm_window
{
  uint8_t bits;
  uint8_t pos;
  uint8_t failures;
};

/* Exactly one call per new acquisition sample. Successful reads always
 * recover offline state; threshold evaluation still uses the real value. */
enum vg_alarm_kind vg_alarm_sample(const struct vg_alarm_rule *rule,
                                   struct vg_alarm_window *window,
                                   int online, float value, float *thr_out);

/* 0 = none. *thr_out is the threshold that fired (0 for offline). */
enum vg_alarm_kind vg_alarm_eval(const struct vg_alarm_rule *rule,
                                 int last_ok, uint8_t failed_samples,
                                 float value, float *thr_out);

uint8_t vg_alarm_fail_n(const struct vg_alarm_rule *rule);
int vg_alarm_kind_rank(enum vg_alarm_kind kind);

#endif
