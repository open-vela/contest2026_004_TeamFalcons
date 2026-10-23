/****************************************************************************
 * app/velaguard/host_tests/test_voice_answer.c
 *
 * BVO-10: the answer contract.
 *
 * The display text and the speech text must describe the same snapshot, the
 * full anomaly list must not be shortened to the eight entries the screen
 * report shows, and a model-written number that is not in the snapshot must
 * be rejected.
 ****************************************************************************/

#include <stdio.h>
#include <string.h>

#include "vg_voice_answer.h"

static int g_fail;

#define CHECK(cond) \
  do \
    { \
      if (!(cond)) \
        { \
          printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
          g_fail++; \
        } \
    } \
  while (0)

#define CHECK_STR_CONTAINS(hay, needle) \
  do \
    { \
      if (strstr((hay), (needle)) == NULL) \
        { \
          printf("FAIL %s:%d: missing '%s' in: %s\n", __FILE__, __LINE__, \
                 (needle), (hay)); \
          g_fail++; \
        } \
    } \
  while (0)

static void fill_point(struct vg_voice_facts *f)
{
  memset(f, 0, sizeof(*f));
  f->seq = 1;
  f->config_version = 3;
  f->data_age_ms = 2000;
  f->has_point = true;
  snprintf(f->point_id, sizeof(f->point_id), "ups_load");
  snprintf(f->point_name, sizeof(f->point_name), "UPS负载");
  snprintf(f->point_unit, sizeof(f->point_unit), "%s", "%");
  f->point_online = true;
  f->point_has_value = true;
  f->point_value = 45.2f;
  f->point_age_ms = 2000;
  f->point_has_warn = true;
  f->point_warn = 80.0f;
  f->point_has_crit = true;
  f->point_crit = 90.0f;
}

static void add_anomaly(struct vg_voice_facts *f, int i, const char *id,
                        uint8_t flags, uint8_t rank, float value,
                        float threshold, uint32_t age_ms, uint32_t since_ms)
{
  struct vg_runtime_anomaly *a = &f->anomalies[i];

  memset(a, 0, sizeof(*a));
  snprintf(a->id, sizeof(a->id), "%s", id);
  a->flags = flags;
  a->rank = rank;
  a->has_value = (flags & VG_RUNTIME_ANOMALY_F_THRESHOLD) != 0;
  a->value = value;
  a->threshold = threshold;
  snprintf(a->cmp, sizeof(a->cmp), "ge");
  a->age_ms = age_ms;
  a->since_ms = since_ms;
}

static void test_point(void)
{
  struct vg_voice_facts f;
  char display[1024];
  char speech[256];

  fill_point(&f);

  CHECK(vg_va_render_point(&f, display, sizeof(display), speech,
                           sizeof(speech)) > 0);
  CHECK_STR_CONTAINS(display, "ups_load");
  CHECK_STR_CONTAINS(display, "UPS负载");
  CHECK_STR_CONTAINS(display, "45.2");
  CHECK_STR_CONTAINS(display, "采样 2 秒前");
  CHECK_STR_CONTAINS(display, "警告 ≥ 80");
  CHECK_STR_CONTAINS(display, "严重 ≥ 90");
  CHECK_STR_CONTAINS(speech, "45.2");
  CHECK(vg_va_text_facts_consistent(&f, display));
  CHECK(vg_va_text_facts_consistent(&f, speech));

  /* Offline: no value may be presented as current. */

  f.point_online = false;
  f.point_age_ms = 30000;
  CHECK(vg_va_render_point(&f, display, sizeof(display), speech,
                           sizeof(speech)) > 0);
  CHECK_STR_CONTAINS(display, "离线");
  CHECK_STR_CONTAINS(speech, "离线");
  CHECK(strstr(speech, "45.2") == NULL);
  CHECK(vg_va_text_facts_consistent(&f, display));

  /* Online but no sample: say so instead of printing a zero. */

  f.point_online = true;
  f.point_has_value = false;
  CHECK(vg_va_render_point(&f, display, sizeof(display), speech,
                           sizeof(speech)) > 0);
  CHECK_STR_CONTAINS(display, "没有数值");
  CHECK(strstr(display, "：0") == NULL);
}

static void test_anomalies_empty(void)
{
  struct vg_voice_facts f;
  char display[1024];
  char speech[256];

  memset(&f, 0, sizeof(f));
  f.data_age_ms = 1500;
  f.anomaly_total = 0;
  f.anomaly_seen = 14;
  f.anomaly_n = 0;

  CHECK(vg_va_render_anomalies(&f, display, sizeof(display), speech,
                               sizeof(speech)) > 0);
  CHECK_STR_CONTAINS(display, "当前没有异常点位");
  CHECK(strstr(display, "0 个已确认点位") == NULL);
  CHECK_STR_CONTAINS(speech, "当前没有异常点位");
  CHECK(vg_va_text_facts_consistent(&f, display));
  CHECK(vg_va_text_facts_consistent(&f, speech));
}


