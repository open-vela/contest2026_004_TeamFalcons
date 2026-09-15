/****************************************************************************
 * app/velaguard/host_tests/test_mqtt_payload.c
 *
 * Host tests for device_id formatting and dashboard-api JSON skeletons.
 ****************************************************************************/

#include <stdio.h>
#include <string.h>

#include "vg_device_id.h"
#include "vg_mqtt_payload.h"

static int g_fail;

static void expect(int cond, const char *msg)
{
  if (!cond)
    {
      fprintf(stderr, "FAIL: %s\n", msg);
      g_fail++;
    }
}

static void test_device_id_from_uid(void)
{
  uint8_t uid[VG_DEVICE_UID_LEN];
  char id[VG_DEVICE_ID_MAX];
  int i;

  for (i = 0; i < VG_DEVICE_UID_LEN; i++)
    {
      uid[i] = (uint8_t)i;
    }

  expect(vg_device_id_from_uid(uid, id, sizeof(id)) == 0, "from_uid");
  expect(strcmp(id, "vg-000102030405060708090a0b") == 0, "vg-hex id");
  expect(vg_device_id_from_uid(uid, id, 8) != 0, "short buf fails");
}

static void test_topics_and_status(void)
{
  char topic[VG_MQTT_TOPIC_MAX];
  char buf[320];

  expect(vg_mqtt_topic(topic, sizeof(topic), "vg-abc", "status") == 0,
         "status topic");
  expect(strcmp(topic, "vg/vg-abc/status") == 0, "status topic text");
  expect(vg_mqtt_format_lwt(buf, sizeof(buf), "vg-abc") == 0, "lwt");
  expect(strstr(buf, "\"device_id\":\"vg-abc\"") != NULL, "lwt device");
  expect(strstr(buf, "\"online\":false") != NULL, "lwt offline");
  expect(vg_mqtt_format_status(buf, sizeof(buf), "vg-abc", "rj45", 123,
                               456) == 0,
         "status");
  expect(strstr(buf, "\"network\":\"rj45\"") != NULL, "status net");
  expect(strstr(buf, "\"uptime_ms\":123") != NULL, "status uptime");
  expect(strstr(buf, "\"ts_ms\":456") != NULL, "status ts");
  expect(strstr(buf, "\"time_quality\":\"unknown\"") != NULL, "time q");
}

static void test_telemetry_and_alarm(void)
{
  char buf[512];
  struct vg_mqtt_tel_pt pts[2];

  pts[0].id = "temp";
  pts[0].value = 36.5f;
  pts[0].ok = 1;
  pts[0].age_ms = 120;
  pts[1].id = "flood";
  pts[1].value = 0;
  pts[1].ok = 0;
  pts[1].age_ms = 130;

  expect(vg_mqtt_format_telemetry(buf, sizeof(buf), pts, 2) == 0, "tel");
  expect(buf[0] == '[', "tel array");
  expect(strstr(buf, "\"id\":\"temp\"") != NULL, "tel temp");
  expect(strstr(buf, "\"ok\":true") != NULL, "tel ok");
  expect(strstr(buf, "\"ok\":false") != NULL, "tel fail");
  expect(strstr(buf, "\"value\":null") != NULL, "tel null value");

  expect(strcmp(vg_mqtt_alarm_kind(1, "ge"), "offline") == 0, "kind off");
  expect(strcmp(vg_mqtt_alarm_kind(0, "ge"), "threshold_high") == 0,
         "kind high");
  expect(strcmp(vg_mqtt_alarm_kind(0, "le"), "threshold_low") == 0,
         "kind low");

  expect(vg_mqtt_format_alarm(buf, sizeof(buf), "vg-abc",
                              "vg-abc-00000001-2", "temp", "threshold_high",
                              "raised", 36.5f, 35.0f, "warn",
                              1789266000000LL) == 0,
         "alarm");
  expect(strstr(buf, "\"state\":\"raised\"") != NULL, "alarm state");
  expect(strstr(buf, "\"level\":\"warn\"") != NULL, "alarm level");
  expect(strstr(buf, "\"alarm_id\":\"vg-abc-00000001-2\"") != NULL,
         "alarm id");
}

