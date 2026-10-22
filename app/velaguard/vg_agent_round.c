/****************************************************************************
 * app/velaguard/vg_agent_round.c
 ****************************************************************************/

#include <nuttx/config.h>

#ifdef CONFIG_EXAMPLES_AI_AGENT_VELA

#include <errno.h>
#include <pthread.h>
#include <string.h>
#include <syslog.h>
#include <time.h>

#include <velaclaw/client.h>

#include "vg_agent_round.h"

#define TAG "vgagent"

#define VG_ROUND_REQ_MAX   1536
#define VG_ROUND_REPLY_MAX 192

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;

static velaclaw_client_t          *g_client;
static enum vg_agent_round_state   g_state = VG_AGENT_ROUND_IDLE;
static uint32_t                    g_started_ms;
static uint32_t                    g_generation;
static uint32_t                    g_request_serial;
static bool                        g_reply_seen;
static bool                        g_reply_ok;
static bool                        g_submitting;
static enum vg_agent_round_owner   g_owner;
static uint32_t                    g_finished_ms;

static char g_req[VG_ROUND_REQ_MAX];
static char g_reply[VG_ROUND_REPLY_MAX];

uint32_t vg_agent_round_now_ms(void)
{
  struct timespec ts;

  (void)clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint32_t)((uint64_t)ts.tv_sec * 1000u
                    + (uint64_t)ts.tv_nsec / 1000000u);
}

/* Runs on the agent's outbound dispatch thread, so it only records the
 * outcome and never blocks on anything but this mutex. */

static void round_reply(int status, const char *text, void *cookie)
{
  pthread_mutex_lock(&g_lock);
  if ((uint32_t)(uintptr_t)cookie != g_request_serial ||
      (g_state != VG_AGENT_ROUND_QUEUED && g_state != VG_AGENT_ROUND_RUNNING))
    { pthread_mutex_unlock(&g_lock); return; }
  g_reply_seen = true;
  g_reply_ok = (status == 0);

  if (text != NULL)
    {
      strncpy(g_reply, text, sizeof(g_reply) - 1);
      g_reply[sizeof(g_reply) - 1] = '\0';
    }
  else
    {
      g_reply[0] = '\0';
    }

  pthread_mutex_unlock(&g_lock);
}

int vg_agent_round_init(void)
{
  int rc = 0;

  pthread_mutex_lock(&g_lock);

  if (g_client == NULL)
    {
      /* Fails while the agent's message bus is still coming up; the worker
       * retries on later ticks until VG_AGENT_ROUND_QUEUE_TIMEOUT_MS. */

      g_client = velaclaw_client_open("velaguard");
      if (g_client != NULL)
        {
          syslog(LOG_INFO, "[%s] local agent client ready\n", TAG);
        }
    }

  if (g_client == NULL)
    {
      rc = -ENODEV;
    }

  pthread_mutex_unlock(&g_lock);
  return rc;
}

int vg_agent_round_queue(const char *text)
{
  return vg_agent_round_queue_owned(text, VG_AGENT_ROUND_OWNER_NONE);
}

int vg_agent_round_queue_owned(const char *text,
                               enum vg_agent_round_owner owner)
{
  size_t len;

  if (text == NULL)
    {
      return -EINVAL;
    }

  len = strlen(text);
  if (len == 0)
    {
      return -EINVAL;
    }

  if (len >= VG_ROUND_REQ_MAX)
    {
      return -E2BIG;
    }

  pthread_mutex_lock(&g_lock);

  /* A finished result that its owner has not read yet is not free: letting
   * another flow overwrite it would discard an artifact that is already on
   * eMMC.  Both consumers clear their own result within a tick, so this only
   * ever costs the new request one worker iteration. */

  if (g_state != VG_AGENT_ROUND_IDLE)
    {
      pthread_mutex_unlock(&g_lock);
      return -EBUSY;
    }

