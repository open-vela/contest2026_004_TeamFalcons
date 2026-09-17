/****************************************************************************
 * app/velaguard/vg_agent_round.h
 *
 * Board-driven agent rounds.
 *
 * The HMI build does not run ReAct from the heartbeat (heartbeat_send() is
 * gated under CONFIG_VG_HMI) and cron is skipped, so the board itself has to
 * ask the agent for the two proactive flows: explaining the active alarms and
 * writing the daily report.  Both go through this one door so that only a
 * single round is ever outstanding, and so the UI thread never waits.
 *
 * Usage from the board backend:
 *   vg_agent_round_tick()   every file-worker tick, drives submit + watchdog
 *   vg_agent_round_queue()  hand over a request built by vg_ai_contract
 *   vg_agent_round_state()  poll from the UI side, then read the artifact
 ****************************************************************************/

#ifndef __APP_VELAGUARD_VG_AGENT_ROUND_H
#define __APP_VELAGUARD_VG_AGENT_ROUND_H

#include <stdbool.h>
#include <stdint.h>

enum vg_agent_round_state
{
  VG_AGENT_ROUND_IDLE = 0,  /* nothing outstanding; result (if any) consumed */
  VG_AGENT_ROUND_QUEUED,    /* accepted, not yet handed to the agent bus */
  VG_AGENT_ROUND_RUNNING,   /* pushed; waiting for the agent to answer */
  VG_AGENT_ROUND_DONE,      /* the agent answered; read the artifact for truth */
  VG_AGENT_ROUND_ERROR      /* could not be delivered, or the round ran long */
};

/* Which flow asked for the round in flight.  Two flows share this one
 * channel, and a finished round must only be read by the flow that asked
 * for it: the reply text is not the artifact, so the wrong consumer would
 * read a file another request wrote and reject it as stale. */

enum vg_agent_round_owner
{
  VG_AGENT_ROUND_OWNER_NONE = 0,  /* manual round (the NSH probe) */
  VG_AGENT_ROUND_OWNER_ADVICE,    /* per-point alarm advice */
  VG_AGENT_ROUND_OWNER_DAILY      /* the daily report */
};

/* Total budget for one round, in milliseconds.  Measured rounds on this board
 * run 100-190 s wall clock (six ReAct iterations, ~105 s inside the LLM), so
 * this is deliberately far above the 120 s per-call watchdog.  Expiry only
 * means "stop waiting": the artifact carries boot and req, so a late answer
 * is rejected by vg_ai_advice_parse() rather than mistaken for a later round. */

#define VG_AGENT_ROUND_TIMEOUT_MS       300000u

/* How long a queued request may wait for the agent bus to come up. */

#define VG_AGENT_ROUND_QUEUE_TIMEOUT_MS  20000u

/* Opens the local agent client if it is not open yet.  Returns 0 when the
 * client is ready, -ENODEV while the agent is still starting (retry later).
 * Safe to call repeatedly. */

int vg_agent_round_init(void);

/* Hand over one request.  Returns 0 on success, -EBUSY when a round is
 * already queued or running, -EINVAL on an empty request, -E2BIG when the
 * text does not fit the internal buffer.  The text is copied. */

int vg_agent_round_queue(const char *text);

/* Same, but records which flow owns the round.  Consumers must check
 * vg_agent_round_owner() before reading an artifact. */

int vg_agent_round_queue_owned(const char *text,
                               enum vg_agent_round_owner owner);

/* Owner of the current round, or NONE when idle. */

enum vg_agent_round_owner vg_agent_round_owner(void);

/* Drive submission and the watchdog.  Call from the file worker thread. */

void vg_agent_round_tick(void);

/* State and bookkeeping for the consumer side. */

enum vg_agent_round_state vg_agent_round_state(void);
bool                    vg_agent_round_busy(void);

/* Bumped every time a round finishes with a reply, so a consumer can tell a
 * fresh result from one it already handled. */

uint32_t vg_agent_round_generation(void);

/* Returns DONE or ERROR to IDLE so the next round may be queued.  A DONE
 * result must be consumed (the artifact read and validated) before this. */

void vg_agent_round_clear(void);

/* How long a finished result may sit unclaimed before any other flow may
 * release it.  The queue refuses to start a round while a terminal state is
 * still there, so without this a manual round that nobody read (the NSH
 * probe killed half way, say) would hold the channel forever. */

#define VG_AGENT_ROUND_RECLAIM_MS 30000u

/* Releases somebody else's finished-but-unread round once it has been
 * sitting for grace_ms.  Consumers call this with their own owner; returns
 * true when a stale result was released. */

bool vg_agent_round_reclaim(enum vg_agent_round_owner mine, uint32_t grace_ms);

/* Last reply text, truncated for diagnostics (NSH readback, logs). */

const char *vg_agent_round_last_reply(void);

/* Monotonic milliseconds.  Every deadline and rate limit on this path uses
 * this base: the wall clock jumps when vgtime syncs a minute after boot, and
 * one jump would otherwise expire every in-flight round at once. */

uint32_t vg_agent_round_now_ms(void);

#endif /* __APP_VELAGUARD_VG_AGENT_ROUND_H */