/* Before the first acquisition cycle nothing is sampled: an empty list is
 * unknown, not healthy. */

static void test_anomalies_pending(void)
{
  struct vg_voice_facts f;
  char display[512];
  char speech[128];

  memset(&f, 0, sizeof(f));
  f.anomaly_total = 0;
  f.anomaly_seen = 0;

  CHECK(vg_va_render_anomalies(&f, display, sizeof(display), speech,
                               sizeof(speech)) > 0);
  CHECK_STR_CONTAINS(display, "采集尚未完成");
  CHECK_STR_CONTAINS(display, "无法确认异常");
  CHECK_STR_CONTAINS(speech, "无法确认异常");
  CHECK(strstr(display, "当前没有异常点位") == NULL);
}

static void test_anomalies_many(void)
{
  struct vg_voice_facts f;
  char display[2048];
  char speech[256];

  memset(&f, 0, sizeof(f));
  f.data_age_ms = 2000;
  f.anomaly_total = 12;
  f.anomaly_seen = 14;
  f.anomaly_n = 12;

  add_anomaly(&f, 0, "ups_temp", VG_RUNTIME_ANOMALY_F_THRESHOLD, 0, 95.0f,
              90.0f, 2000, 12000);
  add_anomaly(&f, 1, "ups_bat", VG_RUNTIME_ANOMALY_F_THRESHOLD, 1, 55.0f,
              50.0f, 2000, 4000);
  add_anomaly(&f, 2, "bms_1", VG_RUNTIME_ANOMALY_F_OFFLINE, 2, 0.0f, 0.0f,
              30000, 30000);

  {
    char id[16];
    int i;

    for (i = 3; i < 12; i++)
      {
        snprintf(id, sizeof(id), "sensor_%d", i);
        add_anomaly(&f, i, id, VG_RUNTIME_ANOMALY_F_STALE, 3, 1.0f, 0.0f,
                    12000, 12000);
      }
  }

  CHECK(vg_va_render_anomalies(&f, display, sizeof(display), speech,
                               sizeof(speech)) > 0);

  /* The whole set is present, not the eight-entry report slice. */

  CHECK_STR_CONTAINS(display, "sensor_11");
  CHECK_STR_CONTAINS(display, "当前异常 12 个点位");
  CHECK(strstr(display, "其余") == NULL);

  /* Speech names the total and the worst item.  The rest of the set
   * stays on the screen: the voice service needs about a second and a
   * half per spoken unit and the round has one budget. */

  CHECK_STR_CONTAINS(speech, "12");
  CHECK_STR_CONTAINS(speech, "ups_temp");
  CHECK(strstr(speech, "sensor_3") == NULL);
  CHECK(strstr(speech, "sensor_11") == NULL);
  CHECK(vg_va_text_facts_consistent(&f, display));
  CHECK(vg_va_text_facts_consistent(&f, speech));

  /* A bounded display buffer still ends with a complete line. */

  {
    char small[120];

    CHECK(vg_va_render_anomalies(&f, small, sizeof(small), speech,
                                 sizeof(speech)) > 0);
    CHECK(small[sizeof(small) - 1] == '\0');
  }
}

static void test_anomalies_truncated_snapshot(void)
{
  struct vg_voice_facts f;
  char display[2048];
  char speech[256];

  memset(&f, 0, sizeof(f));
  f.anomaly_total = 9;
  f.anomaly_seen = 14;
  f.anomaly_n = 2;
  add_anomaly(&f, 0, "a", VG_RUNTIME_ANOMALY_F_THRESHOLD, 0, 1.0f, 0.5f,
              1000, 0);
  add_anomaly(&f, 1, "b", VG_RUNTIME_ANOMALY_F_OFFLINE, 2, 0.0f, 0.0f,
              9000, 9000);

  CHECK(vg_va_render_anomalies(&f, display, sizeof(display), speech,
                               sizeof(speech)) > 0);
  CHECK_STR_CONTAINS(display, "其余 7 个未列出");
  CHECK_STR_CONTAINS(speech, "9");
}

