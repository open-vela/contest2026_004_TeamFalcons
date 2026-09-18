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
 ****************************************************************************/

#include <nuttx/config.h>

#ifdef CONFIG_EXAMPLES_AI_AGENT_VELA

#include <stdio.h>
#include <string.h>
#include <unistd.h>

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
