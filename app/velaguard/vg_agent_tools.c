/****************************************************************************
 * app/velaguard/vg_agent_tools.c
 *
 * Two read-only tools for the on-board agent, registered through the
 * ai_agent provider hook (packages/ai_agent include/tools/tool_provider.h):
 *
 *   vg_point_read {query?}  live value from the confirmed point table
 *   vg_run_report {}        since-boot report, same text as the report page
 *
 * Neither opens RS485.  vg_point_read reads the HMI poll snapshot and the
 * committed table, exactly what NSH `vgpoint get` does and what
 * docs/velaguard-host-nsh-protocol.md requires of it: taking the bus lock
 * here would stall the poller behind an LLM round that can last minutes.
 ****************************************************************************/

#include <nuttx/config.h>

#ifdef CONFIG_EXAMPLES_AI_AGENT_VELA

/* Each tool depends on a VelaGuard module that not every target builds.
 * velaguard-ai-probe, for instance, enables ai_agent without the frame-stat
 * or point-table modules, so a tool whose data source is absent must not be
 * advertised: the model would call it and get a link error instead of data. */

#ifdef CONFIG_VG_FRAME_STATS
#  define VG_AGENT_TOOLS_HAVE_REPORT 1
#endif

/* vg_point_table.c is built by either discover switch (app/velaguard/Makefile
 * links it for CONFIG_VG_BUS_DISCOVER and, when that is off, for
 * CONFIG_VG_HMI_DISCOVER), so the point table is readable under both. */
#if defined(CONFIG_VG_BUS_DISCOVER) || defined(CONFIG_VG_HMI_DISCOVER)
#  define VG_AGENT_TOOLS_HAVE_POINTS 1
#endif

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <netutils/cJSON.h>

#include "tools/tool_provider.h"

#include "vg_agent_query.h"
#include "vg_agent_tools.h"
#include "vg_runtime.h"

#if defined(VG_AGENT_TOOLS_HAVE_POINTS) || defined(VG_AGENT_TOOLS_HAVE_REPORT)

/* The agent loop hands a tool a 4 KB buffer on the HMI build
 * (TOOL_OUTPUT_SIZE).  Stay well under it so the model never receives a
 * half-line: the list form is the only one that can grow. */
#define VG_AGENT_TOOL_OUT_MAX 2048

/* Argument caps.  tool_guard.c enforces a 32 KB input limit and a call-rate
 * window but does not validate a tool's JSON Schema, so the bounds a schema
 * advertises have to be enforced here too. */
#define VG_AGENT_TOOL_QUERY_MAX 48

#ifndef CONFIG_VG_DISCOVER_POINTS_PATH
#  define CONFIG_VG_DISCOVER_POINTS_PATH "/data/velaguard/config/points.json"
#endif

#ifndef CONFIG_VG_LIVE_VALUES_PATH
#  define CONFIG_VG_LIVE_VALUES_PATH "/data/velaguard/live/values.txt"
#endif

#ifndef CONFIG_VG_STORE_ROOT
#  define CONFIG_VG_STORE_ROOT "/data"
#endif

#endif /* any tool */

#ifdef VG_AGENT_TOOLS_HAVE_POINTS

#include "vg_discover.h"

/* ── table + snapshot join ───────────────────────────────────────── */

/* Build the agent-facing view of the confirmed table.  Returns 0 when values
 * are available, or a vg_agent_query_rc reason otherwise.  Values come from
 * the live snapshot, which is written with tmp+rename so a reader never sees
 * a torn file. */

