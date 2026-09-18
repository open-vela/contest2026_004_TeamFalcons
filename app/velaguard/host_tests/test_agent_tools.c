/****************************************************************************
 * app/velaguard/host_tests/test_agent_tools.c
 *
 * Host tests for the device-side agent tools (vg_agent_tools.c), compiled
 * against a stub nuttx/config.h and stubs for the point table / runtime
 * modules this file only reads through.
 *
 * What this covers that test_agent_query.c cannot: the JSON definition the
 * model actually receives, the argument guards, and the table+snapshot join.
 * An unparseable tools JSON would leave the model with no tools at all and
 * would only show up on the board, so it is checked here by parsing.
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <netutils/cJSON.h>

#include "vg_agent_tools.h"
#include "vg_discover.h"
#include "vg_runtime.h"

static int g_fail;

static void expect(int cond, const char *msg)
{
  if (!cond)
    {
      fprintf(stderr, "FAIL: %s\n", msg);
      g_fail++;
    }
}

/* ── stubbed board state ─────────────────────────────────────────── */

/* The test controls what the tools see by setting these. */

static struct vg_discover_summary g_table;
static struct vg_live_snapshot    g_snap;
static int  g_table_rc      = 0;
static int  g_snap_rc       = -ENOENT;
static int  g_root_ok       = 0;
static int  g_report_rc     = 1;
static char g_report[512]   = "运行报告由板上统计生成，数字不是推测。\n通信质量\n点位在线\n";

int vg_point_table_read(FAR struct vg_discover_summary *sum,
                        FAR const char *path)
{
  (void)path;
  if (g_table_rc != 0)
    {
      return g_table_rc;
    }

  *sum = g_table;
  return 0;
}

int vg_point_store_root_ok(FAR const char *root)
{
  (void)root;
  return g_root_ok;
}

int vg_live_snapshot_read(FAR const char *path,
                          FAR struct vg_live_snapshot *snap)
{
  (void)path;
  if (g_snap_rc != 0)
    {
      return g_snap_rc;
    }

  *snap = g_snap;
  return 0;
}

int vg_live_snapshot_find_id(FAR const struct vg_live_snapshot *snap,
                             FAR const char *id)
{
  int i;

  for (i = 0; i < snap->n; i++)
    {
      if (strcmp(snap->samples[i].id, id) == 0)
        {
          return i;
        }
    }

  return -1;
}

uint32_t vg_live_now_ms(void) { return 100000; }

uint32_t vg_live_age_ms(uint32_t tick_ms, uint32_t now_ms)
{
  return now_ms - tick_ms;
}

int vg_runtime_format_report(char *out, size_t cap)
{
  size_t n;

  if (out == NULL || cap == 0 || g_report_rc <= 0)
    {
      return 0;
    }

  n = strlen(g_report);
  if (n >= cap)
    {
      n = cap - 1;
    }

  memcpy(out, g_report, n);
  out[n] = '\0';
  return (int)n;
}

/* Records what the provider registered, so the test can call it the same way
 * tool_registry_execute does. */

static char *(*g_provider_tools)(void);
static int (*g_provider_exec)(const char *, const char *, char *, size_t);

void tool_registry_register_provider(const char *name,
                                     char *(*get_tools)(void),
                                     int (*execute)(const char *, const char *,
                                                    char *, size_t))
{
  (void)name;
  g_provider_tools = get_tools;
  g_provider_exec = execute;
}

/* ── fixtures ────────────────────────────────────────────────────── */

/* Fixtures reset the failure injections too.  Leaving them set meant an
 * earlier case's error leaked into the next one, which is how these tests
 * first failed: the tool was right, the fixture was stale. */

static void seed_table(void)
{
  memset(&g_table, 0, sizeof(g_table));
  g_table_rc = 0;
  g_root_ok = 0;
  g_table.baud = 9600;
  g_table.n_points = 3;

  snprintf(g_table.points[0].id, sizeof(g_table.points[0].id), "ups_load");
  snprintf(g_table.points[0].name, sizeof(g_table.points[0].name), "UPS负载");
  snprintf(g_table.points[0].unit, sizeof(g_table.points[0].unit), "%%");
  snprintf(g_table.points[0].cmp, sizeof(g_table.points[0].cmp), "ge");
  g_table.points[0].has_warn = 1;
  g_table.points[0].warn = 70;
  g_table.points[0].has_crit = 1;
  g_table.points[0].crit = 90;

  snprintf(g_table.points[1].id, sizeof(g_table.points[1].id), "ups_soc");
  snprintf(g_table.points[1].name, sizeof(g_table.points[1].name), "UPS电池");
  snprintf(g_table.points[1].unit, sizeof(g_table.points[1].unit), "%%");

  /* No name in the table: vg_point_table_read fills it with the id, and the
   * tool must reproduce that without making the row ambiguous. */
  snprintf(g_table.points[2].id, sizeof(g_table.points[2].id), "acu_run");
  snprintf(g_table.points[2].name, sizeof(g_table.points[2].name), "acu_run");
}

