/****************************************************************************
 * app/velaguard/vg_alarm_eval.c
 ****************************************************************************/

#include "vg_alarm_eval.h"

#include <string.h>

#ifndef VG_ALARM_EQ_EPS
#  define VG_ALARM_EQ_EPS 0.0005f
#endif

uint8_t vg_alarm_fail_n(const struct vg_alarm_rule *rule)
{
  if (rule == NULL || rule->fail_n < 1)
    {
      return 3;
    }

  if (rule->fail_n > 20)
    {
      return 20;
    }

  return rule->fail_n;
}

int vg_alarm_kind_rank(enum vg_alarm_kind kind)
{
  switch (kind)
    {
      case VG_ALARM_KIND_CRIT:
        return 3;
      case VG_ALARM_KIND_OFFLINE:
        return 2;
      case VG_ALARM_KIND_WARN:
        return 1;
      default:
        return 0;
    }
}

static int eq_hit(float value, float thr)
{
  float d = value - thr;

  if (d < 0.0f)
    {
      d = -d;
    }

  return d <= VG_ALARM_EQ_EPS;
}

enum vg_alarm_kind vg_alarm_eval(const struct vg_alarm_rule *rule,
                                 int last_ok, uint8_t fail_streak,
                                 float value, float *thr_out)
{
  uint8_t need;
  const char *cmp;

  if (thr_out != NULL)
    {
      *thr_out = 0.0f;
    }

  if (rule == NULL)
    {
      return VG_ALARM_KIND_NONE;
    }

  need = vg_alarm_fail_n(rule);
  if (!last_ok)
    {
      if (fail_streak >= need)
        {
          return VG_ALARM_KIND_OFFLINE;
        }

      return VG_ALARM_KIND_NONE;
    }

  cmp = rule->cmp;
  if (cmp[0] == '\0')
    {
      return VG_ALARM_KIND_NONE;
    }

  if (strcmp(cmp, "eq") == 0)
    {
      if (rule->has_crit && eq_hit(value, rule->crit))
        {
          if (thr_out != NULL)
            {
              *thr_out = rule->crit;
            }

          return VG_ALARM_KIND_CRIT;
        }

      if (rule->has_warn && eq_hit(value, rule->warn))
        {
          if (thr_out != NULL)
            {
              *thr_out = rule->warn;
            }

          return VG_ALARM_KIND_WARN;
        }

      return VG_ALARM_KIND_NONE;
    }

  if (strcmp(cmp, "ge") == 0)
    {
      if (rule->has_crit && value >= rule->crit)
        {
          if (thr_out != NULL)
            {
              *thr_out = rule->crit;
            }

          return VG_ALARM_KIND_CRIT;
        }

      if (rule->has_warn && value >= rule->warn)
        {
          if (thr_out != NULL)
            {
              *thr_out = rule->warn;
            }

          return VG_ALARM_KIND_WARN;
        }

      return VG_ALARM_KIND_NONE;
    }

  if (strcmp(cmp, "le") == 0)
    {
      if (rule->has_crit && value <= rule->crit)
        {
          if (thr_out != NULL)
            {
              *thr_out = rule->crit;
            }

          return VG_ALARM_KIND_CRIT;
        }

      if (rule->has_warn && value <= rule->warn)
        {
          if (thr_out != NULL)
            {
              *thr_out = rule->warn;
            }

          return VG_ALARM_KIND_WARN;
        }

      return VG_ALARM_KIND_NONE;
    }

  return VG_ALARM_KIND_NONE;
}
