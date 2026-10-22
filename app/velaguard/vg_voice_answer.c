/****************************************************************************
 * app/velaguard/vg_voice_answer.c
 *
 * BVO-10: render one validated snapshot as display text and speech text.
 *
 * Both strings come from the same struct vg_voice_facts, so a number that is
 * spoken also appears in the list the user can scroll.  The display text may
 * be the whole anomaly set; the speech text is a short summary of the same
 * set, worst first.
 *
 * Every number printed here is a field of the snapshot.  That is what makes
 * vg_va_text_facts_consistent() usable as an admission check for a
 * model-written answer: the check accepts a number only when the snapshot
 * contains it, so an invented reading is rejected instead of shown.
 ****************************************************************************/

#include "vg_voice_answer.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vg_agent_query.h"   /* VG_AGENT_QUERY_STALE_MS */

#define VA_LINE_MAX 192

/* ── small formatting helpers ───────────────────────────────────── */

static int va_append(char *buf, size_t cap, size_t *off, const char *fmt, ...)
{
  va_list ap;
  int n;

  if (buf == NULL || cap == 0 || off == NULL || *off >= cap - 1)
    {
      return 0;
    }

  va_start(ap, fmt);
  n = vsnprintf(buf + *off, cap - *off, fmt, ap);
  va_end(ap);

  if (n < 0)
    {
      return 0;
    }

  if ((size_t)n >= cap - *off)
    {
      /* The buffer filled up.  The caller keeps what fits; the display is
       * still a prefix of a correct list, never a different list. */

      *off = cap - 1;
      return (int)(cap - 1 - *off);
    }

  *off += (size_t)n;
  return n;
}

/* One decimal, no trailing .0: 45 -> "45", 45.25 -> "45.3". */

static void va_num(char *out, size_t cap, float v)
{
  long whole = (long)v;

  if ((float)whole == v && whole > -1000000L && whole < 1000000L)
    {
      snprintf(out, cap, "%ld", whole);
    }
  else
    {
      snprintf(out, cap, "%.1f", v);
    }
}

/* Seconds, rounded to nearest.  Printed as an integer so the value can be
 * matched against the snapshot by the consistency check. */

static uint32_t va_secs(uint32_t ms)
{
  return (ms + 500u) / 1000u;
}

/* A speech buffer that filled up is cut back to the last sentence or clause
 * boundary.  A cut in the middle of a number would be spoken as a different
 * number, which is the one thing a summary must not do. */

static void va_trim_speech(char *s, size_t cap)
{
  static const char *const delims[] = { "。", "，", "；", "\n", NULL };
  size_t n;
  size_t best = 0;
  int i;

  if (s == NULL || cap == 0)
    {
      return;
    }

  n = strlen(s);
  if (n + 1 < cap)
    {
      return;
    }

  for (i = 0; delims[i] != NULL; i++)
    {
      size_t dl = strlen(delims[i]);
      const char *p = s;

      while ((p = strstr(p, delims[i])) != NULL)
        {
          size_t end = (size_t)(p - s) + dl;

          if (end <= n && end > best)
            {
              best = end;
            }

          p += dl;
        }
    }

  if (best > 0)
    {
      s[best] = '\0';
    }
}

const char *vg_va_rank_label(unsigned rank)
{
  switch (rank)
    {
      case 0: return "严重";
      case 1: return "警告";
      case 2: return "离线";
      case 3: return "过期";
      default: return "异常";
    }
}

static const char *va_kind_label(enum vg_runtime_kind kind)
{
  switch (kind)
    {
      case VG_RUNTIME_KIND_THRESHOLD: return "阈值";
      case VG_RUNTIME_KIND_OFFLINE:   return "离线";
      default:                        return "状态";
    }
}

static const char *va_cmp_symbol(const char *cmp)
{
  if (cmp == NULL || cmp[0] == '\0')
    {
      return "超过";
    }

  if (strcmp(cmp, "ge") == 0)
    {
      return "≥";
    }

  if (strcmp(cmp, "le") == 0)
    {
      return "≤";
    }

  if (strcmp(cmp, "eq") == 0)
    {
      return "=";
    }

  return "超过";
}

/* ── point ──────────────────────────────────────────────────────── */

