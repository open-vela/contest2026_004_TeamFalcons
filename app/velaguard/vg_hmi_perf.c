/****************************************************************************
 * app/velaguard/vg_hmi_perf.c
 *
 * Implementation of the bounded HMI performance counters. Compiled only
 * into explicit measurement builds (CONFIG_VG_HMI_PERF / -DVG_HMI_PERF_
 * ENABLED); the header provides no-op inlines for every other build.
 *
 * Hot path guarantees (B-AC2): record functions do not malloc, printf,
 * sort, touch files or block. The writer takes the state mutex with
 * trylock and drops the single conflicting sample into the atomic dropped
 * counter; the diagnostic reader holds the mutex only for the struct copy
 * and computes percentiles/format outside of it.
 *
 * Static storage: one state struct, about 6.4 KiB for six 256-sample
 * windows plus counters — within the 8 KiB budget from design.md. Verify
 * against the link map (symbols vg_perf_state / vg_perf_stats).
 ****************************************************************************/

/* No nuttx/config.h include on purpose: the enable flag is passed as
 * -DVG_HMI_PERF_ENABLED=1 by the firmware Makefile (under CONFIG_VG_HMI_PERF)
 * and by the host test, so this file compiles for both targets unchanged. */

#ifndef VG_HMI_PERF_ENABLED
#error "vg_hmi_perf.c must be compiled with -DVG_HMI_PERF_ENABLED=1"
#endif

#include <pthread.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>

#include "vg_hmi_perf.h"

typedef struct
{
  bool pending_valid;
  uint32_t pending_start_us;
} vg_perf_pending_t;

typedef struct
{
  bool published;
  bool hmi_running;
  uint32_t init_us;
  uint32_t clock_res_us;
  uint32_t granularity_us;
  vg_perf_metric_stats_t m[VG_PERF_METRIC_N];
  vg_perf_pending_t pending[VG_PERF_METRIC_N];
  vg_perf_resources_t res;
  pthread_mutex_t lock;
} vg_perf_state_t;

/* BSS only: no runtime allocation anywhere in this module. */
static vg_perf_state_t g_perf =
{
  .lock = PTHREAD_MUTEX_INITIALIZER
};

/* Writer-side conflict counter; atomic so the writer never needs the mutex
 * just to record a drop. */
static uint32_t g_dropped;

static uint32_t (*g_time_fn)(void);

#define VG_PERF_US_PER_MS 1000u

