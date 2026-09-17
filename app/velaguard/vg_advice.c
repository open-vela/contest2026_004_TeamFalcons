/****************************************************************************
 * app/velaguard/vg_advice.c
 *
 * Per-point AI alarm advice, board side.
 *
 * Flow: the alarm set changes -> build a VGADV1 request -> queue one agent
 * round -> when the round ends, read what the agent wrote and put it through
 * vg_ai_advice_parse().  Only a document that parses is ever published to the
 * page, which is what lets the page show AI text without owning a
 * fallback path of its own: a miss simply means the deterministic rule
 * summary stays on screen.
 ****************************************************************************/

#include <nuttx/config.h>

#ifdef CONFIG_VG_HMI

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <syslog.h>
#include <time.h>
#include <unistd.h>

#include "model/vg_model.h"
#include "model/vg_ui_backend.h"

#include "vg_advice.h"

#ifdef CONFIG_EXAMPLES_AI_AGENT_VELA
#  include "vg_agent_round.h"
#endif

#ifndef CONFIG_VG_HMI_REPORT_DIR
#  define CONFIG_VG_HMI_REPORT_DIR "/data/velaguard/reports"
#endif

#define VG_ADV_FILE        CONFIG_VG_HMI_REPORT_DIR "/alarm_advice.txt"

/* One round costs about two minutes on this board, so asking again on every
 * alarm-set change would just queue behind the answer already in flight. */

#define VG_ADV_MIN_GAP_MS  20000u    /* between two rounds */
#define VG_ADV_REFRESH_MS  300000u   /* re-ask for a long-running alarm set */
#define VG_ADV_RETRY_MS    300000u   /* after a round that produced no usable
                                      * document: the same alarm set must not
                                      * buy a fresh LLM round every 20 s, an
                                      * alarm set that changed still may.
                                      * A round takes about 195 s, so a short
                                      * retry would keep this single channel
                                      * busy almost continuously and starve
                                      * the daily report. */

#define VG_ADV_SIG_MAX     192
#define VG_ADV_REQ_MAX     1536

#define TAG "vgadvice"

static pthread_mutex_t    g_lock = PTHREAD_MUTEX_INITIALIZER;
static vg_ai_advice_doc_t g_doc;
static bool               g_loaded;
static vg_ui_advice_state_t g_state = VG_UI_ADV_IDLE;
static char               g_sig[VG_ADV_SIG_MAX];
static uint32_t           g_boot;
static uint32_t           g_req;
static uint32_t           g_last_ask_ms;
static uint32_t           g_last_ok_ms;
static uint32_t           g_next_ask_ms;
static bool               g_asked_once;

/* Buffers for the file read and the parse live here rather than on the file
 * worker's stack: the document alone is about 4 KB and that stack is 8 KB. */

static char               s_raw[VG_AI_DOC_MAX + 1];
static vg_ai_advice_doc_t s_parsed;

/* Snapshot of the active alarms, published by the UI thread and consumed by
 * the file worker.  Strings are copied: the pointer-returning model helpers
 * hand out addresses into live state that the next acquisition overwrites. */

#define VG_ADV_NAME_MAX 32

typedef struct
{
  char     id[VG_AI_ID_MAX];
  char     name[VG_ADV_NAME_MAX];
  uint32_t epoch;
  uint8_t  sev;
  float    value;
  float    threshold;
  int32_t  dur_s;
} vg_advice_alarm_t;

static pthread_mutex_t   g_snap_lock = PTHREAD_MUTEX_INITIALIZER;
static vg_advice_alarm_t g_snap[VG_AI_ADV_MAX];
static int               g_snap_n;