static enum vg_agent_query_rc collect_points(struct vg_agent_point *out,
                                             int *n_out)
{
  struct vg_discover_summary table;
  struct vg_live_snapshot snap;
  bool have_snap = false;
  enum vg_agent_query_rc rc = VG_AGENT_QUERY_OK;
  uint32_t age = 0;
  int i;
  int rc_read;

  if (out == NULL || n_out == NULL)
    {
      return VG_AGENT_QUERY_ARG;
    }

  *n_out = 0;
  rc_read = vg_point_table_read(&table, CONFIG_VG_DISCOVER_POINTS_PATH);

  if (rc_read == -ENOENT)
    {
      /* An absent table is empty only while the store itself is reachable.
       * When /data is gone the honest answer is "cannot read", not "no
       * points configured": the two look identical otherwise, and the NSH
       * tools separate them the same way (vgpoint.c cmd_get). */
      if (vg_point_store_root_ok(CONFIG_VG_STORE_ROOT) == 0)
        {
          memset(&table, 0, sizeof(table));
          rc_read = 0;
        }
    }

  if (rc_read != 0 || table.n_points <= 0)
    {
      return VG_AGENT_QUERY_NO_TABLE;
    }

  if (vg_live_snapshot_read(CONFIG_VG_LIVE_VALUES_PATH, &snap) == 0)
    {
      have_snap = true;
      age = vg_live_age_ms(snap.tick_ms, vg_live_now_ms());
    }

  for (i = 0; i < table.n_points && i < VG_AGENT_QUERY_MAX; i++)
    {
      const struct vg_point_entry *e = &table.points[i];
      struct vg_agent_point *p = &out[i];
      int idx;

      memset(p, 0, sizeof(*p));
      snprintf(p->id, sizeof(p->id), "%s", e->id);
      snprintf(p->name, sizeof(p->name), "%s",
               e->name[0] ? e->name : e->id);
      snprintf(p->unit, sizeof(p->unit), "%s", e->unit);
      snprintf(p->cmp, sizeof(p->cmp), "%s", e->cmp);
      p->has_warn = e->has_warn != 0;
      p->warn = e->warn;
      p->has_crit = e->has_crit != 0;
      p->crit = e->crit;

      if (!have_snap)
        {
          continue;
        }

      idx = vg_live_snapshot_find_id(&snap, e->id);
      if (idx < 0)
        {
          continue;
        }

      p->has_sample = true;
      p->ok = snap.samples[idx].ok != 0;
      p->value = snap.samples[idx].value;
      p->age_ms = age;
    }

  if (!have_snap)
    {
      /* The table is known but nothing has been sampled yet.  Report the
       * thresholds without inventing values. */
      rc = VG_AGENT_QUERY_NO_SAMPLE;
    }

  *n_out = (table.n_points < VG_AGENT_QUERY_MAX)
               ? table.n_points
               : VG_AGENT_QUERY_MAX;
  return rc;
}

static int tool_point_read(const char *query, char *out, size_t cap)
{
  struct vg_agent_point points[VG_AGENT_QUERY_MAX];
  struct vg_agent_query_result res;
  enum vg_agent_query_rc collect_rc;
  enum vg_agent_query_rc rc;
  int n = 0;

  collect_rc = collect_points(points, &n);

  if (collect_rc == VG_AGENT_QUERY_NO_TABLE)
    {
      snprintf(out, cap,
               "vgquery: reason=no_table msg=已确认点表不可读或尚未导入\n");
      return OK;
    }

  if (n <= 0)
    {
      snprintf(out, cap, "vgquery: reason=no_table msg=已确认点表为空\n");
      return OK;
    }

  rc = vg_agent_query_match(points, n, query, &res);

  switch (rc)
    {
      case VG_AGENT_QUERY_OK:
        vg_agent_query_format_point(out, cap, &points[res.index],
                                    vg_agent_query_is_stale(
                                        &points[res.index]));
        break;

      case VG_AGENT_QUERY_AMBIG:
        vg_agent_query_format_candidates(out, cap, points, n, &res);
        break;

      case VG_AGENT_QUERY_NOT_FOUND:
        vg_agent_query_format_list(out, cap, points, n);
        snprintf(out + strlen(out), cap - strlen(out),
                 "vgquery: reason=not_found query=%s\n",
                 (query != NULL) ? query : "");
        break;

      case VG_AGENT_QUERY_LIST:
      default:
        vg_agent_query_format_list(out, cap, points, n);
        break;
    }

  /* Reaching here means the question was answered, including "no sample
   * yet": that is a real answer, and the text says where the data is
   * missing.  Only an unreadable table takes the early return above. */
  return OK;
}

#endif /* VG_AGENT_TOOLS_HAVE_POINTS */

#ifdef VG_AGENT_TOOLS_HAVE_REPORT

static int tool_run_report(char *out, size_t cap)
{
  if (vg_runtime_format_report(out, cap) <= 0)
    {
      snprintf(out, cap, "vgruntime: reason=no_data msg=板上尚无运行统计\n");
    }

  return OK;
}

