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
 *
 * What the page reads is governed by vg_advice_policy: an entry is shown when
 * it matches the alarm episode on screen, and the outcome of the last round
 * is not part of that test.  A failed refresh therefore leaves the advice for
 * the current alarm set in place instead of blanking the page, and only an
 * alarm set with nothing behind it is reported as unavailable.
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
#include "vg_advice_policy.h"

#ifdef CONFIG_EXAMPLES_AI_AGENT_VELA
#  include "vg_agent_round.h"
#  include "vg_provision.h"
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
static char               g_sig[VG_ADV_SIG_MAX];      /* set last asked about */
static char               g_backoff_sig[VG_ADV_SIG_MAX]; /* set the pause is for */
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

/* Single place where the page-visible state is derived, so the three paths
 * that end a tick cannot disagree about it.  See vg_advice_presence(): advice
 * that covers the alarm set on screen keeps the page on READY even while a
 * round is in flight. */

static void advice_publish_state(bool covered, bool round_in_flight)
{
  switch (vg_advice_presence(covered, round_in_flight,
                             vg_llm_credentials_ready()))
    {
      case VG_ADV_PRESENCE_SHOW:
        advice_set_state(VG_UI_ADV_READY);
        break;

      case VG_ADV_PRESENCE_WAITING:
        advice_set_state(VG_UI_ADV_PENDING);
        break;

      case VG_ADV_PRESENCE_NO_CRED:
        advice_set_state(VG_UI_ADV_NO_CRED);
        break;

      default:
        advice_set_state(VG_UI_ADV_ERROR);
        break;
    }
}

/* Does the loaded document have an entry for every alarm in this snapshot? */

