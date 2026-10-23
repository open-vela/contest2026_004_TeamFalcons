#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include "vg_voice_facts.h"

uint32_t vg_runtime_boot_id(void) { return 73; }
static void point(const char *id, float value)
{
  vg_voice_facts_publish_point(id, id, "V", true, true, value, 50,
                                false, 0, false, 0, "le");
}
static void *other_publisher(void *arg)
{
  struct vg_voice_facts competing;
  (void)arg;
  assert(!vg_voice_facts_capture_begin(&competing));
  point("late_old_request", 999);
  assert(!vg_voice_facts_capture_end());
  return NULL;
}
static void *scoped_publisher(void *arg)
{
  uintptr_t serial = (uintptr_t)arg;
  velaclaw_request_observe("velaguard", "velaguard:voice:1", (void *)serial, true);
  assert(vg_voice_facts_request_active());
  point("ups_load", (float)serial);
  velaclaw_request_observe(NULL, NULL, NULL, false);
  assert(!vg_voice_facts_request_active());
  point("after_scope", 888);
  return NULL;
}
static void *wrong_client(void *arg)
{
  velaclaw_request_observe("other_client", "other_client:voice:1", arg, true);
  point("ups_load", 999);
  velaclaw_request_observe(NULL, NULL, NULL, false);
  return NULL;
}
static void *background_publisher(void *arg)
{
  velaclaw_request_observe("velaguard","velaguard:12",arg,true);
  point("ups_load",999);
  velaclaw_request_observe(NULL,NULL,NULL,false);
  return NULL;
}
int main(void)
{
  struct vg_voice_facts captured, global;
  pthread_t other;
  vg_voice_facts_set_config_version(9);
  point("before_request", 123);
  assert(!vg_voice_facts_capture_begin(NULL));
  assert(vg_voice_facts_capture_begin(&captured));
  assert(!vg_voice_facts_capture_end()); /* blocked/failed tool cannot reuse old */
  assert(vg_voice_facts_capture_begin(&captured));
  point("ups_load", 45);
  assert(!pthread_create(&other, NULL, other_publisher, NULL));
  assert(!pthread_join(other, NULL));
  assert(vg_voice_facts_capture_end());
  assert(!strcmp(captured.point_id, "ups_load") && captured.point_value == 45);
  assert(captured.boot_id == 73 && captured.config_version == 9 && !strcmp(captured.point_cmp, "le"));
  assert(vg_voice_facts_copy(&global));
  assert(!strcmp(global.point_id, "late_old_request"));
  point("after_request", 888);
  assert(captured.point_value == 45); /* result stays immutable after end */
  assert(!vg_voice_facts_capture_end());
  assert(vg_voice_facts_capture_begin(&captured));
  vg_voice_facts_publish_anomalies(NULL, 0, 0, 14, 0);
  assert(vg_voice_facts_capture_end());
  assert(captured.seq && !captured.has_point && !captured.has_recovery);
  assert(captured.anomaly_total == 0 && captured.anomaly_seen == 14);
  struct vg_runtime_recovery recovery = {0};
  assert(vg_voice_facts_capture_begin(&captured));
  vg_voice_facts_publish_recovery(&recovery, 100);
  assert(vg_voice_facts_capture_end() && captured.has_recovery);
  /* 同名点位不足以证明来源；旧请求和后台发布均不得替换当前请求。 */
  assert(!vg_voice_facts_capture_request(&captured, 0, 9));
  assert(vg_voice_facts_capture_request(&captured, 42, 9));
  assert(!pthread_create(&other, NULL, scoped_publisher, (void *)(uintptr_t)41));
  assert(!pthread_join(other, NULL));
  assert(!vg_voice_facts_capture_end());
  assert(vg_voice_facts_capture_request(&captured,42,9));
  assert(!pthread_create(&other,NULL,background_publisher,(void *)(uintptr_t)42));
  assert(!pthread_join(other,NULL));
  assert(!vg_voice_facts_capture_end());
  assert(vg_voice_facts_capture_request(&captured, 42, 9));
  assert(!pthread_create(&other, NULL, wrong_client, (void *)(uintptr_t)42));
  assert(!pthread_join(other, NULL));
  assert(!vg_voice_facts_capture_end());
  assert(vg_voice_facts_capture_request(&captured, 42, 9));
  assert(!pthread_create(&other, NULL, scoped_publisher, (void *)(uintptr_t)42));
  assert(!pthread_join(other, NULL));
  assert(!pthread_create(&other, NULL, other_publisher, NULL));
  assert(!pthread_join(other, NULL));
  assert(vg_voice_facts_capture_end());
  assert(!strcmp(captured.point_id, "ups_load") && captured.point_value == 42);
  assert(vg_voice_facts_capture_request(&captured, 43, 9));
  vg_voice_facts_set_config_version(10);
  assert(!pthread_create(&other, NULL, scoped_publisher, (void *)(uintptr_t)43));
  assert(!pthread_join(other, NULL));
  assert(!vg_voice_facts_capture_end());
  puts("test_voice_facts: OK (private copy, stale request/client/thread/config rejected, request publisher scope, failed tool, three intents)");
  return 0;
}