#endif /* VG_AGENT_TOOLS_HAVE_REPORT */

#if defined(VG_AGENT_TOOLS_HAVE_POINTS) || defined(VG_AGENT_TOOLS_HAVE_REPORT)

/* ── tool definitions ────────────────────────────────────────────── */

static const char *VG_TOOL_POINT_JSON =
  "{"
    "\"name\":\"vg_point_read\","
    "\"description\":\"读取本机已确认点表中一个点位的实时值、单位、采样新鲜度和阈值。"
      "query 可以写点位 id（如 ups_load）或中文点名（如 UPS负载）；留空则列出全部点位。"
      "只读，不发起总线通信。\","
    "\"input_schema\":{"
      "\"type\":\"object\","
      "\"properties\":{"
        "\"query\":{\"type\":\"string\",\"description\":"
          "\"点位 id 或中文点名，例如 ups_load 或 UPS负载；留空列出全部点位\"}"
      "},"
      "\"required\":[]"
    "}"
  "}";

static const char *VG_TOOL_REPORT_JSON =
  "{"
    "\"name\":\"vg_run_report\","
    "\"description\":\"读取本机自本次上电以来的运行报告，含通信质量、点位在线和异常时间线三节。"
      "数字由板上统计产生，不需要参数。只读。\","
    "\"input_schema\":{\"type\":\"object\",\"properties\":{},\"required\":[]}"
  "}";

/* Assemble only the tools whose data source this build actually has. */

static size_t app_str(char *buf, size_t cap, size_t off, const char *s)
{
  size_t n;

  if (buf == NULL || cap == 0 || s == NULL || off >= cap - 1)
    {
      return off;
    }

  n = strlen(s);
  if (n > cap - 1 - off)
    {
      n = cap - 1 - off;
    }

  memcpy(buf + off, s, n);
  buf[off + n] = '\0';
  return off + n;
}

static char *vg_tools_get_json(void)
{
  char buf[2048];
  size_t off = 0;

  buf[0] = '\0';
  off = app_str(buf, sizeof(buf), off, "[");

  /* The comma is chosen at compile time because the set of tools is fixed
   * then; a runtime "is this the first entry" flag would be dead code in
   * every single-tool build. */

#ifdef VG_AGENT_TOOLS_HAVE_POINTS
  off = app_str(buf, sizeof(buf), off, VG_TOOL_POINT_JSON);
#endif

#ifdef VG_AGENT_TOOLS_HAVE_REPORT
#  ifdef VG_AGENT_TOOLS_HAVE_POINTS
  off = app_str(buf, sizeof(buf), off, ",");
#  endif

  off = app_str(buf, sizeof(buf), off, VG_TOOL_REPORT_JSON);
#endif

  (void)app_str(buf, sizeof(buf), off, "]");

  return strdup(buf);
}

/* Extract the optional query string and bound it.  Returns 0 on success and
 * leaves *query NULL when the caller omitted it.  Only vg_point_read takes an
 * argument, so this lives inside that tool's build guard. */

#ifdef VG_AGENT_TOOLS_HAVE_POINTS
static int parse_query(const char *input_json, char *buf, size_t cap,
                       const char **query)
{
  cJSON *root;
  cJSON *item;
  const char *s;
  size_t len;

  *query = NULL;

  if (input_json == NULL || input_json[0] == '\0')
    {
      return 0;
    }

  root = cJSON_Parse(input_json);
  if (root == NULL)
    {
      return -EINVAL;
    }

  item = cJSON_GetObjectItem(root, "query");
  if (item == NULL || cJSON_IsNull(item))
    {
      cJSON_Delete(root);
      return 0;
    }

  if (!cJSON_IsString(item))
    {
      cJSON_Delete(root);
      return -EINVAL;
    }

  s = item->valuestring;
  len = strlen(s);

  if (len >= cap)
    {
      /* Reject rather than truncate: a cut point name could match a
       * different point and answer the wrong question. */
      cJSON_Delete(root);
      return -E2BIG;
    }

  memcpy(buf, s, len + 1);
  *query = buf;
  cJSON_Delete(root);
  return 0;
}
#endif /* VG_AGENT_TOOLS_HAVE_POINTS */

