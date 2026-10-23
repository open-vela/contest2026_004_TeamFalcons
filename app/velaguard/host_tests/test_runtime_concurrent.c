/* SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include "vg_runtime.h"

void vg_mqtt_enqueue_alarm(const char *id, const char *kind, const char *state,
    float value, float threshold, const char *level, long long ts)
{ (void)id; (void)kind; (void)state; (void)value; (void)threshold; (void)level; (void)ts; }
static void *acquire(void *arg)
{
  (void)arg;
  for (int i = 0; i < 10000; i++)
    {
      vg_runtime_note_sample("p", 1, true, 95, VG_RUNTIME_KIND_THRESHOLD, 80, "ge", "crit");
      vg_runtime_note_sample("p", 1, false, 0, VG_RUNTIME_KIND_OFFLINE, 0, "", "offline");
      vg_runtime_note_sample("p", 1, true, 40, VG_RUNTIME_KIND_NONE, 0, "ge", NULL);
    }
  return NULL;
}
int main(void)
{
  pthread_t writer;
  vg_runtime_init();
  assert(!pthread_create(&writer, NULL, acquire, NULL));
  for (int i = 0; i < 10000; i++)
    {
      struct vg_runtime_anomaly a[2];
      struct vg_runtime_recovery r;
      int total;
      int n = vg_runtime_anomalies(a, 2, &total, 0);
      assert(n == total && n >= 0 && n <= 1);
      if (n) assert(a[0].value == 95 && a[0].threshold == 80 &&
                    a[0].rank == 0 && !strcmp(a[0].cmp, "ge"));
      assert(!vg_runtime_recovery("p", VG_RUNTIME_KIND_THRESHOLD, &r));
      if (r.state == VG_RUNTIME_RECOVERY_ACTIVE)
        assert(r.value == 95 && r.threshold == 80 && r.episode && !strcmp(r.cmp, "ge"));
      if (r.state == VG_RUNTIME_RECOVERY_RECOVERED)
        assert(r.value == 40 && r.history_complete && r.has_duration && r.episode);
    }
  assert(!pthread_join(writer, NULL));
  puts("test_runtime_concurrent: OK (30000 acquisition updates with 20000 coherent voice queries)");
  return 0;
}
