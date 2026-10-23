/* SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include "vg_discover.h"

static uint32_t generation;
static void *publish(void *arg)
{
  (void)arg;
  struct vg_live_snapshot s = {.n = 2};
  strcpy(s.samples[0].id, "one"); strcpy(s.samples[1].id, "two");
  for (int i = 1; i <= 10000; i++) {
    s.tick_ms = i;
    s.samples[0].value = i; s.samples[1].value = -i;
    assert(!vg_live_snapshot_publish(generation, &s));
  }
  return NULL;
}
int main(void)
{
  struct vg_discover_summary table = {0}, copied;
  struct vg_live_snapshot s = {0}, got;
  assert(vg_live_points_view(&copied, &got, NULL) == 0);
  table.n_points = 2;
  strcpy(table.points[0].id, "one"); strcpy(table.points[1].id, "two");
  assert(!vg_live_points_replace(&table));
  generation = vg_live_points_gen();
  assert(vg_live_points_view(&copied, &got, NULL) == 2 && got.tick_ms == 0);
  s.n = 2; s.tick_ms = 1;
  strcpy(s.samples[0].id, "one"); strcpy(s.samples[1].id, "two");
  assert(vg_live_snapshot_publish(generation - 1, &s) == -ESTALE);
  strcpy(s.samples[1].id, "wrong");
  assert(vg_live_snapshot_publish(generation, &s) < 0);
  strcpy(s.samples[1].id, "two");
  assert(!vg_live_snapshot_publish(generation, &s));
  pthread_t writer;
  assert(!pthread_create(&writer, NULL, publish, NULL));
  for (int i = 0; i < 10000; i++) {
    uint32_t gen;
    assert(vg_live_points_view(&copied, &got, &gen) == 2);
    assert(gen == generation && got.n == 2);
    assert(got.samples[0].value + got.samples[1].value == 0);
  }
  assert(!pthread_join(writer, NULL));
  table.n_points = 1; strcpy(table.points[0].id, "replacement");
  assert(!vg_live_points_replace(&table));
  assert(vg_live_points_view(&copied, &got, NULL) == 1 && !got.tick_ms && !got.n);
  assert(!strcmp(copied.points[0].id, "replacement"));
  assert(vg_live_snapshot_publish(generation, &s) == -ESTALE);
  puts("test_live_memory: OK (current boot only, confirmed generation, invalid source, coherent samples, config replacement invalidates old data)");
  return 0;
}
