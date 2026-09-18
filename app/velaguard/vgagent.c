/****************************************************************************
 * app/velaguard/vgagent.c
 *
 * NSH probe for the board-driven agent round channel (vg_agent_round).
 *
 * The HMI normally drives this channel from its file worker; this tool exists
 * so the channel can be exercised on its own during bring-up and acceptance,
 * and so the last round's outcome can be read back over NSH without scraping
 * syslog.
 *
 *   vgagent status          round state, generation, last reply
 *   vgagent advice          what the alarm page is being handed (read-only)
 *   vgagent ask <text>      queue one round and wait for it
 *   vgagent clear           release a DONE/ERROR slot
 *   vgagent tools           the tool definitions the model is offered
 *   vgagent tool <name> [json]  run one read-only tool, print its raw answer
 ****************************************************************************/

#include <nuttx/config.h>

#ifdef CONFIG_EXAMPLES_AI_AGENT_VELA

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "tools/tool_provider.h"

#include "vg_agent_round.h"
#include "vg_provision.h"

#ifdef CONFIG_VG_HMI
#  include "vg_advice.h"
#endif

#define VGAGENT_ASK_MAX 1024

static const char *state_name(enum vg_agent_round_state st)
{
  switch (st)
    {
      case VG_AGENT_ROUND_IDLE:
        return "idle";
      case VG_AGENT_ROUND_QUEUED:
        return "queued";
      case VG_AGENT_ROUND_RUNNING:
        return "running";
      case VG_AGENT_ROUND_DONE:
        return "done";
      case VG_AGENT_ROUND_ERROR:
        return "error";
      default:
        return "?";
    }
}

static void usage(void)
{
  printf("Usage:\n");
  printf("  vgagent status\n");
  printf("  vgagent advice\n");
  printf("  vgagent ask <text>\n");
  printf("  vgagent clear\n");
  printf("  vgagent tools\n");
  printf("  vgagent tool <name> [json]\n");
  printf("    a bare word is wrapped as {\"query\":\"<word>\"}, because NSH\n");
  printf("    strips the quotes a JSON argument needs\n");
}

/* ── read-only tool diagnostics ─────────────────────────────────────
 *
 * A round needs a reachable LLM, so when the model backend is down these two
 * subcommands are the only way to show on the board that the tools themselves
 * work.  They are read-only: they run the same guard and audit path the ReAct
 * loop does, and neither writes board state.
 *
 * The tool JSON is printed in full because a provider whose definitions do not
 * parse is silently dropped by the registry, which from the outside looks
 * exactly like the model choosing not to call anything.  Seeing the string is
 * what tells those two apart.
 */

static void print_tools(void)
{
  char *json = tool_registry_get_tools_json();

  if (json == NULL)
    {
      printf("tools: none (registry not initialized)\n");
      return;
    }

  printf("tools: %u bytes\n", (unsigned)strlen(json));

  /* Print the VelaGuard-owned slice, not the whole registry: the builtin
   * tools are long and would bury the part being checked. */

  {
    const char *p = json;
    int found = 0;

    while ((p = strstr(p, "\"vg_")) != NULL)
      {
        const char *end = strchr(p, '}');

        printf("tool: %.200s}\n", p);
        found++;
        p = (end != NULL) ? end + 1 : p + 1;
      }

    if (found == 0)
      {
        printf("tools: no vg_* tool registered\n");
      }
  }

  free(json);
}

static int run_tool_cmd(int argc, char *argv[])
{
  static char out[2048];
  static char args_buf[128];
  const char *name;
  const char *args;
  int rc;

  if (argc < 3)
    {
      fprintf(stderr, "vgagent: tool needs a name\n");
      return 1;
    }

  name = argv[2];
  args = (argc >= 4) ? argv[3] : "{}";

  /* NSH's tokenizer eats the quotes in a JSON argument, so a quoted
   * {"query":"ups_load"} arrives as {query:ups_load} and fails to parse.  A
   * bare word is therefore accepted for the single-string-parameter case and
   * wrapped here.  Point ids and names cannot contain a quote or a backslash
   * (vg_point_validate_name rejects both), so this cannot break the JSON. */
  if (args[0] != '{')
    {
      if (strlen(args) > 64)
        {
          fprintf(stderr, "vgagent: argument too long\n");
          return 1;
        }

      snprintf(args_buf, sizeof(args_buf), "{\"query\":\"%s\"}", args);
      args = args_buf;
    }

  out[0] = '\0';
  rc = tool_registry_execute(name, args, out, sizeof(out));

  printf("tool %s rc=%d\n", name, rc);
  printf("%s", out);
  if (out[0] != '\0' && out[strlen(out) - 1] != '\n')
    {
      printf("\n");
    }

  return (rc == 0) ? 0 : 1;
}

