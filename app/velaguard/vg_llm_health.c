/****************************************************************************
 * app/velaguard/vg_llm_health.c
 *
 * Periodic liveness probe for the MiMo LLM link; see vg_llm_health.h for
 * the design.  The classification follows the CLIProxyAPI failure taxonomy:
 * transport errors and server-side 5xx mark the link down, endpoint answers
 * that prove the service is alive (200, and 401/403/429 for the fixed test
 * credential) mark it up.  A 404 means the configured path is wrong, which
 * breaks the real chain, so it is down too.
 *
 * Probes never carry the real API key and never log request content; the
 * syslog line records the transport return code and HTTP status only.
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <syslog.h>
#include <time.h>
#include <unistd.h>

#include <agent_http.h>
#include <infra/config_store.h>

#include "vg_agent_round.h"
#include "vg_llm_health.h"

#ifdef VG_VOICE_ROUND
#  include "vg_voice_round.h"
#endif

#define TAG "vgllm"

/* One probe is bounded well below the interval: a hung endpoint must not
 * overlap the next round of probes. */

#define VG_LLM_HEALTH_PROBE_TIMEOUT_MS 15000u

#ifndef CONFIG_VG_LLM_HEALTH_INTERVAL_MS
#  define CONFIG_VG_LLM_HEALTH_INTERVAL_MS 30000u
#endif

#ifndef CONFIG_VG_LLM_HEALTH_STACKSIZE
#  define CONFIG_VG_LLM_HEALTH_STACKSIZE 8192
#endif

/* Slice granularity of the wait loop; the wake source is a flag checked
 * between slices, so a kick lands within half a second without any
 * signal primitives that could misbehave across a wall-clock sync. */

#define VG_LLM_HEALTH_SLICE_MS 500u

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;

static bool       g_started;
static volatile bool g_kick;

/* Published probe verdict, guarded by g_lock. */

static bool       g_seen;         /* a probe finished at least once */
static bool       g_ok;           /* verdict of the last finished probe */
static int        g_last_rc;      /* agent_http_probe transport return */
static int        g_last_status;  /* HTTP status, 0 when none */
static int        g_fails;        /* consecutive failed probes */

static uint32_t now_ms(void)
{
  struct timespec ts;

  (void)clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint32_t)((uint64_t)ts.tv_sec * 1000u
                    + (uint64_t)ts.tv_nsec / 1000000u);
}

static void publish(bool ok, int rc, int status)
{
  bool changed;

  pthread_mutex_lock(&g_lock);
  changed = !g_seen || g_ok != ok;
  g_seen = true;
  g_ok = ok;
  g_last_rc = rc;
  g_last_status = status;
  g_fails = ok ? 0 : g_fails + 1;
  pthread_mutex_unlock(&g_lock);

  if (changed)
    {
      syslog(ok ? LOG_INFO : LOG_WARNING,
             "[%s] link %s: rc=%d status=%d\n",
             TAG, ok ? "up" : "down", rc, status);
    }
}

/* Runs the probe on the monitor thread and publishes the verdict.  The
 * endpoint comes from the agent config so the probe measures the chain the
 * product actually uses; with no host configured the link is down by
 * definition and the probe retries on the same interval. */

