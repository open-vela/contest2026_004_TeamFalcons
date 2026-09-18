/****************************************************************************
 * app/velaguard/vg_agent_query.h
 *
 * Pure logic behind the vg_point_read agent tool: resolving a user's wording
 * ("UPS负载") to one entry of the confirmed point table, and rendering the
 * answer as bounded text.
 *
 * Deliberately free of LVGL, cJSON and NuttX headers so the matching and
 * truncation rules compile under app/velaguard/host_tests.
 ****************************************************************************/

#ifndef __APP_VELAGUARD_VG_AGENT_QUERY_H
#define __APP_VELAGUARD_VG_AGENT_QUERY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define VG_AGENT_QUERY_STR_MAX   48   /* matches VG_POINT_NAME_MAX */
#define VG_AGENT_QUERY_UNIT_MAX  8
#define VG_AGENT_QUERY_MAX        32  /* VG_DISCOVER_MAX_POINTS */
#define VG_AGENT_QUERY_CAND_MAX    3  /* candidates listed on ambiguity */
#define VG_AGENT_QUERY_LIST_MAX   12  /* entries listed when query is empty */

/* Snapshot freshness.  The whole-file snapshot carries one tick_ms, so this
 * describes the sample cycle rather than an individual point. */
#define VG_AGENT_QUERY_STALE_MS 5000u

/* One point as the tool sees it: table metadata joined with the live sample. */

struct vg_agent_point
{
  char     id[VG_AGENT_QUERY_STR_MAX];
  char     name[VG_AGENT_QUERY_STR_MAX];   /* no name in the table -> id */
  char     unit[VG_AGENT_QUERY_UNIT_MAX];
  char     cmp[4];                         /* ge / le / eq, "" when none */
  bool     has_warn;
  float    warn;
  bool     has_crit;
  float    crit;
  bool     has_sample;
  float    value;
  bool     ok;                             /* sample read succeeded */
  uint32_t age_ms;
};

enum vg_agent_query_rc
{
  VG_AGENT_QUERY_OK        = 0,
  VG_AGENT_QUERY_LIST      = 1,   /* empty query: caller lists the table */
  VG_AGENT_QUERY_AMBIG     = 2,   /* several points match; none chosen */
  VG_AGENT_QUERY_NOT_FOUND = 3,
  VG_AGENT_QUERY_NO_TABLE  = 4,   /* confirmed table absent or unreadable */
  VG_AGENT_QUERY_NO_SAMPLE = 5,   /* table present, no live snapshot yet */
  VG_AGENT_QUERY_ARG       = 6    /* bad caller argument */
};

enum vg_agent_match_mode
{
  VG_AGENT_MATCH_NONE = 0,
  VG_AGENT_MATCH_ID,          /* query equals the point id */
  VG_AGENT_MATCH_NAME,        /* query equals the point name */
  VG_AGENT_MATCH_NAME_SUB     /* query is contained in exactly one name */
};

struct vg_agent_query_result
{
  enum vg_agent_match_mode mode;
  int                      index;      /* matched entry, -1 when none */
  int                      cand_idx[VG_AGENT_QUERY_CAND_MAX];
  int                      cand_n;
};

/* Resolve query against points[0..n).  Never mutates points.
 *
 * id is tried before name, and an exact name before a substring, so the
 * narrowest reading wins.  A substring that hits several points reports
 * VG_AGENT_QUERY_AMBIG and fills cand_idx instead of picking one: reporting
 * an arbitrary point's value under the user's question is worse than asking
 * which point was meant.
 */

enum vg_agent_query_rc
vg_agent_query_match(const struct vg_agent_point *points, int n,
                     const char *query,
                     struct vg_agent_query_result *out);

/* Render one resolved point as a single vgquery: line.
 * Returns bytes written, or -1 on a bad argument.  Always NUL-terminates. */

int vg_agent_query_format_point(char *out, size_t cap,
                                const struct vg_agent_point *p, bool stale);

/* Render the candidate list for an ambiguous query. */

int vg_agent_query_format_candidates(char *out, size_t cap,
                                     const struct vg_agent_point *points,
                                     int n,
                                     const struct vg_agent_query_result *res);

/* Render up to VG_AGENT_QUERY_LIST_MAX points, one per line. */

int vg_agent_query_format_list(char *out, size_t cap,
                               const struct vg_agent_point *points, int n);

/* True when a sample older than VG_AGENT_QUERY_STALE_MS should be flagged.
 * A point with no sample is not "stale", it is missing; callers tell those
 * apart via has_sample. */

bool vg_agent_query_is_stale(const struct vg_agent_point *p);

/* ── call rate ─────────────────────────────────────────────────────
 *
 * tool_guard.c rate-limits run_shell / write_file / edit_file only, so a
 * read-only tool has no ceiling there.  AGENTS.md requires the limit to live
 * in C rather than the prompt, and an unthrottled read is still an eMMC read
 * on every ReAct iteration.  The state is explicit so the rule is testable
 * with a chosen clock instead of a real one.
 */

struct vg_agent_rate
{
  uint32_t window_start_ms;
  uint32_t window_ms;
  unsigned calls;
  unsigned max_calls;
  bool     started;
};

void vg_agent_rate_init(struct vg_agent_rate *r, unsigned max_calls,
                        uint32_t window_ms);

/* Consume one call.  Returns false when the budget for the current window is
 * already spent.  Comparison uses unsigned subtraction, so a wrapping
 * monotonic clock does not reopen the window forever. */

bool vg_agent_rate_allow(struct vg_agent_rate *r, uint32_t now_ms);

#endif /* __APP_VELAGUARD_VG_AGENT_QUERY_H */