/* ── call rate ───────────────────────────────────────────────────────
 *
 * tool_guard.c rate-limits run_shell / write_file / edit_file only, so a
 * read-only tool has no ceiling there.  AGENTS.md requires the limit to live
 * in C rather than the prompt, and an unthrottled read is still an eMMC read
 * on every iteration, so cap it here.  The ReAct loop already bounds a round
 * (10 iterations, 4 same-name repeats); this bounds the burst inside it.
 * The rule itself lives in vg_agent_query.c so host tests can drive it.
 */

#define VG_TOOL_RATE_WINDOW_MS 60000u
#define VG_TOOL_RATE_MAX_CALLS 20

static struct vg_agent_rate g_rate;

static uint32_t rate_now_ms(void)
{
  struct timespec ts;

  if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
    {
      return 0;
    }

  return (uint32_t)((uint64_t)ts.tv_sec * 1000u
                    + (uint64_t)ts.tv_nsec / 1000000u);
}

static int vg_tools_execute(const char *name, const char *input_json,
                            char *output, size_t output_size)
{
  size_t cap;

  /* input_json is only read by vg_point_read; the report tool takes none. */
  (void)input_json;

  if (name == NULL || output == NULL || output_size == 0)
    {
      return ERROR;
    }

  cap = (output_size > VG_AGENT_TOOL_OUT_MAX)
            ? VG_AGENT_TOOL_OUT_MAX
            : output_size;

  output[0] = '\0';

  /* Only count calls we actually own, so a probe for someone else's tool does
   * not spend this provider's budget, and a tool this build did not compile
   * in is not claimed at all. */
  {
    bool owned = false;

#ifdef VG_AGENT_TOOLS_HAVE_POINTS
    if (strcmp(name, "vg_point_read") == 0)
      {
        owned = true;
      }
#endif

#ifdef VG_AGENT_TOOLS_HAVE_REPORT
    if (strcmp(name, "vg_run_report") == 0)
      {
        owned = true;
      }
#endif

    if (!owned)
      {
        return ERROR;
      }
  }

  if (!vg_agent_rate_allow(&g_rate, rate_now_ms()))
    {
      /* OK, not ERROR: the registry reads a provider's ERROR as "not my
       * tool", skips to the next provider and finally overwrites this buffer
       * with "unknown tool".  A rate-limited call would then reach the model
       * as a tool that does not exist, and it would keep retrying instead of
       * waiting.  Only a name we do not own may return ERROR. */
      snprintf(output, cap,
               "vgquery: reason=rate_limited msg=调用过于频繁，请稍后再试\n");
      return OK;
    }

#ifdef VG_AGENT_TOOLS_HAVE_POINTS
  if (strcmp(name, "vg_point_read") == 0)
    {
      char query[VG_AGENT_TOOL_QUERY_MAX];
      const char *q = NULL;
      int rc;

      rc = parse_query(input_json, query, sizeof(query), &q);

      if (rc == -E2BIG)
        {
          snprintf(output, cap,
                   "vgquery: reason=bad_arg msg=query 过长，上限 %d 字节\n",
                   VG_AGENT_TOOL_QUERY_MAX - 1);
          return OK;
        }

      if (rc != 0)
        {
          snprintf(output, cap,
                   "vgquery: reason=bad_arg msg=query 必须是字符串\n");
          return OK;
        }

      return tool_point_read(q, output, cap);
    }
#endif

#ifdef VG_AGENT_TOOLS_HAVE_REPORT
  if (strcmp(name, "vg_run_report") == 0)
    {
      return tool_run_report(output, cap);
    }
#endif

  /* Not ours: let the registry try the next provider. */
  return ERROR;
}

#endif /* any tool */

/* ── registration ────────────────────────────────────────────────── */

void vg_agent_tools_register(void)
{
#if defined(VG_AGENT_TOOLS_HAVE_POINTS) || defined(VG_AGENT_TOOLS_HAVE_REPORT)
  static bool registered;

  if (registered)
    {
      return;
    }

  tool_registry_register_provider("vg", vg_tools_get_json, vg_tools_execute);
  vg_agent_rate_init(&g_rate, VG_TOOL_RATE_MAX_CALLS, VG_TOOL_RATE_WINDOW_MS);
  registered = true;
#endif
}

#endif /* CONFIG_EXAMPLES_AI_AGENT_VELA */
