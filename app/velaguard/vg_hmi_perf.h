/****************************************************************************
 * app/velaguard/vg_hmi_perf.h
 *
 * Bounded HMI performance counters (task 09-13-hmi-performance-baseline).
 * The six-metric set is fixed on purpose; this is not a generic tracing
 * framework. Without VG_HMI_PERF_ENABLED every call compiles to nothing
 * and no sample storage exists, so default builds keep their exact
 * business behavior and footprint.
 *
 * Threading: record calls run on the HMI thread only. vg_hmi_perf_read()
 * may be called from any thread (NSH diagnostics). The writer never blocks
 * on the reader: a conflicting read drops that one sample into the dropped
 * counter instead of stalling the UI.
 *
 * Time: monotonic microseconds truncated to uint32_t; wraparound uses
 * unsigned subtraction. Reported values are microseconds, but the actual
 * observable granularity is reported separately and must not be confused
 * with the unit.
 *
 * Storage budget: one static state struct (< 8 KiB, see
 * vg_hmi_perf_budget_bytes()); verified against the link map.
 ****************************************************************************/

#ifndef VG_HMI_PERF_H
#define VG_HMI_PERF_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
  VG_PERF_LOOP = 0,   /* one lv_timer_handler() round, excluding the sleep after it */
  VG_PERF_INPUT_READ, /* wrapped raw indev read callback (sampling cost, not touch latency) */
  VG_PERF_MODEL_TICK, /* vg_model_tick() with its current call chain */
  VG_PERF_RENDER,     /* LV_EVENT_RENDER_START .. LV_EVENT_RENDER_READY (software draw) */
  VG_PERF_FLUSH,      /* LV_EVENT_FLUSH_START .. LV_EVENT_FLUSH_FINISH (driver cb, not panel scan) */
  VG_PERF_NAV,        /* navigation request into page build return */
  VG_PERF_METRIC_N
} vg_perf_metric_t;

#define VG_PERF_WINDOW 256

typedef struct
{
  uint32_t total_n;      /* completed samples since init */
  uint32_t max_us;
  uint32_t incomplete_n; /* begin without a paired end; no zero-duration fakes */
  uint16_t win_n;        /* valid samples in the recent window (<= VG_PERF_WINDOW) */
  uint16_t win_head;     /* next window write slot */
  uint64_t total_us;
  uint32_t win[VG_PERF_WINDOW];
} vg_perf_metric_stats_t;

typedef struct
{
  bool valid;              /* resource fields published at least once (1 Hz HMI snapshot) */
  int32_t page_id;
  uint32_t page_objects;   /* current page subtree object count */
  uint32_t model_listeners;
  uint32_t timers_total;   /* public lv_timer enumeration (core + shell + page timers) */
  uint32_t shell_timers;   /* shell-owned timers (clock / model tick / toast) */
  uint32_t commits;        /* flush submits completed */
  uint32_t last_commit_ms; /* monotonic ms since perf init at last submit */
} vg_perf_resources_t;

typedef struct
{
  bool published;
  bool hmi_running;
  uint32_t uptime_ms;
  uint32_t clock_res_us;    /* clock_getres(CLOCK_MONOTONIC) */
  uint32_t granularity_us;  /* observed minimum delta at init; never claim it as the unit */
  uint32_t dropped_n;       /* samples dropped on writer/reader conflict */
  vg_perf_metric_stats_t m[VG_PERF_METRIC_N];
  vg_perf_resources_t res;
} vg_perf_report_t;

#ifdef VG_HMI_PERF_ENABLED

void vg_hmi_perf_init(void);
uint32_t vg_hmi_perf_now_us(void);
void vg_hmi_perf_begin(vg_perf_metric_t m);
void vg_hmi_perf_end(vg_perf_metric_t m);
/* Record duration (now - start_us) in one call; use for spans measured
 * inside a single function (loop / input / model tick / nav). */
void vg_hmi_perf_span(vg_perf_metric_t m, uint32_t start_us);
void vg_hmi_perf_set_running(bool running);
/* HMI thread only: publish the 1 Hz resource snapshot. commits and
 * last_commit_ms stay owned by the flush path and are preserved. */
void vg_hmi_perf_publish_resources(const vg_perf_resources_t * res);
/* Copy a coherent report under the short lock; false before first init. */
bool vg_hmi_perf_read(vg_perf_report_t * out);
/* Static storage footprint of the stats state (diagnostic output). */
uint32_t vg_hmi_perf_budget_bytes(void);
/* Sort a copied sample buffer ascending and return the nearest-rank value:
 * index ceil(pct/100 * n), 1-based. Reader-side helper, never on the hot
 * path. */
uint32_t vg_hmi_perf_nearest_rank(uint32_t * samples, uint16_t n, uint8_t pct);
/* Host test hook: deterministic time source; NULL restores the monotonic
 * clock. Never set by firmware code. */
void vg_hmi_perf_set_time_source(uint32_t (*fn)(void));

#else /* !VG_HMI_PERF_ENABLED */

/* Same call sites compile away; keep the types for diagnostics code that
 * is compiled in both configurations. */
static inline void vg_hmi_perf_init(void) { }
static inline uint32_t vg_hmi_perf_now_us(void) { return 0; }
static inline void vg_hmi_perf_begin(vg_perf_metric_t m) { (void)m; }
static inline void vg_hmi_perf_end(vg_perf_metric_t m) { (void)m; }
static inline void vg_hmi_perf_span(vg_perf_metric_t m, uint32_t start_us)
{
  (void)m; (void)start_us;
}
static inline void vg_hmi_perf_set_running(bool running) { (void)running; }
static inline void vg_hmi_perf_publish_resources(const vg_perf_resources_t * res)
{
  (void)res;
}
static inline bool vg_hmi_perf_read(vg_perf_report_t * out)
{
  (void)out;
  return false;
}
static inline uint32_t vg_hmi_perf_budget_bytes(void) { return 0; }
static inline uint32_t vg_hmi_perf_nearest_rank(uint32_t * samples,
                                                uint16_t n, uint8_t pct)
{
  (void)samples; (void)n; (void)pct;
  return 0;
}

#endif /* VG_HMI_PERF_ENABLED */

#ifdef __cplusplus
}
#endif

#endif /* VG_HMI_PERF_H */