  if (++g_request_serial == 0) ++g_request_serial;
  memcpy(g_req, text, len + 1);
  g_owner = owner;
  g_state = VG_AGENT_ROUND_QUEUED;
  g_started_ms = vg_agent_round_now_ms();
  g_reply_seen = false;
  g_reply_ok = false;
  g_reply[0] = '\0';

  pthread_mutex_unlock(&g_lock);
  return 0;
}

static void round_submit(void)
{
  /* Function-local so a 1.5 KB request never lands on the worker stack, and
   * released before the call so the reply callback can never contend with us
   * on the same lock if a build ever dispatches replies synchronously. */

  static char req[VG_ROUND_REQ_MAX];
  velaclaw_client_t *client;
  velaclaw_ask_req_t ask;
  int rc;
  uint32_t serial;

  pthread_mutex_lock(&g_lock);
  if (g_client == NULL)
    {
      pthread_mutex_unlock(&g_lock);

      if (vg_agent_round_init() != 0)
        {
          return;
        }

      pthread_mutex_lock(&g_lock);
    }

  client = g_client;
  memcpy(req, g_req, sizeof(req));
  serial = g_request_serial;
  pthread_mutex_unlock(&g_lock);

  ask.text = req;
  ask.timeout_ms = (int)VG_AGENT_ROUND_TIMEOUT_MS;

  rc = velaclaw_ask_async(client, &ask, round_reply, (void *)(uintptr_t)serial);

  pthread_mutex_lock(&g_lock);

  /* abandon() may have run while velaclaw_ask_async was in flight.  In that
   * case the question is gone: leave the state IDLE instead of resurrecting
   * it, or the next question is refused as busy.  The reply is dropped by
   * its serial either way. */

  if (rc == 0 && g_state == VG_AGENT_ROUND_QUEUED)
    {
      g_state = VG_AGENT_ROUND_RUNNING;
      g_started_ms = vg_agent_round_now_ms();
      syslog(LOG_INFO, "[%s] round submitted: %u bytes\n",
             TAG, (unsigned)strlen(req));
    }
  else if (rc == 0)
    {
      syslog(LOG_INFO, "[%s] round abandoned during submit\n", TAG);
    }
  else if (rc == -EBUSY)
    {
      /* The channel already carries a background round.  A user question
       * waits behind it instead of failing on the spot: the state stays
       * QUEUED and later ticks retry until the queue timeout. */

    }
  else
    {
      g_state = VG_AGENT_ROUND_ERROR;
      g_finished_ms = vg_agent_round_now_ms();
      syslog(LOG_WARNING, "[%s] round submit failed: %d\n", TAG, rc);
    }

  pthread_mutex_unlock(&g_lock);
}

void vg_agent_round_tick(void)
{
  pthread_mutex_lock(&g_lock);
  if (g_state == VG_AGENT_ROUND_QUEUED)
    {
      if (g_submitting) { pthread_mutex_unlock(&g_lock); return; }
      g_submitting = true;
      pthread_mutex_unlock(&g_lock);
      round_submit();
      pthread_mutex_lock(&g_lock);
      g_submitting = false;
      if (g_state == VG_AGENT_ROUND_QUEUED &&
          (int32_t)(vg_agent_round_now_ms() - g_started_ms) >=
          (int32_t)VG_AGENT_ROUND_QUEUE_TIMEOUT_MS)
        {
          g_state = VG_AGENT_ROUND_ERROR;
          g_finished_ms = vg_agent_round_now_ms();
          syslog(LOG_WARNING, "[%s] round never reached the agent\n", TAG);
        }
      pthread_mutex_unlock(&g_lock);
      return;
    }
  if (g_state != VG_AGENT_ROUND_RUNNING)
    { pthread_mutex_unlock(&g_lock); return; }

  uint32_t now = vg_agent_round_now_ms();
  if (g_reply_seen)
    {
      bool ok = g_reply_ok;
      g_reply_seen = false;
      g_state = ok ? VG_AGENT_ROUND_DONE : VG_AGENT_ROUND_ERROR;
      if (ok) g_generation++;
      g_finished_ms = now;
      pthread_mutex_unlock(&g_lock);
      syslog(LOG_INFO, "[%s] round finished: %s\n", TAG, ok ? "reply" : "agent error");
      return;
    }
  if ((int32_t)(now - g_started_ms) >= (int32_t)VG_AGENT_ROUND_TIMEOUT_MS)
    {
      g_state = VG_AGENT_ROUND_ERROR;
      g_finished_ms = now;
      pthread_mutex_unlock(&g_lock);
      syslog(LOG_WARNING, "[%s] round timed out\n", TAG);
      return;
    }
  pthread_mutex_unlock(&g_lock);
}