static void test_telemetry_empty_suppress(void)
{
  struct vg_mqtt_tel_hist hist[VG_MQTT_TEL_HIST_MAX];
  int n = 0;
  int idx;

  expect(vg_mqtt_tel_want_send(1, 0, 0) == 1, "ok always send");
  expect(vg_mqtt_tel_want_send(0, 0, 0) == 0, "never-seen empty skip");
  expect(vg_mqtt_tel_want_send(0, 1, 1) == 1, "value to empty send null");
  expect(vg_mqtt_tel_want_send(0, 1, 0) == 0, "empty to empty skip");
  expect(vg_mqtt_tel_want_send(1, 1, 0) == 1, "empty to value send");

  expect(vg_mqtt_tel_hist_find(NULL, 1, "temp") < 0, "find null hist");
  expect(vg_mqtt_tel_hist_upsert(hist, &n, VG_MQTT_TEL_HIST_MAX, "temp", 1)
         == 0,
         "upsert temp");
  expect(n == 1, "hist n 1");
  idx = vg_mqtt_tel_hist_find(hist, n, "temp");
  expect(idx == 0 && hist[0].last_ok == 1, "temp last ok");
  expect(vg_mqtt_tel_hist_upsert(hist, &n, VG_MQTT_TEL_HIST_MAX, "temp", 0)
         == 0,
         "upsert temp empty");
  expect(n == 1 && hist[0].last_ok == 0, "temp last empty");
  expect(vg_mqtt_tel_hist_upsert(hist, &n, VG_MQTT_TEL_HIST_MAX, "flood", 1)
         == 0,
         "upsert flood");
  expect(n == 2, "hist n 2");
  expect(vg_mqtt_tel_hist_find(hist, n, "flood") == 1, "find flood");

  {
    struct vg_mqtt_tel_hist pub[VG_MQTT_TEL_HIST_MAX];
    int pub_n = 0;
    struct
    {
      const char *id;
      int ok;
    } round0[] = {{"temp", 1}, {"flood", 0}};
    struct
    {
      const char *id;
      int ok;
    } round1[] = {{"temp", 0}, {"flood", 0}};
    struct
    {
      const char *id;
      int ok;
    } round2[] = {{"temp", 0}, {"flood", 1}};
    const char *want0[] = {"temp"};
    const char *want1[] = {"temp"};
    const char *want2[] = {"flood"};
    int i;
    int out_n;
    const char *out_id[2];

    memset(pub, 0, sizeof(pub));
    n = 0;
    memset(hist, 0, sizeof(hist));

    out_n = 0;
    for (i = 0; i < 2; i++)
      {
        idx = vg_mqtt_tel_hist_find(hist, n, round0[i].id);
        if (vg_mqtt_tel_want_send(round0[i].ok, idx >= 0,
                                  idx >= 0 ? (int)hist[idx].last_ok : 0))
          {
            out_id[out_n++] = round0[i].id;
            (void)vg_mqtt_tel_hist_upsert(pub, &pub_n, VG_MQTT_TEL_HIST_MAX,
                                          round0[i].id, round0[i].ok);
          }
      }

    expect(out_n == 1 && strcmp(out_id[0], want0[0]) == 0, "r0 only temp");
    for (i = 0; i < pub_n; i++)
      {
        (void)vg_mqtt_tel_hist_upsert(hist, &n, VG_MQTT_TEL_HIST_MAX,
                                      pub[i].id, (int)pub[i].last_ok);
      }

    pub_n = 0;
    memset(pub, 0, sizeof(pub));
    out_n = 0;
    for (i = 0; i < 2; i++)
      {
        idx = vg_mqtt_tel_hist_find(hist, n, round1[i].id);
        if (vg_mqtt_tel_want_send(round1[i].ok, idx >= 0,
                                  idx >= 0 ? (int)hist[idx].last_ok : 0))
          {
            out_id[out_n++] = round1[i].id;
            (void)vg_mqtt_tel_hist_upsert(pub, &pub_n, VG_MQTT_TEL_HIST_MAX,
                                          round1[i].id, round1[i].ok);
          }
      }

    expect(out_n == 1 && strcmp(out_id[0], want1[0]) == 0,
           "r1 temp null once");
    for (i = 0; i < pub_n; i++)
      {
        (void)vg_mqtt_tel_hist_upsert(hist, &n, VG_MQTT_TEL_HIST_MAX,
                                      pub[i].id, (int)pub[i].last_ok);
      }

    pub_n = 0;
    memset(pub, 0, sizeof(pub));
    out_n = 0;
    for (i = 0; i < 2; i++)
      {
        idx = vg_mqtt_tel_hist_find(hist, n, round2[i].id);
        if (vg_mqtt_tel_want_send(round2[i].ok, idx >= 0,
                                  idx >= 0 ? (int)hist[idx].last_ok : 0))
          {
            out_id[out_n++] = round2[i].id;
            (void)vg_mqtt_tel_hist_upsert(pub, &pub_n, VG_MQTT_TEL_HIST_MAX,
                                          round2[i].id, round2[i].ok);
          }
      }

    expect(out_n == 1 && strcmp(out_id[0], want2[0]) == 0,
           "r2 flood value, temp skip");
  }
}

static void test_point_table_empty(void)
{
  const char *empty =
    "{\"schema_version\":1,\"bus\":{\"device\":\"/dev/rs485\",\"baud\":9600},"
    "\"hits\":[],\"points\":[]}";
  const char *full =
    "{\"schema_version\":1,\"points\":[{\"id\":\"temp\",\"name\":\"t\"}]}";

  expect(vg_mqtt_point_table_has_points(empty, strlen(empty)) == 0,
         "empty points");
  expect(vg_mqtt_point_table_has_points(full, strlen(full)) == 1,
         "has points");
  expect(vg_mqtt_point_table_has_points(NULL, 0) == 0, "null json");
}

int main(void)
{
  test_device_id_from_uid();
  test_topics_and_status();
  test_telemetry_and_alarm();
  test_telemetry_empty_suppress();
  test_point_table_empty();

  if (g_fail != 0)
    {
      fprintf(stderr, "test_mqtt_payload: %d failure(s)\n", g_fail);
      return 1;
    }

  printf("test_mqtt_payload: OK\n");
  return 0;
}