void vg_advice_note_alarms(void)
{
#ifdef CONFIG_EXAMPLES_AI_AGENT_VELA
  vg_alarm_t list[VG_AI_ADV_MAX];
  vg_advice_alarm_t snap[VG_AI_ADV_MAX];
  int n;
  int i;

  n = vg_model_collect_alarms(list, VG_AI_ADV_MAX);
  memset(snap, 0, sizeof(snap));

  for (i = 0; i < n; i++)
    {
      const vg_sensor_t *s = vg_model_get_sensor(list[i].sensor_id);

      const char *nm = (s != NULL) ? s->name : list[i].sensor_id;

      snprintf(snap[i].id, sizeof(snap[i].id), "%s", list[i].sensor_id);
      strncpy(snap[i].name, nm, sizeof(snap[i].name) - 1);
      snap[i].name[sizeof(snap[i].name) - 1] = '\0';
      snap[i].epoch = (s != NULL) ? s->al_epoch : 0;
      snap[i].sev = (list[i].severity == VG_SEV_CRIT)   ? VG_AI_SEV_CRIT
                  : (list[i].severity == VG_SEV_OFFLINE) ? VG_AI_SEV_OFFLINE
                                                         : VG_AI_SEV_WARN;
      snap[i].value = list[i].value;
      snap[i].threshold = list[i].threshold;
      snap[i].dur_s = list[i].duration_sec;
    }

  pthread_mutex_lock(&g_snap_lock);
  memcpy(g_snap, snap, sizeof(snap));
  g_snap_n = n;
  pthread_mutex_unlock(&g_snap_lock);
#endif
}

static void advice_set_state(vg_ui_advice_state_t st)
{
  pthread_mutex_lock(&g_lock);
  g_state = st;
  pthread_mutex_unlock(&g_lock);
}

/* Only has to differ between boots; the epoch inside the document then
 * separates alarm episodes within one boot. */

static uint32_t advice_boot_nonce(void)
{
  uint32_t n;

  if (g_boot != 0)
    {
      return g_boot;
    }

  n = (uint32_t)time(NULL) ^ vg_agent_round_now_ms() ^ 0x5a17c0deu;
  if (n == 0)
    {
      n = 0x5a17c0deu;
    }

  g_boot = n;
  return n;
}

static void build_sig(char *out, size_t cap, const vg_advice_alarm_t *list,
                      int n)
{
  size_t used = 0;
  int i;

  out[0] = '\0';

  for (i = 0; i < n; i++)
    {
      int w;

      w = snprintf(out + used, cap - used, "%s:%u:%d,",
                   list[i].id, (unsigned)list[i].epoch, (int)list[i].sev);
      if (w < 0 || (size_t)w >= cap - used)
        {
          break;
        }

      used += (size_t)w;
    }
}

/* Read and validate what the agent wrote for the round that just finished. */

static void advice_load(void)
{
  uint32_t boot;
  uint32_t req;
  int fd;
  ssize_t got;
  int rc;

  fd = open(VG_ADV_FILE, O_RDONLY);
  if (fd < 0)
    {
      syslog(LOG_WARNING, "[%s] no advice file (%d)\n", TAG, errno);
      advice_set_state(VG_UI_ADV_ERROR);
      return;
    }

  got = read(fd, s_raw, sizeof(s_raw) - 1);
  close(fd);

  if (got <= 0)
    {
      advice_set_state(VG_UI_ADV_ERROR);
      return;
    }

  s_raw[got] = '\0';

  pthread_mutex_lock(&g_lock);
  boot = g_boot;
  req = g_req;
  pthread_mutex_unlock(&g_lock);

  rc = vg_ai_advice_parse(s_raw, (size_t)got, boot, req, &s_parsed);
  if (rc != VG_AI_OK)
    {
      /* Rejected whole: the page keeps showing the rule summary, and the
       * rejection is recorded rather than half-applied. */

      syslog(LOG_WARNING, "[%s] advice rejected rc=%d (%d bytes)\n",
             TAG, rc, (int)got);
      advice_set_state(VG_UI_ADV_ERROR);
      return;
    }

  pthread_mutex_lock(&g_lock);
  g_doc = s_parsed;
  g_loaded = true;
  g_state = VG_UI_ADV_READY;
  g_last_ok_ms = vg_agent_round_now_ms();
  g_next_ask_ms = 0;
  pthread_mutex_unlock(&g_lock);

  syslog(LOG_INFO, "[%s] advice loaded: %d entries\n", TAG, s_parsed.n);
}