static void seed_snapshot_scaled(void)
{
  memset(&g_snap, 0, sizeof(g_snap));
  g_snap.tick_ms = 99000;   /* 1 s before vg_live_now_ms */
  g_snap.n = 2;
  snprintf(g_snap.samples[0].id, sizeof(g_snap.samples[0].id), "ups_load");
  snprintf(g_snap.samples[0].unit, sizeof(g_snap.samples[0].unit), "%%");
  g_snap.samples[0].ok = 1;
  g_snap.samples[0].value = 72.5f;
  snprintf(g_snap.samples[1].id, sizeof(g_snap.samples[1].id), "ups_soc");
  g_snap.samples[1].ok = 1;
  g_snap.samples[1].value = 88.0f;
  g_snap_rc = 0;
}

static int run_tool(const char *name, const char *args, char *out, size_t cap)
{
  out[0] = '\0';
  return g_provider_exec(name, args, out, cap);
}

/* ── tests ───────────────────────────────────────────────────────── */

static void test_tools_json_is_valid_and_complete(void)
{
  char *json = g_provider_tools();
  cJSON *arr;
  cJSON *tool;
  int names_seen = 0;
  int has_point = 0;
  int has_report = 0;

  expect(json != NULL, "provider returns tools JSON");
  if (json == NULL)
    {
      return;
    }

  /* The registry parses this string and drops it silently if it is not a
   * JSON array, so parsing here is the real check. */
  arr = cJSON_Parse(json);
  expect(arr != NULL, "tools JSON parses");
  expect(cJSON_IsArray(arr), "tools JSON is an array");
  if (arr == NULL)
    {
      free(json);
      return;
    }

  cJSON_ArrayForEach(tool, arr)
    {
      cJSON *name = cJSON_GetObjectItem(tool, "name");
      cJSON *desc = cJSON_GetObjectItem(tool, "description");
      cJSON *schema = cJSON_GetObjectItem(tool, "input_schema");

      expect(cJSON_IsString(name), "tool has a name");
      expect(cJSON_IsString(desc), "tool has a description");
      expect(schema != NULL && cJSON_IsObject(schema),
             "tool has an object input_schema");

      if (cJSON_IsString(name))
        {
          names_seen++;
          if (strcmp(name->valuestring, "vg_point_read") == 0)
            {
              has_point = 1;
              expect(cJSON_GetObjectItem(schema, "properties") != NULL,
                     "vg_point_read declares properties");
            }

          if (strcmp(name->valuestring, "vg_run_report") == 0)
            {
              has_report = 1;
            }
        }
    }

  expect(names_seen == 2, "exactly the two read-only tools are advertised");
  expect(has_point, "vg_point_read advertised");
  expect(has_report, "vg_run_report advertised");

  cJSON_Delete(arr);
  free(json);
}

static void test_point_read_returns_scaled_value(void)
{
  char out[1024];
  int rc;

  seed_table();
  seed_snapshot_scaled();

  rc = run_tool("vg_point_read", "{\"query\":\"ups_load\"}", out, sizeof(out));
  expect(rc == 0, "vg_point_read succeeds");
  expect(strstr(out, "id=ups_load") != NULL, "resolves the point by id");
  expect(strstr(out, "value=72.5") != NULL, "returns the engineering value");
  expect(strstr(out, "unit=%") != NULL, "returns the unit");
  expect(strstr(out, "warn=ge70") != NULL, "returns the warn threshold");
  expect(strstr(out, "crit=ge90") != NULL, "returns the crit threshold");
}

static void test_point_read_by_chinese_name(void)
{
  char out[1024];
  int rc;

  seed_table();
  seed_snapshot_scaled();

  /* This is the wording a user actually types. */
  rc = run_tool("vg_point_read", "{\"query\":\"UPS负载\"}", out, sizeof(out));
  expect(rc == 0, "chinese name query succeeds");
  expect(strstr(out, "id=ups_load") != NULL,
         "chinese name maps to the point id");
  expect(strstr(out, "value=72.5") != NULL, "returns the value");
}

