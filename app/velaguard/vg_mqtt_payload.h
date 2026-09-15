/****************************************************************************
 * app/velaguard/vg_mqtt_payload.h
 *
 * Host-testable JSON / topic helpers for dashboard-api v1 board publish.
 ****************************************************************************/

#ifndef __VG_MQTT_PAYLOAD_H
#define __VG_MQTT_PAYLOAD_H

#include <stddef.h>
#include <stdint.h>

#define VG_MQTT_TOPIC_MAX 64
/* Must stay >= VG_DISCOVER_MAX_POINTS / VG_POINT_ID_MAX. */
#define VG_MQTT_TEL_HIST_MAX 32
#define VG_MQTT_TEL_ID_MAX 24

struct vg_mqtt_tel_pt
{
  const char *id;
  float value;
  int ok;
  uint32_t age_ms;
};

struct vg_mqtt_tel_hist
{
  char id[VG_MQTT_TEL_ID_MAX];
  uint8_t last_ok;
};

int vg_mqtt_topic(char *out, size_t n, const char *device_id,
                  const char *kind);

int vg_mqtt_format_lwt(char *out, size_t n, const char *device_id);

int vg_mqtt_format_status(char *out, size_t n, const char *device_id,
                          const char *network, long long uptime_ms,
                          long long ts_ms);

int vg_mqtt_format_telemetry(char *out, size_t n,
                             const struct vg_mqtt_tel_pt *pts, int npt);

int vg_mqtt_tel_want_send(int ok, int prev_known, int prev_ok);

int vg_mqtt_tel_hist_find(const struct vg_mqtt_tel_hist *hist, int n,
                          const char *id);

int vg_mqtt_tel_hist_upsert(struct vg_mqtt_tel_hist *hist, int *n, int max,
                            const char *id, int ok);

const char *vg_mqtt_alarm_kind(int is_offline, const char *cmp);

int vg_mqtt_format_alarm(char *out, size_t n, const char *device_id,
                         const char *alarm_id, const char *point_id,
                         const char *kind, const char *state, float value,
                         float thr, const char *level, long long ts_ms);

int vg_mqtt_point_table_has_points(const char *json, size_t len);

#endif