static void advice_drop(void)
{
  pthread_mutex_lock(&g_lock);

  if (g_loaded || g_state != VG_UI_ADV_IDLE)
    {
      g_loaded = false;
      g_state = VG_UI_ADV_IDLE;
      g_sig[0] = '\0';
      g_asked_once = false;
      g_next_ask_ms = 0;
    }

  pthread_mutex_unlock(&g_lock);
}

void vg_advice_tick(void)
{
#ifdef CONFIG_EXAMPLES_AI_AGENT_VELA
  static char req[VG_ADV_REQ_MAX];
  static vg_advice_alarm_t snap[VG_AI_ADV_MAX];
  vg_ai_alarm_in_t in[VG_AI_ADV_MAX];
  char sig[VG_ADV_SIG_MAX];
  uint32_t now = vg_agent_round_now_ms();
  enum vg_agent_round_state rnd;
  bool changed;
  bool refresh;
  bool due;
  int n;
  int i;
  int len;
  int rc;

  pthread_mutex_lock(&g_snap_lock);
  memcpy(snap, g_snap, sizeof(snap));
  n = g_snap_n;
  pthread_mutex_unlock(&g_snap_lock);

  if (n <= 0)
    {
      /* Release our own finished round first: the queue refuses to start a
       * new round while any result is still unconsumed. */

      if (vg_agent_round_owner() == VG_AGENT_ROUND_OWNER_ADVICE)
        {
          enum vg_agent_round_state rst = vg_agent_round_state();

          if (rst == VG_AGENT_ROUND_DONE || rst == VG_AGENT_ROUND_ERROR)
            {
              vg_agent_round_clear();
            }
        }

      advice_drop();
      return;
    }

  build_sig(sig, sizeof(sig), snap, n);

  /* A round is in flight: nothing to do but wait. */

  if (vg_agent_round_busy())
    {
      advice_set_state(VG_UI_ADV_PENDING);
      return;
    }

  /* A finished round nobody claimed - a manual one whose process went away,
   * most likely - would hold the channel forever now that the queue refuses
   * to start a round while a terminal state is present. */

  vg_agent_round_reclaim(VG_AGENT_ROUND_OWNER_ADVICE,
                         VG_AGENT_ROUND_RECLAIM_MS);

  /* A round just ended (or failed).  Only a round this flow asked for says
   * anything about alarm_advice.txt: the daily flow shares the channel and
   * its reply is a different file.  A round owned by the other flow is left
   * alone here, and this tick carries on to the asking logic below. */

  rnd = vg_agent_round_state();
  if ((rnd == VG_AGENT_ROUND_DONE || rnd == VG_AGENT_ROUND_ERROR) &&
      vg_agent_round_owner() == VG_AGENT_ROUND_OWNER_ADVICE)
    {
      if (rnd == VG_AGENT_ROUND_DONE)
        {
          advice_load();
        }
      else
        {
          advice_set_state(VG_UI_ADV_ERROR);
        }

      /* The round ended without a document the parser accepted.  Hold the
       * same alarm set off for a while: a rejected or missing file is not
       * going to fix itself in the next 20 s, and each retry costs a full
       * LLM round. */

      pthread_mutex_lock(&g_lock);
      if (!g_loaded)
        {
          g_next_ask_ms = now + VG_ADV_RETRY_MS;
        }
      pthread_mutex_unlock(&g_lock);

      vg_agent_round_clear();
    }

  /* Read the gate after the round bookkeeping above, so a backoff armed by
   * the round that just ended is already in effect for this tick. */

  pthread_mutex_lock(&g_lock);
  changed = (strcmp(sig, g_sig) != 0);
  refresh = g_loaded && g_last_ok_ms != 0 &&
            now - g_last_ok_ms >= VG_ADV_REFRESH_MS;
  due = changed || g_next_ask_ms == 0 ||
        (int32_t)(now - g_next_ask_ms) >= 0;
  pthread_mutex_unlock(&g_lock);

  if (!changed && !refresh && g_loaded)
    {
      return;
    }

  if (!due)
    {
      return;
    }

  if (g_asked_once && now - g_last_ask_ms < VG_ADV_MIN_GAP_MS)
    {
      return;
    }

  memset(in, 0, sizeof(in));
  for (i = 0; i < n; i++)
    {
      in[i].id = snap[i].id;
      in[i].name = snap[i].name;
      in[i].epoch = snap[i].epoch;
      in[i].sev = snap[i].sev;
      in[i].value = snap[i].value;
      in[i].threshold = snap[i].threshold;
      in[i].dur_s = snap[i].dur_s;
    }

  pthread_mutex_lock(&g_lock);
  g_req++;
  len = vg_ai_advice_build_request(req, sizeof(req), advice_boot_nonce(),
                                   g_req, in, n);
  if (len > 0)
    {
      memcpy(g_sig, sig, sizeof(g_sig));
      g_sig[sizeof(g_sig) - 1] = '\0';
    }
  pthread_mutex_unlock(&g_lock);

  if (len <= 0)
    {
      syslog(LOG_WARNING, "[%s] request build failed: %d\n", TAG, len);
      advice_set_state(VG_UI_ADV_ERROR);
      return;
    }

  rc = vg_agent_round_queue_owned(req, VG_AGENT_ROUND_OWNER_ADVICE);
  if (rc != 0)
    {
      if (rc != -EBUSY)
        {
          /* Delivery failed, which means the agent is not draining its bus.
           * Without a backoff here this would retry every VG_ADV_MIN_GAP_MS
           * and fill that 16-deep queue by itself, so take the same pause as
           * a round that produced nothing usable. */

          pthread_mutex_lock(&g_lock);
          g_next_ask_ms = now + VG_ADV_RETRY_MS;
          g_asked_once = true;
          g_last_ask_ms = now;
          pthread_mutex_unlock(&g_lock);
        }

      /* -EBUSY is the other flow holding the channel for one tick; asking
       * again on the next tick is the right move, not a five minute pause. */

      advice_set_state(VG_UI_ADV_ERROR);
      return;
    }

  pthread_mutex_lock(&g_lock);
  g_last_ask_ms = now;
  g_next_ask_ms = 0;
  g_asked_once = true;
  pthread_mutex_unlock(&g_lock);

  advice_set_state(VG_UI_ADV_PENDING);

  syslog(LOG_INFO, "[%s] asked for %d alarms\n", TAG, n);
#else
  /* HMI without the agent: the page keeps showing the rule summary. */

  advice_drop();
#endif
}