static uint32_t perf_now(void)
{
  if(g_time_fn != NULL)
    {
      return g_time_fn();
    }

  struct timespec ts;
  if(clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
    {
      return 0;
    }

  /* Truncate to uint32_t: ~71.6 min wraparound, handled below with
   * unsigned subtraction. Durations are always far below the wrap. */
  return (uint32_t)((uint64_t)ts.tv_sec * 1000000u +
                    (uint32_t)ts.tv_nsec / 1000u);
}

void vg_hmi_perf_set_time_source(uint32_t (*fn)(void))
{
  g_time_fn = fn;
}

static void perf_probe_clock(void)
{
  struct timespec res;
  uint32_t min_delta = UINT32_MAX;
  uint32_t prev;
  int i;

  g_perf.clock_res_us = 0;
  if(clock_getres(CLOCK_MONOTONIC, &res) == 0)
    {
      g_perf.clock_res_us = (uint32_t)(res.tv_sec * 1000000u +
                                       res.tv_nsec / 1000u);
    }

  /* Observed granularity: minimum positive delta over consecutive reads.
   * Zero deltas are skipped; when every read is instant (ns-resolution
   * host clock) fall back to the advertised resolution. */
  prev = perf_now();
  for(i = 0; i < 512 && min_delta == UINT32_MAX; i++)
    {
      uint32_t now = perf_now();
      uint32_t d = now - prev; /* unsigned: wraparound-safe */
      if(d > 0 && d < min_delta)
        {
          min_delta = d;
        }
      prev = now;
    }

  g_perf.granularity_us = (min_delta == UINT32_MAX) ? g_perf.clock_res_us
                                                    : min_delta;
}

void vg_hmi_perf_init(void)
{
  memset(&g_perf.m, 0, sizeof(g_perf.m));
  memset(&g_perf.pending, 0, sizeof(g_perf.pending));
  memset(&g_perf.res, 0, sizeof(g_perf.res));
  g_perf.hmi_running = false;
  g_perf.init_us = perf_now();
  perf_probe_clock();
  g_perf.published = true;
}

uint32_t vg_hmi_perf_now_us(void)
{
  return perf_now();
}

static void perf_record(vg_perf_metric_stats_t * m, uint32_t dur_us)
{
  if(pthread_mutex_trylock(&g_perf.lock) != 0)
    {
      /* Reader owns the lock; never block the UI thread for a sample. */
      __atomic_fetch_add(&g_dropped, 1, __ATOMIC_RELAXED);
      return;
    }

  m->total_n++;
  m->total_us += dur_us;
  if(dur_us > m->max_us)
    {
      m->max_us = dur_us;
    }

  m->win[m->win_head] = dur_us;
  m->win_head = (uint16_t)((m->win_head + 1) % VG_PERF_WINDOW);
  if(m->win_n < VG_PERF_WINDOW)
    {
      m->win_n++;
    }

  pthread_mutex_unlock(&g_perf.lock);
}

void vg_hmi_perf_begin(vg_perf_metric_t m)
{
  if(m >= VG_PERF_METRIC_N)
    {
      return;
    }

  vg_perf_pending_t * p = &g_perf.pending[m];
  if(p->pending_valid)
    {
      /* A begin without its end: count it, never fake a duration. */
      __atomic_fetch_add(&g_perf.m[m].incomplete_n, 1, __ATOMIC_RELAXED);
    }

  p->pending_start_us = perf_now();
  p->pending_valid = true;
}

void vg_hmi_perf_end(vg_perf_metric_t m)
{
  vg_perf_pending_t * p;
  uint32_t now;

  if(m >= VG_PERF_METRIC_N)
    {
      return;
    }

  p = &g_perf.pending[m];
  if(!p->pending_valid)
    {
      /* Stray end (e.g. enable boundary): ignore, no fake sample. */
      return;
    }

  now = perf_now();
  p->pending_valid = false;
  perf_record(&g_perf.m[m], now - p->pending_start_us);

  if(m == VG_PERF_FLUSH)
    {
      /* Submit bookkeeping rides on the completed flush sample. Every
       * writer of res.* is the HMI thread (this callback and the 1 Hz
       * resource timer), so no mutex here: the reader's copy may be one
       * update stale but never torn. */
      g_perf.res.commits++;
      g_perf.res.last_commit_ms = (now - g_perf.init_us) / VG_PERF_US_PER_MS;
    }
}

void vg_hmi_perf_span(vg_perf_metric_t m, uint32_t start_us)
{
  if(m >= VG_PERF_METRIC_N)
    {
      return;
    }

  perf_record(&g_perf.m[m], perf_now() - start_us);
}

void vg_hmi_perf_set_running(bool running)
{
  g_perf.hmi_running = running;
}

void vg_hmi_perf_publish_resources(const vg_perf_resources_t * res)
{
  uint32_t commits;
  uint32_t last_commit_ms;

  if(res == NULL)
    {
      return;
    }

  commits = g_perf.res.commits;
  last_commit_ms = g_perf.res.last_commit_ms;

  pthread_mutex_lock(&g_perf.lock);
  g_perf.res = *res;
  /* commits / last_commit_ms belong to the flush path, not the 1 Hz
   * resource snapshot; keep the values it last wrote. */
  g_perf.res.commits = commits;
  g_perf.res.last_commit_ms = last_commit_ms;
  g_perf.res.valid = true;
  pthread_mutex_unlock(&g_perf.lock);
}

bool vg_hmi_perf_read(vg_perf_report_t * out)
{
  if(out == NULL || !g_perf.published)
    {
      return false;
    }

  pthread_mutex_lock(&g_perf.lock);
  memcpy(out->m, g_perf.m, sizeof(out->m));
  out->res = g_perf.res;
  pthread_mutex_unlock(&g_perf.lock);

  out->published = true;
  out->hmi_running = g_perf.hmi_running;
  out->uptime_ms = (perf_now() - g_perf.init_us) / VG_PERF_US_PER_MS;
  out->clock_res_us = g_perf.clock_res_us;
  out->granularity_us = g_perf.granularity_us;
  out->dropped_n = __atomic_load_n(&g_dropped, __ATOMIC_RELAXED);
  return true;
}

uint32_t vg_hmi_perf_budget_bytes(void)
{
  return (uint32_t)sizeof(g_perf);
}

static int cmp_u32(const void * a, const void * b)
{
  uint32_t va = *(const uint32_t *)a;
  uint32_t vb = *(const uint32_t *)b;
  return (va > vb) - (va < vb);
}

uint32_t vg_hmi_perf_nearest_rank(uint32_t * samples, uint16_t n, uint8_t pct)
{
  uint32_t k;

  if(samples == NULL || n == 0)
    {
      return 0;
    }

  qsort(samples, n, sizeof(uint32_t), cmp_u32);

  /* nearest-rank: ascending index ceil(pct/100 * n), 1-based */
  k = ((uint32_t)pct * (uint32_t)n + 99u) / 100u;
  if(k < 1)
    {
      k = 1;
    }
  if(k > n)
    {
      k = n;
    }
  return samples[k - 1];
}