static bool advice_covered(const vg_advice_alarm_t *list, int n)
{
  vg_advice_episode_t set[VG_AI_ADV_MAX];
  bool covered;
  int i;

  if (n <= 0)
    {
      return false;
    }

  for (i = 0; i < n && i < VG_AI_ADV_MAX; i++)
    {
      set[i].id = list[i].id;
      set[i].epoch = list[i].epoch;
    }

  pthread_mutex_lock(&g_lock);
  covered = vg_advice_doc_covers_set(g_loaded, &g_doc, set, (n < VG_AI_ADV_MAX)
                                                             ? n
                                                             : VG_AI_ADV_MAX);
  pthread_mutex_unlock(&g_lock);

  return covered;
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

/* Read and validate what the agent wrote.
 *
 * Accepted on the boot stamp alone.  A round that timed out while the model
 * was still working leaves its file behind one round later, and that file is
 * still the right advice for the alarms it names: the entries carry (id,
 * epoch), and coverage is checked against the live alarm set afterwards.  The
 * req is reported for diagnosis but is not a gate -- gating on it is what
 * rejected a document that matched the screen exactly, leaving the page on
 * the rule summary with usable advice sitting on eMMC.
 *
 * Returns true when a document passed the parser. */

static bool advice_load(void)
{
  uint32_t boot;
  uint32_t req;
  uint32_t doc_boot;
  uint32_t doc_req;
  int fd;
  ssize_t got;
  int rc;

  pthread_mutex_lock(&g_lock);
  boot = g_boot;
  req = g_req;
  pthread_mutex_unlock(&g_lock);

  fd = open(VG_ADV_FILE, O_RDONLY);
  if (fd < 0)
    {
      syslog(LOG_WARNING, "[%s] no advice file (%d) boot=%08x req=%u\n",
             TAG, errno, (unsigned)boot, (unsigned)req);
      advice_set_state(VG_UI_ADV_ERROR);
      return false;
    }

  got = read(fd, s_raw, sizeof(s_raw) - 1);
  close(fd);

  if (got <= 0)
    {
      syslog(LOG_WARNING, "[%s] advice file empty (%d) req=%u\n",
             TAG, (int)got, (unsigned)req);
      advice_set_state(VG_UI_ADV_ERROR);
      return false;
    }

  s_raw[got] = '\0';

  doc_boot = 0;
  doc_req = 0;
  rc = vg_ai_advice_head(s_raw, (size_t)got, &doc_boot, &doc_req);
  if (rc != VG_AI_OK)
    {
      syslog(LOG_WARNING, "[%s] advice header rejected rc=%d (%d bytes)\n",
             TAG, rc, (int)got);
      advice_set_state(VG_UI_ADV_ERROR);
      return false;
    }

  if (doc_boot != boot)
    {
      /* Another boot's document.  Its epochs describe alarms that no longer
       * exist, so it must not be adopted. */

      syslog(LOG_WARNING,
             "[%s] advice from another boot: file=%08x board=%08x\n",
             TAG, (unsigned)doc_boot, (unsigned)boot);
      advice_set_state(VG_UI_ADV_ERROR);
      return false;
    }

  if (doc_req != req)
    {
      syslog(LOG_INFO,
             "[%s] advice file answers req=%u, live round is req=%u: "
             "coverage decides\n", TAG, (unsigned)doc_req, (unsigned)req);
    }

  /* parse with the document's own identity so the stale test cannot reject a
   * document the coverage rule below would have accepted. */

  rc = vg_ai_advice_parse(s_raw, (size_t)got, doc_boot, doc_req, &s_parsed);
  if (rc != VG_AI_OK)
    {
      /* Rejected whole: the page keeps showing the rule summary, and the
       * rejection is recorded rather than half-applied. */

      syslog(LOG_WARNING,
             "[%s] advice rejected rc=%d (%d bytes) file_req=%u\n",
             TAG, rc, (int)got, (unsigned)doc_req);
      advice_set_state(VG_UI_ADV_ERROR);
      return false;
    }

  pthread_mutex_lock(&g_lock);
  g_doc = s_parsed;
  g_loaded = true;
  g_state = VG_UI_ADV_READY;
  g_last_ok_ms = vg_agent_round_now_ms();
  g_next_ask_ms = 0;
  g_backoff_sig[0] = '\0';
  pthread_mutex_unlock(&g_lock);

  syslog(LOG_INFO, "[%s] advice loaded: %d entries file_req=%u live_req=%u\n",
         TAG, s_parsed.n, (unsigned)doc_req, (unsigned)req);
  return true;
}

static void advice_drop(void)
{
  pthread_mutex_lock(&g_lock);

  if (g_loaded || g_state != VG_UI_ADV_IDLE)
    {
      g_loaded = false;
      g_state = VG_UI_ADV_IDLE;
      g_sig[0] = '\0';
      g_backoff_sig[0] = '\0';
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
  vg_advice_gate_t gate;
  uint32_t now = vg_agent_round_now_ms();
  enum vg_agent_round_state rnd;
  bool covered;
  uint32_t req_no;
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

  /* Computed once and reused below: coverage is what the page is told, and it
   * is a handful of string compares over at most eight alarms. */

  covered = advice_covered(snap, n);

  /* A round is in flight -- ours, or the daily flow's, since the two share
   * this channel.  Nothing to do but wait, and the page only needs to hear
   * about it when there is nothing behind the alarm set on screen: advice
   * that already answers this set must not be labelled as still generating. */

  if (vg_agent_round_busy())
    {
      advice_publish_state(covered, true);
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
      char asked_sig[VG_ADV_SIG_MAX];

      /* Load on either outcome: a round that ended in ERROR can still have
       * left a perfectly good document on eMMC (a write that landed, then a
       * reply the agent never delivered).  What the file says, not what the
       * round reported, decides whether the page has something to show. */

      (void)advice_load();

      /* The set this round was about.  Still g_sig: it is only replaced when
       * a later request is built. */

      pthread_mutex_lock(&g_lock);
      memcpy(asked_sig, g_sig, sizeof(asked_sig));
      pthread_mutex_unlock(&g_lock);

      /* advice_load() may have just replaced the document, so coverage has to
       * be read again here; the value computed above describes the previous
       * one.  Coverage, not the round's outcome, is what the page is told,
       * and this also covers a document that parsed but names other episodes:
       * it stays loaded while the page is told it has nothing for this set. */

      covered = advice_covered(snap, n);
      if (vg_advice_after_round(covered) == VG_ADV_AFTER_ROUND_READY)
        {
          advice_publish_state(true, false);

          syslog(LOG_INFO,
                 "[%s] round ended, advice for this alarm set kept\n", TAG);
        }
      else
        {
          advice_publish_state(false, false);

          /* Pause this set rather than this flow: a rejected or missing file
           * will not fix itself in the next 20 s, and a retry costs a full
           * LLM round.  Keyed to the set the round was about, so an alarm set
           * that changed while the round ran still gets asked at once. */

          pthread_mutex_lock(&g_lock);
          g_next_ask_ms = now + VG_ADV_RETRY_MS;
          memcpy(g_backoff_sig, asked_sig, sizeof(g_backoff_sig));
          g_backoff_sig[sizeof(g_backoff_sig) - 1] = '\0';
          pthread_mutex_unlock(&g_lock);
        }

      vg_agent_round_clear();
    }

  /* Read the gate after the round bookkeeping above, so a backoff armed by
   * the round that just ended is already in effect for this tick.  `covered`
   * is up to date either way: it was read after the load in the round-end
   * block, or nothing has touched the document since it was read earlier. */

  memset(&gate, 0, sizeof(gate));
  gate.covered = covered;
  gate.min_gap_ms = VG_ADV_MIN_GAP_MS;

  pthread_mutex_lock(&g_lock);
  gate.backoff_armed = (g_next_ask_ms != 0);
  gate.backoff_expired = (g_next_ask_ms == 0) ||
                         ((int32_t)(now - g_next_ask_ms) >= 0);
  gate.backoff_same_set = (strcmp(g_backoff_sig, sig) == 0);
  /* Signed difference, as elsewhere in this file: advice_load() runs during
   * this tick and stamps g_last_ok_ms with a later reading than `now`, so the
   * unsigned form would wrap to a huge value and read as "aged out", asking
   * for a fresh round on every single tick. */

  gate.refresh_due = vg_advice_refresh_due(g_loaded, now, g_last_ok_ms,
                                           VG_ADV_REFRESH_MS);
  gate.asked_once = g_asked_once;
  gate.since_last_ask_ms = now - g_last_ask_ms;
  pthread_mutex_unlock(&g_lock);

  if (vg_advice_decide(&gate) != VG_ADV_ASK)
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
  req_no = g_req;
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
      /* The alarm set is too large for the request budget, so this set never
       * gets a round from here.  An earlier document that still answers it
       * stays on screen. */

      syslog(LOG_WARNING, "[%s] request build failed: %d req=%u\n",
             TAG, len, (unsigned)req_no);

      advice_publish_state(covered, false);
      return;
    }

  rc = vg_agent_round_queue_owned(req, VG_AGENT_ROUND_OWNER_ADVICE);
  if (rc != 0)
    {
      if (vg_advice_queue_is_transient(rc))
        {
          /* The other flow holds the single channel this tick.  Nothing is
           * wrong with the advice, so the page keeps whatever it has and the
           * next tick asks again: reporting a failure here would blink the
           * page to "unavailable" every time the daily report is in flight. */

          advice_publish_state(covered, true);

          syslog(LOG_INFO, "[%s] channel busy, retry next tick\n", TAG);
          return;
        }

      /* Delivery failed, which means the agent is not draining its bus.
       * Without a backoff here this would retry every VG_ADV_MIN_GAP_MS and
       * fill that 16-deep queue by itself, so take the same pause as a round
       * that produced nothing usable.  Coverage still decides the state: an
       * alarm set whose advice is already on screen keeps it. */

      syslog(LOG_WARNING, "[%s] ask not delivered: %d req=%u\n",
             TAG, rc, (unsigned)req_no);

      pthread_mutex_lock(&g_lock);
      g_next_ask_ms = now + VG_ADV_RETRY_MS;
      memcpy(g_backoff_sig, sig, sizeof(g_backoff_sig));
      g_backoff_sig[sizeof(g_backoff_sig) - 1] = '\0';
      g_asked_once = true;
      g_last_ask_ms = now;
      pthread_mutex_unlock(&g_lock);

      advice_publish_state(covered, false);
      return;
    }

  pthread_mutex_lock(&g_lock);
  g_last_ask_ms = now;
  g_next_ask_ms = 0;
  g_backoff_sig[0] = '\0';
  g_asked_once = true;
  pthread_mutex_unlock(&g_lock);

  /* A refresh is now in flight, but if the advice on hand still answers this
   * alarm set the page keeps showing it, so the state stays READY: the round
   * is only worth reporting as pending when there is nothing to show yet. */

  advice_publish_state(covered, true);

  syslog(LOG_INFO, "[%s] asked for %d alarms req=%u\n", TAG, n,
         (unsigned)req_no);
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
  bool hit;

  if (sensor_id == NULL || out == NULL)
    {
      return false;
    }

  pthread_mutex_lock(&g_lock);
  hit = vg_advice_lookup(g_loaded, &g_doc, sensor_id, al_epoch, out);
  pthread_mutex_unlock(&g_lock);

  return hit;
}

/* Acceptance probe.  Prints the same inputs the page uses, so a bench run can
 * assert on the serial console instead of reading the LCD: the state field is
 * only the heading the page picks when nothing matches, while advice=hit is
 * what actually puts AI text on screen. */