static void test_point_read_ambiguous_lists_candidates(void)
{
  char out[1024];
  int rc;

  seed_table();
  seed_snapshot_scaled();

  rc = run_tool("vg_point_read", "{\"query\":\"UPS\"}", out, sizeof(out));
  expect(strstr(out, "ambiguous") != NULL, "ambiguous query says so");
  expect(strstr(out, "value=") == NULL,
         "an ambiguous query reports no value");
  expect(strstr(out, "ups_load") != NULL && strstr(out, "ups_soc") != NULL,
         "both candidates are listed");
  (void)rc;
}

static void test_point_read_no_query_lists_table(void)
{
  char out[1024];

  seed_table();
  seed_snapshot_scaled();

  run_tool("vg_point_read", "{}", out, sizeof(out));
  expect(strstr(out, "table n=3") != NULL, "empty query lists the table");
  expect(strstr(out, "id=acu_run") != NULL, "list includes every point");

  /* A point whose table name equals its id must appear once, not twice. */
  expect(strstr(out, "name=acu_run") != NULL, "name falls back to id");
}

static void test_point_read_no_table(void)
{
  char out[1024];

  seed_table();
  seed_snapshot_scaled();
  g_table_rc = -EIO;

  run_tool("vg_point_read", "{\"query\":\"ups_load\"}", out, sizeof(out));
  expect(strstr(out, "reason=no_table") != NULL,
         "unreadable table reports no_table");
  expect(strstr(out, "value=") == NULL, "no value invented for no_table");
}

static void test_point_read_missing_table_with_dead_store(void)
{
  char out[1024];

  seed_table();
  seed_snapshot_scaled();

  /* /data gone: an absent table must not be reported as an empty one. */
  g_table_rc = -ENOENT;
  g_root_ok = -ENODEV;
  run_tool("vg_point_read", "{\"query\":\"ups_load\"}", out, sizeof(out));
  expect(strstr(out, "reason=no_table") != NULL,
         "absent table on a dead store is no_table");

  /* Same errno with a reachable store: genuinely no table yet. */
  g_root_ok = 0;
  g_table.n_points = 0;
  run_tool("vg_point_read", "{\"query\":\"ups_load\"}", out, sizeof(out));
  expect(strstr(out, "reason=no_table") != NULL,
         "empty table on a live store is no_table");
}

static void test_point_read_without_snapshot(void)
{
  char out[1024];

  seed_table();
  g_snap_rc = -ENOENT;

  run_tool("vg_point_read", "{\"query\":\"ups_load\"}", out, sizeof(out));
  expect(strstr(out, "reason=no_sample") != NULL,
         "missing snapshot reports no_sample");
  /* "value=-" is the explicit no-data rendering; a bare "value=" substring
   * check would pass on it, so match the dash. */
  expect(strstr(out, "value=-") != NULL,
         "value field is the no-data dash without a sample");
  expect(strstr(out, "value=-1") == NULL, "no number without a sample");
  expect(strstr(out, "warn=ge70") != NULL,
         "thresholds still reported without a sample");
}

static void test_point_read_not_found(void)
{
  char out[1024];

  seed_table();
  seed_snapshot_scaled();

  run_tool("vg_point_read", "{\"query\":\"不存在的点位\"}", out, sizeof(out));
  expect(strstr(out, "reason=not_found") != NULL, "unknown point says not_found");
}

/* A tool we own must always answer with OK, even when the answer is a
 * complaint.  tool_registry_execute reads a provider's ERROR as "not my tool",
 * moves on to the next provider and finally replaces the buffer with
 * "unknown tool": the model would then be told the tool does not exist and
 * would retry instead of reporting what actually went wrong. */