static void probe_once(void)
{
  char host[128];
  char path[128];
  char port[8];
  vela_req_ctl_t ctl;
  size_t len = 0;
  int status = 0;
  int rc;
  bool ok;

  /* The probe's TLS must never overlap a real round: the board's HTTPS
   * calls were effectively serialized before this monitor existed, and a
   * concurrent handshake is new stress on a transport with known
   * instabilities.  A round finishing on its own counts as evidence
   * anyway, so skipping while busy loses nothing. */

  if (vg_agent_round_busy())
    {
      return;
    }

#ifdef VG_VOICE_ROUND
  /* The voice pipeline makes its own HTTPS calls (ASR, TTS) outside the
   * agent round channel, so the round-busy check above does not cover
   * recording through playback.  Skip while any voice phase is active. */

  {
    static struct vg_voice_ui ui;

    if (vg_voice_round_ui(&ui) && ui.state != VG_VS_IDLE)
      {
        return;
      }
  }
#endif

  host[0] = '\0';
  path[0] = '\0';
  if (claw_config_get("llm_host", host, sizeof(host)) != OK || !host[0])
    {
      publish(false, 0, 0);
      return;
    }
  if (claw_config_get("llm_path", path, sizeof(path)) != OK || !path[0])
    {
      strncpy(path, "/v1/chat/completions", sizeof(path) - 1);
      path[sizeof(path) - 1] = '\0';
    }
  if (claw_config_get("llm_port", port, sizeof(port)) != OK || !port[0])
    {
      strncpy(port, "443", sizeof(port) - 1);
      port[sizeof(port) - 1] = '\0';
    }

  ctl.deadline_ms = now_ms() + VG_LLM_HEALTH_PROBE_TIMEOUT_MS;
  if (ctl.deadline_ms == 0)
    {
      ctl.deadline_ms = 1;
    }
  ctl.cancel = NULL;

  rc = agent_http_probe(host, port, path, &ctl, &len, &status);
  ok = (rc == 0) &&
       (status == 200 || status == 401 || status == 403 || status == 429);

  publish(ok, rc, status);
}

static void *monitor_thread(void *arg)
{
  (void)arg;

  probe_once();

  for (;;)
    {
      uint32_t waited = 0;
      bool kicked = g_kick;

      g_kick = false;
      if (!kicked)
        {
          while (waited < CONFIG_VG_LLM_HEALTH_INTERVAL_MS)
            {
              usleep(VG_LLM_HEALTH_SLICE_MS * 1000u);
              waited += VG_LLM_HEALTH_SLICE_MS;
              if (g_kick)
                {
                  g_kick = false;
                  break;
                }
            }
        }

      probe_once();
    }

  return NULL;
}

void vg_llm_health_start(void)
{
  pthread_t tid;
  pthread_attr_t attr;

  pthread_mutex_lock(&g_lock);
  if (g_started)
    {
      pthread_mutex_unlock(&g_lock);
      return;
    }
  g_started = true;
  pthread_mutex_unlock(&g_lock);

  pthread_attr_init(&attr);
  pthread_attr_setstacksize(&attr, CONFIG_VG_LLM_HEALTH_STACKSIZE);
  if (pthread_create(&tid, &attr, monitor_thread, NULL) != 0)
    {
      pthread_attr_destroy(&attr);
      pthread_mutex_lock(&g_lock);
      g_started = false;
      pthread_mutex_unlock(&g_lock);
      syslog(LOG_ERR, "[%s] monitor start failed\n", TAG);
      return;
    }
  pthread_attr_destroy(&attr);
  pthread_detach(tid);
  syslog(LOG_INFO, "[%s] monitor started, interval=%u ms\n", TAG,
         (unsigned)CONFIG_VG_LLM_HEALTH_INTERVAL_MS);
}

void vg_llm_health_note_round_ok(void)
{
  bool changed;

  pthread_mutex_lock(&g_lock);
  changed = !g_seen || !g_ok;
  g_seen = true;
  g_ok = true;
  g_last_rc = 0;
  g_last_status = 0;
  g_fails = 0;
  pthread_mutex_unlock(&g_lock);

  if (changed)
    {
      syslog(LOG_INFO, "[%s] link up: real round finished\n", TAG);
    }
}

void vg_llm_health_kick(void)
{
  g_kick = true;
}

bool vg_llm_health_status(bool *ok)
{
  bool seen;

  pthread_mutex_lock(&g_lock);
  seen = g_seen;
  if (seen && ok != NULL)
    {
      *ok = g_ok;
    }
  pthread_mutex_unlock(&g_lock);
  return seen;
}
