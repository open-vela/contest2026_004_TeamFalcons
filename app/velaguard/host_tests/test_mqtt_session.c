/* SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#define CONFIG_VG_DISCOVER_POINTS_PATH "mqtt-session-points.json"
#include "../vg_mqtt_session.c"

static int point_publishes, fail_publish;
const char *vg_device_id(void) { return "test-device"; }
const char *mqtt_error_str(enum MQTTErrors error) { (void)error; return "injected"; }
enum MQTTErrors mqtt_sync(struct mqtt_client *client) { return client->error; }
enum MQTTErrors mqtt_publish(struct mqtt_client *client, const char *topic,
                             const void *body, size_t n, uint8_t flags)
{
  (void)client; (void)body; (void)n; (void)flags;
  if (strstr(topic, "point_table"))
    {
      point_publishes++;
      if (fail_publish) return MQTT_ERROR_SEND_BUFFER_IS_FULL;
    }
  return MQTT_OK;
}
void vg_tcp_close(int *fd) { *fd = -1; }
int vg_live_snapshot_read(const char *path, struct vg_live_snapshot *snap)
{ (void)path; (void)snap; return -ENOENT; }
uint32_t vg_live_now_ms(void) { return 1; }
uint32_t vg_live_age_ms(uint32_t tick, uint32_t now) { return now - tick; }

int main(void)
{
  FILE *fp = fopen(CONFIG_VG_DISCOVER_POINTS_PATH, "w");
  assert(fp);
  fputs("{\"points\":[{\"id\":\"temp\"}]}", fp);
  assert(!fclose(fp));
  g_fd = 123; g_online = true; g_client.error = MQTT_OK;
  g_next_period_ms = UINT64_MAX;
  vg_mqtt_notify_point_table();
  vg_mqtt_session_poll();
  assert(point_publishes == 1 && g_pt_sent);
  fail_publish = 1;
  vg_mqtt_notify_point_table();
  vg_mqtt_session_poll();
  assert(point_publishes == 2);
  fail_publish = 0;
  g_next_period_ms = 0; /* advance to the normal periodic retry */
  vg_mqtt_session_poll();
  assert(point_publishes == 3 && g_pt_sent);
  unlink(CONFIG_VG_DISCOVER_POINTS_PATH);
  puts("test_mqtt_session: OK (failed updated table retried after earlier success)");
  return 0;
}