static void test_owned_tool_always_returns_ok(void)
{
  char out[1024];
  int rc;

  seed_table();
  seed_snapshot_scaled();

  rc = run_tool("vg_point_read", "{\"query\":42}", out, sizeof(out));
  expect(rc == 0, "a bad argument still reports the tool as handled");
  expect(strstr(out, "reason=bad_arg") != NULL, "and explains the problem");

  rc = run_tool("vg_point_read", "not json", out, sizeof(out));
  expect(rc == 0, "malformed arguments still report the tool as handled");

  /* The failure that reaches the model must be the real one, not the
   * registry's "unknown tool" replacement. */
  rc = run_tool("vg_point_read", "{\"query\":\"不存在的点位\"}", out, sizeof(out));
  expect(rc == 0, "a not_found answer is a handled call");
  expect(strstr(out, "unknown tool") == NULL,
         "the real reason survives, not the registry's replacement");

  seed_table();
  g_table_rc = -EIO;
  rc = run_tool("vg_point_read", "{\"query\":\"ups_load\"}", out, sizeof(out));
  expect(rc == 0, "an unreadable table is a handled call");
  expect(strstr(out, "reason=no_table") != NULL, "with the real reason");

  g_report_rc = 0;
  rc = run_tool("vg_run_report", "{}", out, sizeof(out));
  expect(rc == 0, "an empty report is a handled call");
  expect(strstr(out, "reason=no_data") != NULL, "with the real reason");

  /* A name we do not own is the one case that must return ERROR, so the
   * registry can try the other providers. */
  expect(run_tool("write_file", "{}", out, sizeof(out)) != 0,
         "a foreign tool name returns ERROR so the registry keeps looking");
}

static void test_query_argument_guards(void)
{
  char out[1024];
  char big[128];

  seed_table();
  seed_snapshot_scaled();

  /* Wrong type must be rejected by our own check: tool_guard does not
   * validate the schema. */
  run_tool("vg_point_read", "{\"query\":42}", out, sizeof(out));
  expect(strstr(out, "reason=bad_arg") != NULL, "non-string query rejected");

  /* Over-long query is rejected, not truncated: a cut name could match the
   * wrong point. */
  memset(big, 'x', sizeof(big) - 1);
  big[sizeof(big) - 1] = '\0';
  {
    char args[256];
    snprintf(args, sizeof(args), "{\"query\":\"%s\"}", big);
    run_tool("vg_point_read", args, out, sizeof(out));
    expect(strstr(out, "reason=bad_arg") != NULL, "over-long query rejected");
  }

  run_tool("vg_point_read", "not json", out, sizeof(out));
  expect(strstr(out, "reason=bad_arg") != NULL, "malformed args rejected");
}

static void test_run_report(void)
{
  char out[1024];
  int rc;

  g_report_rc = 1;
  rc = run_tool("vg_run_report", "{}", out, sizeof(out));
  expect(rc == 0, "vg_run_report succeeds");
  expect(strstr(out, "通信质量") != NULL, "report has comm section");
  expect(strstr(out, "点位在线") != NULL, "report has point section");

  /* No statistics yet must not surface as an empty success. */
  g_report_rc = 0;
  run_tool("vg_run_report", "{}", out, sizeof(out));
  expect(strstr(out, "reason=no_data") != NULL, "empty stats report no_data");
}

static void test_unknown_tool_is_not_claimed(void)
{
  char out[64];

  seed_table();
  seed_snapshot_scaled();

  /* Returning ERROR lets the registry fall through to the next provider. */
  expect(run_tool("write_file", "{}", out, sizeof(out)) != 0,
         "a tool we do not own returns ERROR");
}

static void test_output_is_bounded(void)
{
  char small[96];

  seed_table();
  seed_snapshot_scaled();

  run_tool("vg_point_read", "{}", small, sizeof(small));
  expect(strlen(small) < sizeof(small), "list output respects a small buffer");

  /* Saturated output must still be NUL-terminated. */
  run_tool("vg_point_read", "{}", small, sizeof(small));
  expect(small[sizeof(small) - 1] == '\0',
         "small buffer stays NUL-terminated");
}

int main(void)
{
  vg_agent_tools_register();

  expect(g_provider_tools != NULL && g_provider_exec != NULL,
         "registration installed a provider");

  if (g_provider_tools == NULL || g_provider_exec == NULL)
    {
      fprintf(stderr, "test_agent_tools: provider not registered\n");
      return 1;
    }

  test_tools_json_is_valid_and_complete();
  test_point_read_returns_scaled_value();
  test_point_read_by_chinese_name();
  test_point_read_ambiguous_lists_candidates();
  test_point_read_no_query_lists_table();
  test_point_read_no_table();
  test_point_read_missing_table_with_dead_store();
  test_point_read_without_snapshot();
  test_point_read_not_found();
  test_owned_tool_always_returns_ok();
  test_query_argument_guards();
  test_run_report();
  test_unknown_tool_is_not_claimed();
  test_output_is_bounded();

  if (g_fail != 0)
    {
      fprintf(stderr, "test_agent_tools: %d failure(s)\n", g_fail);
      return 1;
    }

  printf("test_agent_tools: OK\n");
  return 0;
}