static void test_recovery(void)
{
  struct vg_voice_facts f;
  char display[1024];
  char speech[256];

  memset(&f, 0, sizeof(f));
  f.has_recovery = true;
  snprintf(f.recovery.id, sizeof(f.recovery.id), "ups_temp");
  f.recovery.kind = VG_RUNTIME_KIND_THRESHOLD;
  f.recovery.state = VG_RUNTIME_RECOVERY_RECOVERED;
  f.recovery.history_complete = true;
  f.recovery.has_duration = true;
  f.recovery.duration_ms = 199000;
  f.recovery.raise_ms = 12000;
  f.recovery.clear_ms = 211000;
  f.recovery.episode = 4;
  f.recovery.value = 42.0f;
  f.recovery.threshold = 90.0f;

  CHECK(vg_va_render_recovery(&f, display, sizeof(display), speech,
                              sizeof(speech)) > 0);
  CHECK_STR_CONTAINS(display, "已恢复");
  CHECK_STR_CONTAINS(display, "199 秒");
  CHECK_STR_CONTAINS(speech, "已恢复");
  CHECK(vg_va_text_facts_consistent(&f, display));
  CHECK(vg_va_text_facts_consistent(&f, speech));

  /* Active alarm. */

  f.recovery.state = VG_RUNTIME_RECOVERY_ACTIVE;
  CHECK(vg_va_render_recovery(&f, display, sizeof(display), speech,
                              sizeof(speech)) > 0);
  CHECK_STR_CONTAINS(display, "仍在进行中");
  CHECK(vg_va_text_facts_consistent(&f, display));

  /* CLEAR retained, RAISE evicted: no duration may be invented. */

  f.recovery.state = VG_RUNTIME_RECOVERY_RECOVERED_PARTIAL;
  f.recovery.history_complete = false;
  f.recovery.has_duration = false;
  CHECK(vg_va_render_recovery(&f, display, sizeof(display), speech,
                              sizeof(speech)) > 0);
  CHECK_STR_CONTAINS(display, "无法给出持续时长");
  CHECK(vg_va_text_facts_consistent(&f, display));
  CHECK(vg_va_text_facts_consistent(&f, speech));

  /* No retained event: current normality is not proof of recovery. */

  f.recovery.state = VG_RUNTIME_RECOVERY_UNKNOWN;
  CHECK(vg_va_render_recovery(&f, display, sizeof(display), speech,
                              sizeof(speech)) > 0);
  CHECK_STR_CONTAINS(display, "无法确认");
  CHECK_STR_CONTAINS(speech, "无法确认");
  CHECK(vg_va_text_facts_consistent(&f, display));
}

static void test_consistency(void)
{
  struct vg_voice_facts f;
  char display[1024];
  char speech[256];

  fill_point(&f);

  /* Text without a number asserts no fact and passes. */

  CHECK(vg_va_text_facts_consistent(&f, "UPS负载 目前正常。"));
  CHECK(vg_va_text_facts_consistent(&f, ""));

  /* A number that is in the snapshot passes. */

  CHECK(vg_va_text_facts_consistent(&f, "UPS负载 是 45.2 %，警告阈值 80。"));
  CHECK(vg_va_text_facts_consistent(&f, "采样于 2 秒前。"));

  /* An invented reading fails. */

  CHECK(!vg_va_text_facts_consistent(&f, "UPS负载 是 47.9 %。"));

  /* A digit inside an identifier is not a quantity. */

  CHECK(vg_va_text_facts_consistent(&f, "点位 ups_2 不存在。"));

  /* Anomaly total is part of the snapshot. */

  memset(&f, 0, sizeof(f));
  f.anomaly_total = 3;
  f.anomaly_seen = 14;
  CHECK(vg_va_text_facts_consistent(&f, "当前有 3 个异常点位。"));
  CHECK(!vg_va_text_facts_consistent(&f, "当前有 7 个异常点位。"));

  /* Renderers must pass their own check, or the fallback would be rejected
   * by the same rule it exists to satisfy. */

  fill_point(&f);
  vg_va_render_point(&f, display, sizeof(display), speech, sizeof(speech));
  CHECK(vg_va_text_facts_consistent(&f, display));
}

static void test_speech_truncation(void)
{
  struct vg_voice_facts f;
  char display[2048];
  char speech[40];
  int i;

  memset(&f, 0, sizeof(f));
  f.anomaly_total = 12;
  f.anomaly_seen = 14;
  f.anomaly_n = 12;

  for (i = 0; i < 12; i++)
    {
      char id[16];

      snprintf(id, sizeof(id), "p%02d", i);
      add_anomaly(&f, i, id, VG_RUNTIME_ANOMALY_F_THRESHOLD, 0,
                  (float)(90 + i), 80.0f, 1000, 0);
    }

  CHECK(vg_va_render_anomalies(&f, display, sizeof(display), speech,
                               sizeof(speech)) > 0);
  CHECK(speech[sizeof(speech) - 1] == '\0');

  /* The cut is on a clause boundary, so the last character before the
   * terminator is a full stop or a comma, not half a number. */

  {
    size_t n = strlen(speech);

    CHECK(n >= 3);
    CHECK(strcmp(speech + n - 3, "。") == 0 ||
          strcmp(speech + n - 3, "，") == 0 ||
          strcmp(speech + n - 3, "；") == 0);
  }
}

int main(void)
{
  test_point();
  test_anomalies_empty();
  test_anomalies_pending();
  test_anomalies_many();
  test_anomalies_truncated_snapshot();
  test_recovery();
  test_consistency();
  test_speech_truncation();

  if (g_fail == 0)
    {
      printf("test_voice_answer: ok\n");
      return 0;
    }

  printf("test_voice_answer: %d failure(s)\n", g_fail);
  return 1;
}