/****************************************************************************
 * Public page-facing API
 ****************************************************************************/

void vg_ui_alarm_advice_request(void)
{
  /* Force the next tick to consider asking again. */

  pthread_mutex_lock(&g_lock);
  g_asked_once = false;
  g_last_ask_ms = 0;
  g_next_ask_ms = 0;
  pthread_mutex_unlock(&g_lock);
}

vg_ui_advice_state_t vg_ui_alarm_advice_state(void)
{
  vg_ui_advice_state_t st;

  pthread_mutex_lock(&g_lock);
  st = g_state;
  pthread_mutex_unlock(&g_lock);
  return st;
}

bool vg_ui_alarm_advice_get(const char *sensor_id, uint32_t al_epoch,
                            vg_ai_advice_entry_t *out)
{
  const vg_ai_advice_entry_t *e = NULL;

  if (sensor_id == NULL || out == NULL)
    {
      return false;
    }

  pthread_mutex_lock(&g_lock);

  if (g_loaded && g_state == VG_UI_ADV_READY)
    {
      e = vg_ai_advice_find(&g_doc, sensor_id, al_epoch);
      if (e != NULL)
        {
          *out = *e;
        }
    }

  pthread_mutex_unlock(&g_lock);

  return (e != NULL);
}

#endif /* CONFIG_VG_HMI */