int vg_va_render_point(const struct vg_voice_facts *f,
                       char *display, size_t dcap,
                       char *speech, size_t scap)
{
  char value[24];
  char warn[24];
  char crit[24];
  size_t d = 0;
  size_t s = 0;
  bool stale;

  if (f == NULL || display == NULL || dcap == 0 || speech == NULL || scap == 0)
    {
      return -1;
    }

  display[0] = '\0';
  speech[0] = '\0';

  if (!f->has_point)
    {
      return -1;
    }

  va_num(value, sizeof(value), f->point_value);
  stale = f->point_age_ms > VG_AGENT_QUERY_STALE_MS;

  /* Display: identity, reading, freshness, thresholds. */

  if (f->point_online && f->point_has_value)
    {
      va_append(display, dcap, &d, "%s %s：%s %s", f->point_id,
                f->point_name, value, f->point_unit);
    }
  else if (f->point_online)
    {
      va_append(display, dcap, &d, "%s %s：在线，本次快照没有数值",
                f->point_id, f->point_name);
    }
  else
    {
      va_append(display, dcap, &d, "%s %s：离线，没有当前读数",
                f->point_id, f->point_name);
    }

  va_append(display, dcap, &d, "\n采样 %u 秒前%s\n", va_secs(f->point_age_ms),
            stale ? "，已超过新鲜度上限" : "");

  if (f->point_has_warn || f->point_has_crit)
    {
      va_append(display, dcap, &d, "阈值：");

      if (f->point_has_warn)
        {
          va_num(warn, sizeof(warn), f->point_warn);
          va_append(display, dcap, &d, "警告 %s %s %s", va_cmp_symbol("ge"),
                    warn, f->point_unit);
        }

      if (f->point_has_crit)
        {
          va_num(crit, sizeof(crit), f->point_crit);
          va_append(display, dcap, &d, "%s严重 %s %s %s",
                    f->point_has_warn ? "，" : "",
                    va_cmp_symbol("ge"), crit, f->point_unit);
        }

      va_append(display, dcap, &d, "\n");
    }

  if (f->config_version != 0)
    {
      va_append(display, dcap, &d, "数据来自已确认点表，配置版本 %u",
                f->config_version);
    }
  else
    {
      va_append(display, dcap, &d, "数据来自已确认点表");
    }

  /* Speech: same facts, shorter. */

  if (!f->point_online)
    {
      va_append(speech, scap, &s, "%s 离线，最近成功采样 %u 秒前。",
                f->point_name, va_secs(f->point_age_ms));
    }
  else if (!f->point_has_value)
    {
      va_append(speech, scap, &s, "%s 在线，但本次快照没有数值。",
                f->point_name);
    }
  else
    {
      va_append(speech, scap, &s, "%s %s %s，%s，采样 %u 秒前。",
                f->point_name, value, f->point_unit,
                stale ? "数据已过期" : "在线", va_secs(f->point_age_ms));
    }

  va_trim_speech(speech, scap);
  return (int)d;
}

/* ── anomalies ──────────────────────────────────────────────────── */

static void va_anomaly_line(char *buf, size_t cap, int index,
                            const struct vg_runtime_anomaly *a)
{
  char value[24];
  char threshold[24];
  size_t off = 0;

  buf[0] = '\0';
  va_num(value, sizeof(value), a->value);
  va_num(threshold, sizeof(threshold), a->threshold);

  va_append(buf, cap, &off, "%d. [%s] %s", index, vg_va_rank_label(a->rank),
            a->id);

  if ((a->flags & VG_RUNTIME_ANOMALY_F_THRESHOLD) != 0)
    {
      if (a->has_value)
        {
          va_append(buf, cap, &off, " %s", value);
        }

      va_append(buf, cap, &off, "，阈值 %s %s", va_cmp_symbol(a->cmp),
                threshold);
    }

  if ((a->flags & VG_RUNTIME_ANOMALY_F_OFFLINE) != 0)
    {
      va_append(buf, cap, &off, "，离线，最近成功采样 %u 秒前",
                va_secs(a->age_ms));
    }

  if ((a->flags & VG_RUNTIME_ANOMALY_F_STALE) != 0)
    {
      va_append(buf, cap, &off, "，数据 %u 秒未更新", va_secs(a->age_ms));
    }

  if (a->since_ms > 0)
    {
      va_append(buf, cap, &off, "，已持续 %u 秒", va_secs(a->since_ms));
    }
}