static const char *owner_name(enum vg_agent_round_owner owner)
{
  switch (owner)
    {
      case VG_AGENT_ROUND_OWNER_ADVICE:
        return "advice";
      case VG_AGENT_ROUND_OWNER_DAILY:
        return "daily";
      case VG_AGENT_ROUND_OWNER_NONE:
        return "none";
      default:
        return "?";
    }
}

static void print_status(void)
{
  printf("round: state=%s gen=%u owner=%s\n",
         state_name(vg_agent_round_state()),
         (unsigned)vg_agent_round_generation(),
         owner_name(vg_agent_round_owner()));

  /* One line that answers "why is there no AI".  Without it the reader has to
   * enter the agent CLI and run config_show/router_status to learn that the
   * credentials are gone, which is how a missing key was mistaken for a bug in
   * the feature consuming the answer. */

  printf("llm: credentials=%s provision_file=%s\n",
         vg_llm_credentials_ready() ? "ready" : "MISSING",
         vg_provision_is_present() ? "present" : "absent");

  if (vg_agent_round_last_reply()[0] != '\0')
    {
      printf("reply: %s\n", vg_agent_round_last_reply());
    }
}

/* Join argv[2..] so the question can be passed without quoting games. */

static int join_args(int argc, char *argv[], char *out, size_t cap)
{
  size_t used = 0;
  int i;

  out[0] = '\0';

  for (i = 2; i < argc; i++)
    {
      size_t len = strlen(argv[i]);

      if (used + len + 2 > cap)
        {
          return -1;
        }

      if (i > 2)
        {
          out[used++] = ' ';
        }

      memcpy(out + used, argv[i], len);
      used += len;
      out[used] = '\0';
    }

  return (used > 0) ? 0 : -1;
}

int main(int argc, char *argv[])
{
  static char ask[VGAGENT_ASK_MAX];
  uint32_t start;
  int rc;

  if (argc < 2)
    {
      usage();
      return 1;
    }

  if (strcmp(argv[1], "status") == 0)
    {
      print_status();
      return 0;
    }

#ifdef CONFIG_VG_HMI
  if (strcmp(argv[1], "advice") == 0)
    {
      /* Read-only: the alarm page's inputs, so acceptance can assert on the
       * console that advice is reaching the screen. */

      print_status();
      vg_advice_probe_dump();
      return 0;
    }
#endif

  if (strcmp(argv[1], "clear") == 0)
    {
      vg_agent_round_clear();
      print_status();
      return 0;
    }

  if (strcmp(argv[1], "tools") == 0)
    {
      print_tools();
      return 0;
    }

  if (strcmp(argv[1], "tool") == 0)
    {
      return run_tool_cmd(argc, argv);
    }

  if (strcmp(argv[1], "ask") != 0)
    {
      usage();
      return 1;
    }

  if (join_args(argc, argv, ask, sizeof(ask)) != 0)
    {
      fprintf(stderr, "vgagent: empty or oversized question\n");
      return 1;
    }

  vg_agent_round_clear();

  rc = vg_agent_round_queue(ask);
  if (rc != 0)
    {
      fprintf(stderr, "vgagent: queue failed: %d\n", rc);
      return 1;
    }

  printf("vgagent: queued %u bytes, waiting (up to %u s)...\n",
         (unsigned)strlen(ask),
         (unsigned)(VG_AGENT_ROUND_TIMEOUT_MS / 1000u));

  /* Drive the channel from here as well: outside the HMI this is the only
   * ticker, and alongside it the submission guard keeps the two from
   * pushing the same request twice. */

  start = vg_agent_round_now_ms();

  while (vg_agent_round_busy())
    {
      vg_agent_round_tick();
      usleep(200000);

      if (vg_agent_round_now_ms() - start >
          VG_AGENT_ROUND_TIMEOUT_MS + 10000u)
        {
          break;
        }
    }

  print_status();

  /* Release the slot: the queue refuses a new round while any result is
   * still unconsumed, and the HMI flows only ever clear their own. */

  rc = (vg_agent_round_state() == VG_AGENT_ROUND_DONE) ? 0 : 1;
  vg_agent_round_clear();

  return rc;
}

#endif /* CONFIG_EXAMPLES_AI_AGENT_VELA */
