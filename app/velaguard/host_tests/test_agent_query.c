/****************************************************************************
 * app/velaguard/host_tests/test_agent_query.c
 *
 * Host tests for the vg_point_read resolution logic: id/name/substring
 * matching, ambiguity handling, and the shape of the rendered answer.
 ****************************************************************************/

#include <stdio.h>
#include <string.h>

#include "vg_agent_query.h"

static int g_fail;

static void expect(int cond, const char *msg)
{
  if (!cond)
    {
      fprintf(stderr, "FAIL: %s\n", msg);
      g_fail++;
    }
}

/* The demo point table's UPS entries plus two names that share a substring,
 * so ambiguity is reachable with the real wording.  Field order follows
 * struct vg_agent_point: id, name, unit, cmp, has_warn, warn, has_crit,
 * crit, has_sample, value, ok, age_ms. */

static const struct vg_agent_point g_pts[] =
  {
    { "acu_run",   "空调运行", "",  "",   false, 0,  false, 0,  true,  1.0f,  true,  120 },
    { "ups_load",  "UPS负载", "%", "ge", true,  70, true,  90, true,  72.5f, true,  120 },
    { "ups_soc",   "UPS电池", "%", "le", true,  40, true,  20, true,  88.0f, true,  120 },
    { "ups_bypass","UPS旁路", "",  "eq", false, 0,  true,  1,  true,  0.0f,  true,  120 },
    { "temp_a",    "温度A",   "C", "ge", true,  40, false, 0,  true,  24.5f, true,  120 },
    { "temp_b",    "温度B",   "C", "ge", true,  40, false, 0,  false, 0.0f,  false, 0   },
  };

#define N_PTS ((int)(sizeof(g_pts) / sizeof(g_pts[0])))

static void test_id_exact(void)
{
  struct vg_agent_query_result res;
  enum vg_agent_query_rc rc;

  rc = vg_agent_query_match(g_pts, N_PTS, "ups_load", &res);
  expect(rc == VG_AGENT_QUERY_OK, "id exact resolves");
  expect(res.mode == VG_AGENT_MATCH_ID, "mode is id");
  expect(res.index == 1, "id exact picks ups_load");

  /* A user or the model may not preserve case. */
  rc = vg_agent_query_match(g_pts, N_PTS, "UPS_LOAD", &res);
  expect(rc == VG_AGENT_QUERY_OK && res.index == 1, "id exact is case-insensitive");
}

static void test_name_exact_beats_substring(void)
{
  struct vg_agent_query_result res;
  enum vg_agent_query_rc rc;

  /* "UPS负载" is an exact name, but other names also contain "UPS". */
  rc = vg_agent_query_match(g_pts, N_PTS, "UPS负载", &res);
  expect(rc == VG_AGENT_QUERY_OK, "chinese name exact resolves");
  expect(res.mode == VG_AGENT_MATCH_NAME, "mode is name");
  expect(res.index == 1, "chinese name exact picks ups_load");
}

static void test_id_beats_name(void)
{
  struct vg_agent_point pts[2];
  struct vg_agent_query_result res;
  enum vg_agent_query_rc rc;

  /* An id that also spells another point's name must resolve to the id. */
  memset(pts, 0, sizeof(pts));
  snprintf(pts[0].id, sizeof(pts[0].id), "shared");
  snprintf(pts[0].name, sizeof(pts[0].name), "别的名字");
  snprintf(pts[1].id, sizeof(pts[1].id), "other");
  snprintf(pts[1].name, sizeof(pts[1].name), "shared");

  rc = vg_agent_query_match(pts, 2, "shared", &res);
  expect(rc == VG_AGENT_QUERY_OK, "id wins over name");
  expect(res.index == 0, "id match picked the entry whose id matches");
  expect(res.mode == VG_AGENT_MATCH_ID, "mode is id when both match");
}

static void test_name_field_holding_id_is_not_double_counted(void)
{
  struct vg_agent_point pts[1];
  struct vg_agent_query_result res;
  enum vg_agent_query_rc rc;

  /* vg_point_table.c copies the id into name when the table row has none.
   * That duplicate must not turn a unique hit into an ambiguous one. */
  memset(pts, 0, sizeof(pts));
  snprintf(pts[0].id, sizeof(pts[0].id), "ups_load");
  snprintf(pts[0].name, sizeof(pts[0].name), "ups_load");

  rc = vg_agent_query_match(pts, 1, "ups_load", &res);
  expect(rc == VG_AGENT_QUERY_OK, "id resolves when name duplicates it");
  expect(res.mode == VG_AGENT_MATCH_ID, "duplicate name does not win the name pass");
}