int vg_va_render_anomalies(const struct vg_voice_facts *f,
                           char *display, size_t dcap,
                           char *speech, size_t scap)
{
  char line[VA_LINE_MAX];
  char value[24];
  size_t d = 0;
  size_t s = 0;
  int i;
  int listed;

  if (f == NULL || display == NULL || dcap == 0 || speech == NULL || scap == 0)
    {
      return -1;
    }

  display[0] = '\0';
  speech[0] = '\0';

  listed = f->anomaly_n;

  /* No point has been sampled yet, so an empty list is "unknown", not
   * "healthy".  This happens for the first acquisition cycle after boot. */

  if (f->anomaly_seen <= 0)
    {
      va_append(display, dcap, &d,
                "采集尚未完成，暂时无法确认异常（已确认点位 %d 个）。",
                f->anomaly_total);
      va_append(speech, scap, &s, "采集尚未完成，暂时无法确认异常。");
      return (int)d;
    }

  if (f->anomaly_total <= 0)
    {
      va_append(display, dcap, &d, "当前没有异常点位，共 %d 个已确认点位。",
                f->anomaly_total);
      va_append(speech, scap, &s, "当前没有异常点位。");
      return (int)d;
    }

  va_append(display, dcap, &d, "当前异常 %d 个点位（最旧采样 %u 秒前）\n",
            f->anomaly_total, va_secs(f->data_age_ms));

  for (i = 0; i < listed; i++)
    {
      va_anomaly_line(line, sizeof(line), i + 1, &f->anomalies[i]);
      va_append(display, dcap, &d, "%s\n", line);
    }

  if (f->anomaly_total > listed)
    {
      va_append(display, dcap, &d, "其余 %d 个未列出\n",
                f->anomaly_total - listed);
    }

  /* Speech: total and the first three.  Severity order is the order the
   * snapshot already uses, so the summary never promotes a lesser item. */

  va_append(speech, scap, &s, "当前有 %d 个异常点位。", f->anomaly_total);

  {
    int top = listed < 3 ? listed : 3;

    for (i = 0; i < top; i++)
      {
        const struct vg_runtime_anomaly *a = &f->anomalies[i];

        if ((a->flags & VG_RUNTIME_ANOMALY_F_THRESHOLD) != 0)
          {
            va_num(value, sizeof(value), a->value);
            va_append(speech, scap, &s, "第%d个，%s，%s %s，超过%s阈值。",
                      i + 1, a->id, value,
                      (a->flags & VG_RUNTIME_ANOMALY_F_OFFLINE) ? "且离线"
                                                               : "",
                      vg_va_rank_label(a->rank));
          }
        else if ((a->flags & VG_RUNTIME_ANOMALY_F_OFFLINE) != 0)
          {
            va_append(speech, scap, &s, "第%d个，%s，离线。", i + 1, a->id);
          }
        else
          {
            va_append(speech, scap, &s, "第%d个，%s，数据过期。", i + 1,
                      a->id);
          }
      }
  }

  va_trim_speech(speech, scap);
  return (int)d;
}

/* ── recovery ───────────────────────────────────────────────────── */

int vg_va_render_recovery(const struct vg_voice_facts *f,
                          char *display, size_t dcap,
                          char *speech, size_t scap)
{
  char value[24];
  char threshold[24];
  size_t d = 0;
  size_t s = 0;
  const struct vg_runtime_recovery *r;

  if (f == NULL || display == NULL || dcap == 0 || speech == NULL || scap == 0)
    {
      return -1;
    }

  display[0] = '\0';
  speech[0] = '\0';

  if (!f->has_recovery)
    {
      return -1;
    }

  r = &f->recovery;
  va_num(value, sizeof(value), r->value);
  va_num(threshold, sizeof(threshold), r->threshold);

  switch (r->state)
    {
      case VG_RUNTIME_RECOVERY_ACTIVE:
        va_append(display, dcap, &d,
                  "%s 的%s告警仍在进行中（本次启动）\n当前值 %s，阈值 %s %s\n"
                  "触发于本次启动第 %u 秒",
                  r->id, va_kind_label(r->kind), value,
                  va_cmp_symbol("ge"), threshold, va_secs(r->raise_ms));
        va_append(speech, scap, &s, "%s 的%s告警还在，当前值 %s。", r->id,
                  va_kind_label(r->kind), value);
        break;

      case VG_RUNTIME_RECOVERY_RECOVERED:
        va_append(display, dcap, &d,
                  "%s 的%s告警已恢复（本次启动）\n"
                  "触发于本次启动第 %u 秒，恢复于第 %u 秒，持续 %u 秒\n"
                  "事件记录完整，触发与恢复属于同一实例 %u",
                  r->id, va_kind_label(r->kind), va_secs(r->raise_ms),
                  va_secs(r->clear_ms), va_secs(r->duration_ms), r->episode);
        va_append(speech, scap, &s, "%s 的%s告警已经恢复，持续 %u 秒。",
                  r->id, va_kind_label(r->kind), va_secs(r->duration_ms));
        break;

      case VG_RUNTIME_RECOVERY_RECOVERED_PARTIAL:
        va_append(display, dcap, &d,
                  "%s 的%s告警有恢复记录（本次启动）\n"
                  "触发记录已被事件环覆盖，无法给出持续时长\n"
                  "恢复事件实例 %u",
                  r->id, va_kind_label(r->kind), r->episode);
        va_append(speech, scap, &s,
                  "%s 的%s告警有恢复记录，但触发记录已被覆盖，无法给出持续时长。",
                  r->id, va_kind_label(r->kind));
        break;

      case VG_RUNTIME_RECOVERY_UNKNOWN:
      default:
        va_append(display, dcap, &d,
                  "无法确认 %s 的%s告警恢复情况\n"
                  "本次启动没有保留相关事件，当前正常不能证明告警曾经恢复",
                  r->id, va_kind_label(r->kind));
        va_append(speech, scap, &s,
                  "无法确认 %s 的告警恢复情况，本次启动没有保留相关事件。",
                  r->id);
        break;
    }

  if (!r->history_complete &&
      r->state != VG_RUNTIME_RECOVERY_RECOVERED_PARTIAL &&
      r->state != VG_RUNTIME_RECOVERY_UNKNOWN)
    {
      va_append(display, dcap, &d, "\n事件环已覆盖部分历史，结论仅基于保留记录");
    }

  va_trim_speech(speech, scap);
  return (int)d;
}

