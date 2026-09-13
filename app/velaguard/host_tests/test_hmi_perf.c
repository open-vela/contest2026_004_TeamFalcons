/* Host logic tests for vg_hmi_perf (task 09-13-hmi-performance-baseline).
 * Covers: empty window, sample recording, 256-window coverage, count
 * growth, unpaired begin/end, clock wraparound, nearest-rank percentiles
 * and coherent reads under a concurrent reader. Deterministic time comes
 * from the test-only time-source hook; the concurrency test uses the real
 * clock. Links ../vg_hmi_perf.c with -DVG_HMI_PERF_ENABLED=1.
 */

#include <assert.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "vg_hmi_perf.h"

static uint32_t s_fake_us;
static uint32_t fake_time(void)
{
  return s_fake_us;
}

static int s_fails;
static bool expect(bool cond, const char * what)
{
  printf("%s %s\n", cond ? "[PASS]" : "[FAIL]", what);
  if(!cond) s_fails++;
  return cond;
}

static void span_of_dur(vg_perf_metric_t m, uint32_t dur)
{
  uint32_t start = s_fake_us;
  s_fake_us = start + dur;
  vg_hmi_perf_span(m, start);
}

static void test_recording_and_window(void)
{
  vg_perf_report_t rep;
  uint16_t i;
  uint32_t sum = 0;

  s_fake_us = 1000;
  vg_hmi_perf_init();

  /* empty window right after init */
  assert(vg_hmi_perf_read(&rep));
  expect(rep.m[VG_PERF_LOOP].win_n == 0, "window empty after init");
  expect(rep.m[VG_PERF_LOOP].total_n == 0, "no samples after init");
  expect(rep.uptime_ms == 0 || rep.uptime_ms < 1000, "uptime starts at zero");

  /* 300 spans of duration i -> last 256 samples are 45..300 */
  for(i = 1; i <= 300; i++)
    {
      span_of_dur(VG_PERF_LOOP, i);
      sum += i;
    }
  assert(vg_hmi_perf_read(&rep));
  expect(rep.m[VG_PERF_LOOP].total_n == 300, "total count grows to 300");
  expect(rep.m[VG_PERF_LOOP].total_us == sum, "total duration accumulates");
  expect(rep.m[VG_PERF_LOOP].max_us == 300, "max tracks the largest span");
  expect(rep.m[VG_PERF_LOOP].win_n == VG_PERF_WINDOW,
         "window caps at 256 samples");

  /* oldest entries were overwritten: window holds 45..300 in ring order */
  {
    bool ordered = true;
    uint16_t start = rep.m[VG_PERF_LOOP].win_head; /* oldest slot */
    for(i = 0; i < VG_PERF_WINDOW; i++)
      {
        uint32_t v = rep.m[VG_PERF_LOOP].win[(start + i) % VG_PERF_WINDOW];
        if(v != (uint32_t)(45 + i)) ordered = false;
      }
    expect(ordered, "window keeps the most recent 256 samples in order");
  }
}

static void test_unpaired_events(void)
{
  vg_perf_report_t rep;

  s_fake_us = 0;
  vg_hmi_perf_init();

  /* end without begin: ignored, no fake sample */
  vg_hmi_perf_end(VG_PERF_RENDER);
  assert(vg_hmi_perf_read(&rep));
  expect(rep.m[VG_PERF_RENDER].total_n == 0,
         "stray end does not create a sample");

  /* begin without end, then a new begin: one incomplete */
  vg_hmi_perf_begin(VG_PERF_RENDER);
  s_fake_us = 100;
  vg_hmi_perf_begin(VG_PERF_RENDER);
  assert(vg_hmi_perf_read(&rep));
  expect(rep.m[VG_PERF_RENDER].incomplete_n == 1,
         "unpaired begin counted as incomplete");

  /* the second begin pairs normally */
  s_fake_us = 350;
  vg_hmi_perf_end(VG_PERF_RENDER);
  assert(vg_hmi_perf_read(&rep));
  expect(rep.m[VG_PERF_RENDER].total_n == 1 &&
         rep.m[VG_PERF_RENDER].max_us == 250,
         "paired begin/end records one real sample");
}

static void test_wraparound(void)
{
  vg_perf_report_t rep;

  vg_hmi_perf_init();
  s_fake_us = UINT32_MAX - 0x100u;
  span_of_dur(VG_PERF_FLUSH, 0x200u); /* crosses the uint32 wrap */
  assert(vg_hmi_perf_read(&rep));
  expect(rep.m[VG_PERF_FLUSH].total_n == 1 &&
         rep.m[VG_PERF_FLUSH].max_us == 0x200u,
         "wraparound uses unsigned difference");
}

static void test_nearest_rank(void)
{
  uint32_t v[5] = {5, 3, 9, 1, 7};
  uint32_t one = 42;

  expect(vg_hmi_perf_nearest_rank(v, 5, 50) == 5, "p50 nearest-rank of 5");
  expect(vg_hmi_perf_nearest_rank(v, 5, 95) == 9, "p95 nearest-rank of 5");
  expect(vg_hmi_perf_nearest_rank(v, 5, 0) == 1, "0 pct clamps to minimum");
  expect(vg_hmi_perf_nearest_rank(v, 5, 100) == 9, "100 pct is maximum");
  expect(vg_hmi_perf_nearest_rank(&one, 1, 95) == 42, "single sample rank");
  expect(vg_hmi_perf_nearest_rank(NULL, 0, 50) == 0, "empty input is zero");
}

static volatile bool s_reader_run = true;

static void * reader_thread(void * arg)
{
  vg_perf_report_t rep;
  (void)arg;
  while(s_reader_run)
    {
      if(vg_hmi_perf_read(&rep))
        {
          /* coherence: window bound holds on a concurrently read copy */
          assert(rep.m[VG_PERF_LOOP].win_n <= VG_PERF_WINDOW);
        }
    }
  return NULL;
}

static void test_concurrent_reader(void)
{
  pthread_t th;
  vg_perf_report_t rep;
  const int n_spans = 20000;
  int i;

  s_fake_us = 0;
  vg_hmi_perf_set_time_source(NULL); /* real clock */
  vg_hmi_perf_init();

  assert(pthread_create(&th, NULL, reader_thread, NULL) == 0);
  for(i = 0; i < n_spans; i++)
    {
      uint32_t start = vg_hmi_perf_now_us();
      vg_hmi_perf_span(VG_PERF_LOOP, start);
    }
  s_reader_run = false;
  pthread_join(th, NULL);

  assert(vg_hmi_perf_read(&rep));
  expect(rep.m[VG_PERF_LOOP].total_n + rep.dropped_n == (uint32_t)n_spans,
         "every span is recorded or counted as dropped");
  expect(rep.m[VG_PERF_LOOP].win_n == VG_PERF_WINDOW,
         "window stays bounded under concurrency");
  vg_hmi_perf_set_time_source(fake_time); /* restore determinism */
}

static void test_budget(void)
{
  uint32_t b = vg_hmi_perf_budget_bytes();
  expect(b < 8u * 1024u, "static stats budget under 8 KiB");
  printf("[INFO] vg_hmi_perf budget = %u bytes\n", (unsigned)b);
}

int main(void)
{
  vg_hmi_perf_set_time_source(fake_time);

  test_recording_and_window();
  test_unpaired_events();
  test_wraparound();
  test_nearest_rank();
  test_concurrent_reader();
  test_budget();

  printf("\ntest_hmi_perf: %s\n", s_fails == 0 ? "ALL PASS" : "FAILURES PRESENT");
  return s_fails == 0 ? 0 : 1;
}