static void test_substring_unique(void)
{
  struct vg_agent_query_result res;
  enum vg_agent_query_rc rc;

  rc = vg_agent_query_match(g_pts, N_PTS, "负载", &res);
  expect(rc == VG_AGENT_QUERY_OK, "unique substring resolves");
  expect(res.mode == VG_AGENT_MATCH_NAME_SUB, "mode is substring");
  expect(res.index == 1, "substring picks the only match");
}

static void test_substring_ambiguous(void)
{
  struct vg_agent_query_result res;
  enum vg_agent_query_rc rc;

  /* Three points contain "UPS".  Picking one would answer a different
   * question than the user asked. */
  rc = vg_agent_query_match(g_pts, N_PTS, "UPS", &res);
  expect(rc == VG_AGENT_QUERY_AMBIG, "several substring hits are ambiguous");
  expect(res.index == -1, "ambiguous result names no point");
  expect(res.cand_n == 3, "all three candidates listed");
  expect(res.cand_idx[0] == 1 && res.cand_idx[1] == 2 && res.cand_idx[2] == 3,
         "candidates are the UPS points in table order");
}

static void test_ambiguous_candidates_are_capped(void)
{
  struct vg_agent_point pts[5];
  struct vg_agent_query_result res;
  enum vg_agent_query_rc rc;
  int i;

  for (i = 0; i < 5; i++)
    {
      memset(&pts[i], 0, sizeof(pts[i]));
      snprintf(pts[i].id, sizeof(pts[i].id), "pt%d", i);
      snprintf(pts[i].name, sizeof(pts[i].name), "共享名字%d", i);
    }

  rc = vg_agent_query_match(pts, 5, "共享", &res);
  expect(rc == VG_AGENT_QUERY_AMBIG, "five hits are ambiguous");
  expect(res.cand_n == VG_AGENT_QUERY_CAND_MAX,
         "candidate list is capped at VG_AGENT_QUERY_CAND_MAX");
}

static void test_not_found_and_list(void)
{
  struct vg_agent_query_result res;
  enum vg_agent_query_rc rc;

  rc = vg_agent_query_match(g_pts, N_PTS, "不存在的点", &res);
  expect(rc == VG_AGENT_QUERY_NOT_FOUND, "unknown wording is not_found");
  expect(res.index == -1, "not_found names no point");

  rc = vg_agent_query_match(g_pts, N_PTS, "", &res);
  expect(rc == VG_AGENT_QUERY_LIST, "empty query lists the table");

  rc = vg_agent_query_match(g_pts, N_PTS, NULL, &res);
  expect(rc == VG_AGENT_QUERY_LIST, "NULL query lists the table");
}

static void test_arg_guards(void)
{
  struct vg_agent_query_result res;

  expect(vg_agent_query_match(NULL, N_PTS, "x", &res) == VG_AGENT_QUERY_ARG,
         "NULL points rejected");
  expect(vg_agent_query_match(g_pts, -1, "x", &res) == VG_AGENT_QUERY_ARG,
         "negative count rejected");
  expect(vg_agent_query_match(g_pts, N_PTS, "x", NULL) == VG_AGENT_QUERY_ARG,
         "NULL result rejected");
}

static void test_format_point(void)
{
  char buf[256];
  int n;

  n = vg_agent_query_format_point(buf, sizeof(buf), &g_pts[1], false);
  expect(n > 0, "point line written");
  expect(strstr(buf, "vgquery: point") != NULL, "stable prefix");
  expect(strstr(buf, "id=ups_load") != NULL, "id present");
  expect(strstr(buf, "name=UPS负载") != NULL, "chinese name preserved");
  expect(strstr(buf, "value=72.5") != NULL, "value present");
  expect(strstr(buf, "unit=%") != NULL, "unit present");
  expect(strstr(buf, "ok=1") != NULL, "ok flag present");
  expect(strstr(buf, "stale=1") == NULL, "fresh sample is not stale");
  expect(buf[n - 1] == '\n', "line ends with newline");

  n = vg_agent_query_format_point(buf, sizeof(buf), &g_pts[1], true);
  expect(n > 0 && strstr(buf, "stale=1") != NULL, "stale flag set when asked");
}

