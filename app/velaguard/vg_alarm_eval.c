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
  uint8_t n = rule ? rule->fail_n : 0;
  if (n < 5) return 5;
  if (n > 8) return 8;
  return n;
}

enum vg_alarm_kind vg_alarm_sample(const struct vg_alarm_rule *rule,
                                   struct vg_alarm_window *window,
                                   int online, float value, float *thr_out)
{
  uint8_t mask;
  if (window == NULL) return VG_ALARM_KIND_NONE;
  mask = (uint8_t)(1u << (window->pos % 8));
  if (window->bits & mask) window->failures--;
  if (!online)
    { window->bits |= mask; window->failures++; }
  else window->bits &= (uint8_t)~mask;
  window->pos = (uint8_t)((window->pos + 1) % 8);
  return vg_alarm_eval(rule, online, window->failures, value, thr_out);
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
                                 int last_ok, uint8_t failed_samples,
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
      if (failed_samples >= need)
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