enum vg_agent_round_state vg_agent_round_state(void)
{
  enum vg_agent_round_state st;

  pthread_mutex_lock(&g_lock);
  st = g_state;
  pthread_mutex_unlock(&g_lock);
  return st;
}

bool vg_agent_round_busy(void)
{
  enum vg_agent_round_state st = vg_agent_round_state();

  return (st == VG_AGENT_ROUND_QUEUED || st == VG_AGENT_ROUND_RUNNING);
}

uint32_t vg_agent_round_generation(void)
{
  uint32_t gen;

  pthread_mutex_lock(&g_lock);
  gen = g_generation;
  pthread_mutex_unlock(&g_lock);
  return gen;
}

enum vg_agent_round_owner vg_agent_round_owner(void)
{
  enum vg_agent_round_owner owner;

  pthread_mutex_lock(&g_lock);
  owner = g_owner;
  pthread_mutex_unlock(&g_lock);
  return owner;
}

bool vg_agent_round_reclaim(enum vg_agent_round_owner mine, uint32_t grace_ms)
{
  bool released = false;

  pthread_mutex_lock(&g_lock);

  if ((g_state == VG_AGENT_ROUND_DONE || g_state == VG_AGENT_ROUND_ERROR) &&
      g_owner != mine && g_finished_ms != 0 &&
      vg_agent_round_now_ms() - g_finished_ms >= grace_ms)
    {
      /* Somebody else's finished round that nobody is going to read - a
       * manual round killed half way, or a flow that went away.  Without
       * this the strict queue would hold the channel for it forever. */

      g_state = VG_AGENT_ROUND_IDLE;
      g_owner = VG_AGENT_ROUND_OWNER_NONE;
      released = true;
    }

  pthread_mutex_unlock(&g_lock);
  return released;
}

void vg_agent_round_clear(void)
{
  pthread_mutex_lock(&g_lock);

  if (g_state == VG_AGENT_ROUND_DONE || g_state == VG_AGENT_ROUND_ERROR)
    {
      g_state = VG_AGENT_ROUND_IDLE;
      g_owner = VG_AGENT_ROUND_OWNER_NONE;
      g_finished_ms = 0;
    }

  pthread_mutex_unlock(&g_lock);
}

void vg_agent_round_abandon(void)
{
  velaclaw_client_t *client;

  pthread_mutex_lock(&g_lock);

  client = g_client;

  if (g_state == VG_AGENT_ROUND_QUEUED || g_state == VG_AGENT_ROUND_RUNNING)
    {
      g_state = VG_AGENT_ROUND_IDLE;
      g_owner = VG_AGENT_ROUND_OWNER_NONE;
      g_finished_ms = 0;
      g_reply_seen = false;

      /* A late reply must not be attributed to the next question. */

      if (++g_request_serial == 0)
        {
          ++g_request_serial;
        }
    }

  pthread_mutex_unlock(&g_lock);

  if (client != NULL)
    {
      velaclaw_client_abandon(client);
    }
}

const char *vg_agent_round_last_reply(void)
{
  return g_reply;
}

#endif /* CONFIG_EXAMPLES_AI_AGENT_VELA */