static void test_format_missing_sample_has_no_number(void)
{
  char buf[256];

  /* temp_b has no sample: the answer must not read as a value of zero. */
  vg_agent_query_format_point(buf, sizeof(buf), &g_pts[5], false);
  expect(strstr(buf, "reason=no_sample") != NULL, "no_sample is explicit");
  expect(strstr(buf, "value=-") != NULL, "no number when there is no sample");
  expect(strstr(buf, "value=0") == NULL, "missing sample is not reported as 0");
}

static void test_format_read_failed(void)
{
  struct vg_agent_point p = { "pt1", "点名", "%", "ge", true, 40, false, 0,
                               true, 0.0f, false, 900 };
  char buf[256];

  vg_agent_query_format_point(buf, sizeof(buf), &p, false);
  expect(strstr(buf, "reason=read_failed") != NULL, "bad sample is explicit");
  expect(strstr(buf, "value=-") != NULL, "no number for a failed read");
}

static void test_format_unit_dash(void)
{
  char buf[256];

  /* An empty unit must print as "-", never as an empty field. */
  vg_agent_query_format_point(buf, sizeof(buf), &g_pts[0], false);
  expect(strstr(buf, "unit=-") != NULL, "empty unit prints as dash");
}

static void test_truncation_stays_in_bounds(void)
{
  char small[48];
  int n;
  size_t i;

  /* A short buffer must not produce a partial multi-byte line that tells
   * the model something false, and must stay NUL-terminated. */
  n = vg_agent_query_format_point(small, sizeof(small), &g_pts[1], false);
  expect(n > 0, "short buffer still writes something");
  expect(n <= (int)sizeof(small) - 1, "offset never passes the buffer");

  for (i = 0; i < sizeof(small); i++)
    {
      if (small[i] == '\0')
        {
          break;
        }
    }

  expect(i < sizeof(small), "buffer is NUL-terminated");

  n = vg_agent_query_format_list(small, sizeof(small), g_pts, N_PTS);
  expect(n > 0 && n <= (int)sizeof(small) - 1,
         "list truncation stays in bounds");
}

static void test_format_candidates_and_list(void)
{
  struct vg_agent_query_result res;
  char buf[512];

  expect(vg_agent_query_match(g_pts, N_PTS, "UPS", &res) == VG_AGENT_QUERY_AMBIG,
         "ambiguous query for candidate render");
  vg_agent_query_format_candidates(buf, sizeof(buf), g_pts, N_PTS, &res);
  expect(strstr(buf, "ambiguous n=3") != NULL, "candidate count present");
  expect(strstr(buf, "id=ups_load") != NULL, "candidate id listed");
  expect(strstr(buf, "UPS电池") != NULL, "candidate chinese name listed");

  vg_agent_query_format_list(buf, sizeof(buf), g_pts, N_PTS);
  expect(strstr(buf, "table n=6") != NULL, "list reports entry count");
  expect(strstr(buf, "id=acu_run") != NULL, "list includes first point");
  expect(strstr(buf, "no_sample") != NULL, "list marks the point with no sample");
}

static void test_stale_predicate(void)
{
  struct vg_agent_point p = { "pt", "点名", "%", "", false, 0, false, 0,
                              true, 1.0f, true, 0 };

  p.age_ms = VG_AGENT_QUERY_STALE_MS;
  expect(!vg_agent_query_is_stale(&p), "age exactly at the limit is fresh");

  p.age_ms = VG_AGENT_QUERY_STALE_MS + 1;
  expect(vg_agent_query_is_stale(&p), "age past the limit is stale");

  p.has_sample = false;
  p.age_ms = 60000;
  expect(!vg_agent_query_is_stale(&p),
         "a point with no sample is missing, not stale");
}

static void test_thresholds_reported(void)
{
  char buf[256];
  struct vg_agent_point bare = { "pt", "点名", "%", "", false, 0, false, 0,
                                 true, 1.0f, true, 10 };

  /* ups_load has warn 70 / crit 90 with cmp ge: the model needs both the
   * number and the direction to say "70% or more". */
  vg_agent_query_format_point(buf, sizeof(buf), &g_pts[1], false);
  expect(strstr(buf, "warn=ge70") != NULL, "warn threshold with direction");
  expect(strstr(buf, "crit=ge90") != NULL, "crit threshold with direction");

  /* ups_soc compares the other way; the direction must follow the table. */
  vg_agent_query_format_point(buf, sizeof(buf), &g_pts[2], false);
  expect(strstr(buf, "warn=le40") != NULL, "le direction preserved");

  /* A point with no thresholds must not print an empty warn=. */
  vg_agent_query_format_point(buf, sizeof(buf), &bare, false);
  expect(strstr(buf, "warn=") == NULL, "no warn field when unset");
  expect(strstr(buf, "crit=") == NULL, "no crit field when unset");
}

