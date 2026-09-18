/****************************************************************************
 * app/velaguard/vg_agent_query.c
 *
 * Point lookup and rendering for the vg_point_read agent tool.
 ****************************************************************************/

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "vg_agent_query.h"

/* ── bounded append ────────────────────────────────────────────────
 *
 * snprintf returns the length it *would* have written.  Adding that to the
 * offset walks the offset past the end as soon as a line is cut, and the
 * next write then targets memory outside the buffer.  Clamp instead, and
 * report saturation so callers can say the answer was truncated.
 */

static int appendf(char *out, size_t out_sz, int off, const char *fmt, ...)
{
  va_list ap;
  int n;

  if (off < 0 || (size_t)off >= out_sz)
    {
      return (int)out_sz - 1;
    }

  va_start(ap, fmt);
  n = vsnprintf(out + off, out_sz - (size_t)off, fmt, ap);
  va_end(ap);
  if (n < 0)
    {
      return off;
    }

  if ((size_t)n >= out_sz - (size_t)off)
    {
      return (int)out_sz - 1;
    }

  return off + n;
}

static char lower_ascii(char c)
{
  return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
}

/* Case-insensitive byte compare.  Non-ASCII bytes compare verbatim, which is
 * what Chinese point names need: their UTF-8 bytes have no case. */

static int ci_equal(const char *a, const char *b)
{
  while (*a != '\0' && *b != '\0')
    {
      if (lower_ascii(*a) != lower_ascii(*b))
        {
          return 0;
        }

      a++;
      b++;
    }

  return *a == '\0' && *b == '\0';
}

/* Substring search that is not locale-dependent and does not treat any byte
 * as a word boundary, so a query may start mid-name. */

static int ci_contains(const char *hay, const char *needle)
{
  size_t nl;
  const char *p;

  if (needle[0] == '\0')
    {
      return 0;
    }

  nl = strlen(needle);
  for (p = hay; *p != '\0'; p++)
    {
      size_t i;

      for (i = 0; i < nl; i++)
        {
          if (p[i] == '\0' || lower_ascii(p[i]) != lower_ascii(needle[i]))
            {
              break;
            }
        }

      if (i == nl)
        {
          return 1;
        }
    }

  return 0;
}

bool vg_agent_query_is_stale(const struct vg_agent_point *p)
{
  if (p == NULL || !p->has_sample)
    {
      return false;
    }

  return p->age_ms > VG_AGENT_QUERY_STALE_MS;
}

static void set_match(struct vg_agent_query_result *out,
                      enum vg_agent_match_mode mode, int idx)
{
  memset(out, 0, sizeof(*out));
  out->mode = mode;
  out->index = idx;
}

enum vg_agent_query_rc
vg_agent_query_match(const struct vg_agent_point *points, int n,
                     const char *query,
                     struct vg_agent_query_result *out)
{
  int i;
  int sub_idx = -1;
  int sub_n = 0;
  int cand[VG_AGENT_QUERY_CAND_MAX];

  if (points == NULL || out == NULL || n < 0 || n > VG_AGENT_QUERY_MAX)
    {
      return VG_AGENT_QUERY_ARG;
    }

  if (query == NULL || query[0] == '\0')
    {
      set_match(out, VG_AGENT_MATCH_NONE, -1);
      return VG_AGENT_QUERY_LIST;
    }

  for (i = 0; i < n; i++)
    {
      if (ci_equal(points[i].id, query))
        {
          set_match(out, VG_AGENT_MATCH_ID, i);
          return VG_AGENT_QUERY_OK;
        }
    }

  for (i = 0; i < n; i++)
    {
      /* An entry with no explicit name carries its id in the name field
       * (vg_point_table.c parse fills it that way), so skip the duplicate
       * rather than letting it win the name pass by accident. */
      if (ci_equal(points[i].id, points[i].name))
        {
          continue;
        }

      if (ci_equal(points[i].name, query))
        {
          set_match(out, VG_AGENT_MATCH_NAME, i);
          return VG_AGENT_QUERY_OK;
        }
    }

  for (i = 0; i < n; i++)
    {
      if (!ci_contains(points[i].name, query))
        {
          continue;
        }

      if (sub_n == 0)
        {
          sub_idx = i;
        }

      if (sub_n < VG_AGENT_QUERY_CAND_MAX)
        {
          cand[sub_n] = i;
        }

      sub_n++;
    }

  if (sub_n == 1)
    {
      set_match(out, VG_AGENT_MATCH_NAME_SUB, sub_idx);
      return VG_AGENT_QUERY_OK;
    }

  if (sub_n > 1)
    {
      int listed = (sub_n < VG_AGENT_QUERY_CAND_MAX)
                       ? sub_n
                       : VG_AGENT_QUERY_CAND_MAX;

      /* set_match clears the struct, so the candidates gathered above have
       * to come back afterwards. */
      set_match(out, VG_AGENT_MATCH_NONE, -1);
      for (i = 0; i < listed; i++)
        {
          out->cand_idx[i] = cand[i];
        }

      out->cand_n = listed;
      return VG_AGENT_QUERY_AMBIG;
    }

  set_match(out, VG_AGENT_MATCH_NONE, -1);
  return VG_AGENT_QUERY_NOT_FOUND;
}