/* ── consistency check ──────────────────────────────────────────── */

/* A number in a model answer is accepted when the snapshot contains the same
 * value.  The set is small and fixed, so a linear scan is cheaper than a
 * table and leaves no state behind. */

#define VA_NUM_MAX 96

struct va_numbers
{
  double v[VA_NUM_MAX];
  int    n;
};

static void va_add(struct va_numbers *set, double v)
{
  if (set->n < VA_NUM_MAX)
    {
      set->v[set->n++] = v;
    }
}

static void va_add_ms(struct va_numbers *set, uint32_t ms)
{
  va_add(set, (double)va_secs(ms));
}

static void va_collect(const struct vg_voice_facts *f, struct va_numbers *set)
{
  int i;

  memset(set, 0, sizeof(*set));

  va_add(set, (double)f->anomaly_total);
  va_add(set, (double)f->anomaly_n);
  va_add(set, (double)f->config_version);
  va_add_ms(set, f->data_age_ms);

  for (i = 0; i < f->anomaly_n; i++)
    {
      const struct vg_runtime_anomaly *a = &f->anomalies[i];

      va_add(set, (double)(i + 1));      /* list index */
      va_add(set, (double)a->value);
      va_add(set, (double)a->threshold);
      va_add_ms(set, a->age_ms);
      va_add_ms(set, a->since_ms);
      va_add(set, (double)a->slave);
    }

  if (f->has_point)
    {
      va_add(set, (double)f->point_value);
      va_add(set, (double)f->point_warn);
      va_add(set, (double)f->point_crit);
      va_add_ms(set, f->point_age_ms);
    }

  if (f->has_recovery)
    {
      va_add(set, (double)f->recovery.value);
      va_add(set, (double)f->recovery.threshold);
      va_add_ms(set, f->recovery.raise_ms);
      va_add_ms(set, f->recovery.clear_ms);
      va_add_ms(set, f->recovery.duration_ms);
      va_add(set, (double)f->recovery.episode);
      va_add(set, (double)f->recovery.kind);
    }
}

static bool va_known(const struct va_numbers *set, double v)
{
  int i;

  for (i = 0; i < set->n; i++)
    {
      double d = set->v[i] - v;

      if (d < 0)
        {
          d = -d;
        }

      /* Values are printed with one decimal, so a round trip differs by at
       * most half of that.  Integers compare exactly enough at this width. */

      if (d <= 0.06)
        {
          return true;
        }
    }

  return false;
}

/* True when text[i] belongs to an identifier rather than to a quantity.
 * A point id such as ups_2 must not be read as the number 2. */

static bool va_ident_char(char c)
{
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
         (c >= '0' && c <= '9') || c == '_';
}

bool vg_va_text_facts_consistent(const struct vg_voice_facts *f,
                                 const char *text)
{
  struct va_numbers set;
  size_t i = 0;

  if (f == NULL)
    {
      return false;
    }

  if (text == NULL || text[0] == '\0')
    {
      return true;
    }

  va_collect(f, &set);

  while (text[i] != '\0')
    {
      /* Hex: only the boot identifier is ever written this way, and it is
       * not part of the fact set, so accept it as an identifier. */

      if (text[i] == '0' && (text[i + 1] == 'x' || text[i + 1] == 'X'))
        {
          i += 2;
          while ((text[i] >= '0' && text[i] <= '9') ||
                 (text[i] >= 'a' && text[i] <= 'f') ||
                 (text[i] >= 'A' && text[i] <= 'F'))
            {
              i++;
            }

          continue;
        }

      if (text[i] >= '0' && text[i] <= '9')
        {
          char *end = NULL;
          double v;
          bool ident = (i > 0 && va_ident_char(text[i - 1]));

          v = strtod(text + i, &end);
          if (end == text + i)
            {
              i++;
              continue;
            }

          if (!ident && *end != '\0' && va_ident_char(*end))
            {
              ident = true;
            }

          if (!ident && !va_known(&set, v))
            {
              return false;
            }

          i = (size_t)(end - text);
          continue;
        }

      i++;
    }

  return true;
}