static void test_thresholds_on_no_sample(void)
{
  char buf[256];

  /* Even with no live sample the thresholds are useful: the model can still
   * say what would count as a violation. */
  vg_agent_query_format_point(buf, sizeof(buf), &g_pts[5], false);
  expect(strstr(buf, "reason=no_sample") != NULL, "still reports no_sample");
  expect(strstr(buf, "warn=ge40") != NULL, "thresholds kept without a sample");
}

static void test_zero_value_is_reported(void)
{
  char buf[256];

  /* ups_bypass reads 0.0.  Zero is real data, so it must print as 0 and
   * must not be confused with the no-data rendering. */
  vg_agent_query_format_point(buf, sizeof(buf), &g_pts[3], false);
  expect(strstr(buf, "value=0") != NULL, "zero is reported as a value");
  expect(strstr(buf, "reason=") == NULL, "zero is not a no-data reason");
}

static void test_rate_limit(void)
{
  struct vg_agent_rate r;
  int i;

  vg_agent_rate_init(&r, 3, 1000);

  for (i = 0; i < 3; i++)
    {
      expect(vg_agent_rate_allow(&r, 500), "call inside the budget is allowed");
    }

  expect(!vg_agent_rate_allow(&r, 500), "call past the budget is refused");

  /* A new window reopens the budget. */
  expect(vg_agent_rate_allow(&r, 1500), "next window allows calls again");

  /* The first call sets the window: a clock starting at 0 must not be read
   * as "no window yet" forever. */
  vg_agent_rate_init(&r, 1, 1000);
  expect(vg_agent_rate_allow(&r, 0), "first call at t=0 is allowed");
  expect(!vg_agent_rate_allow(&r, 0), "second call at t=0 is refused");
}

static void test_rate_limit_clock_wrap(void)
{
  struct vg_agent_rate r;
  uint32_t near_wrap = 0xfffffff0u;

  vg_agent_rate_init(&r, 2, 1000);

  expect(vg_agent_rate_allow(&r, near_wrap), "call before the wrap");
  expect(vg_agent_rate_allow(&r, near_wrap), "second call before the wrap");
  expect(!vg_agent_rate_allow(&r, near_wrap), "budget spent before the wrap");

  /* 0x20 ms later the clock has wrapped to a small value.  Unsigned
   * subtraction must read 32 ms elapsed, so the window is NOT over and the
   * spent budget still holds.  Reading the wrap as a huge backwards jump
   * would reopen the budget early. */
  expect(!vg_agent_rate_allow(&r, 0x10u),
         "clock wrap does not reopen a spent window");

  /* Crossing the window boundary across the wrap does reopen it. */
  expect(vg_agent_rate_allow(&r, near_wrap + 1000u),
         "window boundary across the wrap reopens the budget");
}

static void test_rate_limit_guards(void)
{
  struct vg_agent_rate r;

  expect(!vg_agent_rate_allow(NULL, 1), "NULL state is refused");

  vg_agent_rate_init(&r, 0, 1000);
  expect(!vg_agent_rate_allow(&r, 1), "a zero budget refuses every call");

  /* init on NULL must not crash, so a caller cannot fault on setup. */
  vg_agent_rate_init(NULL, 1, 1000);
}

int main(void)
{
  test_id_exact();
  test_name_exact_beats_substring();
  test_id_beats_name();
  test_name_field_holding_id_is_not_double_counted();
  test_substring_unique();
  test_substring_ambiguous();
  test_ambiguous_candidates_are_capped();
  test_not_found_and_list();
  test_arg_guards();
  test_format_point();
  test_format_missing_sample_has_no_number();
  test_format_read_failed();
  test_format_unit_dash();
  test_truncation_stays_in_bounds();
  test_format_candidates_and_list();
  test_stale_predicate();
  test_thresholds_reported();
  test_thresholds_on_no_sample();
  test_zero_value_is_reported();
  test_rate_limit();
  test_rate_limit_clock_wrap();
  test_rate_limit_guards();

  if (g_fail != 0)
    {
      fprintf(stderr, "test_agent_query: %d failure(s)\n", g_fail);
      return 1;
    }

  printf("test_agent_query: OK\n");
  return 0;
}