static const char *unit_or_dash(const struct vg_agent_point *p)
{
  if (p->unit[0] == '\0' || strcmp(p->unit, "-") == 0)
    {
      return "-";
    }

  return p->unit;
}

/* One cmp applies to both thresholds (vg_point_entry has a single cmp), so
 * the direction is printed once and the model can read ge70 as "70 or more". */

static int append_thresholds(char *out, size_t cap, int off,
                             const struct vg_agent_point *p)
{
  const char *cmp = (p->cmp[0] != '\0') ? p->cmp : "ge";

  if (p->has_warn)
    {
      off = appendf(out, cap, off, " warn=%s%.4g", cmp, (double)p->warn);
    }

  if (p->has_crit)
    {
      off = appendf(out, cap, off, " crit=%s%.4g", cmp, (double)p->crit);
    }

  return off;
}

int vg_agent_query_format_point(char *out, size_t cap,
                                const struct vg_agent_point *p, bool stale)
{
  int off;

  if (out == NULL || cap == 0 || p == NULL)
    {
      return -1;
    }

  out[0] = '\0';

  /* The id and value are stable tokens; the rest is prose the model may
   * quote.  reason= keeps "no data" distinguishable from "zero" without the
   * model having to infer it from an empty field. */
  off = appendf(out, cap, 0, "vgquery: point id=%s name=%s",
                p->id[0] ? p->id : "-",
                p->name[0] ? p->name : "-");

  if (!p->has_sample)
    {
      off = appendf(out, cap, off, " value=- unit=%s reason=no_sample",
                    unit_or_dash(p));
      off = append_thresholds(out, cap, off, p);
      off = appendf(out, cap, off, "\n");
      return off;
    }

  if (!p->ok)
    {
      off = appendf(out, cap, off,
                    " value=- unit=%s reason=read_failed age_ms=%u",
                    unit_or_dash(p), (unsigned)p->age_ms);
      off = append_thresholds(out, cap, off, p);
      off = appendf(out, cap, off, "\n");
      return off;
    }

  off = appendf(out, cap, off, " value=%.4g unit=%s ok=1 age_ms=%u",
                (double)p->value, unit_or_dash(p), (unsigned)p->age_ms);

  if (stale)
    {
      off = appendf(out, cap, off, " stale=1");
    }

  off = append_thresholds(out, cap, off, p);
  off = appendf(out, cap, off, "\n");
  return off;
}

int vg_agent_query_format_candidates(char *out, size_t cap,
                                     const struct vg_agent_point *points,
                                     int n,
                                     const struct vg_agent_query_result *res)
{
  int off;
  int i;

  if (out == NULL || cap == 0 || points == NULL || res == NULL)
    {
      return -1;
    }

  out[0] = '\0';
  off = appendf(out, cap, 0, "vgquery: ambiguous n=%d\n", res->cand_n);

  for (i = 0; i < res->cand_n; i++)
    {
      int idx = res->cand_idx[i];

      if (idx < 0 || idx >= n)
        {
          continue;
        }

      off = appendf(out, cap, off, "vgquery: candidate id=%s name=%s\n",
                    points[idx].id[0] ? points[idx].id : "-",
                    points[idx].name[0] ? points[idx].name : "-");
    }

  return off;
}

int vg_agent_query_format_list(char *out, size_t cap,
                               const struct vg_agent_point *points, int n)
{
  int off;
  int i;
  int shown = 0;

  if (out == NULL || cap == 0 || points == NULL || n < 0)
    {
      return -1;
    }

  out[0] = '\0';
  off = appendf(out, cap, 0, "vgquery: table n=%d\n", n);

  for (i = 0; i < n && shown < VG_AGENT_QUERY_LIST_MAX; i++)
    {
      off = appendf(out, cap, off,
                    "vgquery: point id=%s name=%s unit=%s%s",
                    points[i].id[0] ? points[i].id : "-",
                    points[i].name[0] ? points[i].name : "-",
                    unit_or_dash(&points[i]),
                    points[i].has_sample ? "" : " (no_sample)");
      off = append_thresholds(out, cap, off, &points[i]);
      off = appendf(out, cap, off, "\n");
      shown++;
    }

  if (n > shown)
    {
      off = appendf(out, cap, off, "vgquery: more=%d\n", n - shown);
    }

  return off;
}

/* ── call rate ───────────────────────────────────────────────────── */

void vg_agent_rate_init(struct vg_agent_rate *r, unsigned max_calls,
                        uint32_t window_ms)
{
  if (r == NULL)
    {
      return;
    }

  r->window_start_ms = 0;
  r->window_ms = window_ms;
  r->calls = 0;
  r->max_calls = max_calls;
  r->started = false;
}

bool vg_agent_rate_allow(struct vg_agent_rate *r, uint32_t now_ms)
{
  if (r == NULL || r->max_calls == 0)
    {
      return false;
    }

  /* A separate flag, not window_start_ms == 0: CLOCK_MONOTONIC legitimately
   * reads 0 right after boot, and treating that as "no window yet" would
   * reopen the budget on every call for as long as the clock stayed at 0.
   * The unsigned subtraction covers the clock wrapping. */
  if (!r->started || (now_ms - r->window_start_ms) >= r->window_ms)
    {
      r->window_start_ms = now_ms;
      r->calls = 0;
      r->started = true;
    }

  if (r->calls >= r->max_calls)
    {
      return false;
    }

  r->calls++;
  return true;
}
