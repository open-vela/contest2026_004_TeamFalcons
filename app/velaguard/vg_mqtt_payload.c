/****************************************************************************
 * app/velaguard/vg_mqtt_payload.c
 *
 * UTF-8 JSON skeletons for dashboard-api board publish. No MQTT-C.
 ****************************************************************************/

#include "vg_mqtt_payload.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static int appendf(char *out, size_t n, int off, const char *fmt, ...)
{
  va_list ap;
  int w;

  if (off < 0 || (size_t)off >= n)
    {
      return -1;
    }

  va_start(ap, fmt);
  w = vsnprintf(out + off, n - (size_t)off, fmt, ap);
  va_end(ap);
  if (w < 0 || (size_t)off + (size_t)w >= n)
    {
      return -1;
    }

  return off + w;
}

int vg_mqtt_topic(char *out, size_t n, const char *device_id,
                  const char *kind)
{
  if (out == NULL || n == 0 || device_id == NULL || kind == NULL ||
      device_id[0] == '\0' || kind[0] == '\0')
    {
      return -1;
    }

  if (snprintf(out, n, "vg/%s/%s", device_id, kind) >= (int)n)
    {
      return -1;
    }

  return 0;
}

int vg_mqtt_format_lwt(char *out, size_t n, const char *device_id)
{
  if (out == NULL || device_id == NULL || device_id[0] == '\0')
    {
      return -1;
    }

  if (snprintf(out, n, "{\"device_id\":\"%s\",\"online\":false}",
               device_id) >= (int)n)
    {
      return -1;
    }

  return 0;
}

int vg_mqtt_format_status(char *out, size_t n, const char *device_id,
                          const char *network, long long uptime_ms,
                          long long ts_ms)
{
  if (out == NULL || device_id == NULL || device_id[0] == '\0')
    {
      return -1;
    }

  if (network == NULL || network[0] == '\0')
    {
      network = "none";
    }

  if (snprintf(out, n,
               "{\"device_id\":\"%s\",\"online\":true,"
               "\"firmware\":\"0.1.0\",\"build_mode\":\"TEST\","
               "\"network\":\"%s\",\"uptime_ms\":%lld,\"ts_ms\":%lld,"
               "\"time_quality\":\"unknown\"}",
               device_id, network, uptime_ms, ts_ms) >= (int)n)
    {
      return -1;
    }

  return 0;
}

int vg_mqtt_format_telemetry(char *out, size_t n,
                             const struct vg_mqtt_tel_pt *pts, int npt)
{
  int i;
  int off;

  if (out == NULL || n < 3)
    {
      return -1;
    }

  if (pts == NULL || npt <= 0)
    {
      snprintf(out, n, "[]");
      return 0;
    }

  off = snprintf(out, n, "[");
  for (i = 0; i < npt; i++)
    {
      const struct vg_mqtt_tel_pt *p = &pts[i];
      const char *id = (p->id && p->id[0]) ? p->id : "-";

      if (i > 0)
        {
          off = appendf(out, n, off, ",");
          if (off < 0)
            {
              return -1;
            }
        }

      if (p->ok)
        {
          off = appendf(out, n, off,
                        "{\"id\":\"%s\",\"value\":%.6g,\"ok\":true,"
                        "\"age_ms\":%u}",
                        id, (double)p->value, (unsigned)p->age_ms);
        }
      else
        {
          off = appendf(out, n, off,
                        "{\"id\":\"%s\",\"value\":null,\"ok\":false,"
                        "\"age_ms\":%u}",
                        id, (unsigned)p->age_ms);
        }

      if (off < 0)
        {
          return -1;
        }
    }

  off = appendf(out, n, off, "]");
  return (off < 0) ? -1 : 0;
}

int vg_mqtt_tel_want_send(int ok, int prev_known, int prev_ok)
{
  if (ok)
    {
      return 1;
    }

  return (prev_known && prev_ok) ? 1 : 0;
}

int vg_mqtt_tel_hist_find(const struct vg_mqtt_tel_hist *hist, int n,
                          const char *id)
{
  int i;

  if (hist == NULL || id == NULL || id[0] == '\0' || n <= 0)
    {
      return -1;
    }

  for (i = 0; i < n; i++)
    {
      if (strcmp(hist[i].id, id) == 0)
        {
          return i;
        }
    }

  return -1;
}

int vg_mqtt_tel_hist_upsert(struct vg_mqtt_tel_hist *hist, int *n, int max,
                            const char *id, int ok)
{
  int idx;

  if (hist == NULL || n == NULL || max <= 0 || id == NULL || id[0] == '\0')
    {
      return -1;
    }

  if (*n < 0)
    {
      *n = 0;
    }

  idx = vg_mqtt_tel_hist_find(hist, *n, id);
  if (idx >= 0)
    {
      hist[idx].last_ok = ok ? 1 : 0;
      return 0;
    }

  if (*n >= max)
    {
      return -1;
    }

  snprintf(hist[*n].id, sizeof(hist[*n].id), "%s", id);
  hist[*n].last_ok = ok ? 1 : 0;
  (*n)++;
  return 0;
}

const char *vg_mqtt_alarm_kind(int is_offline, const char *cmp)
{
  if (is_offline)
    {
      return "offline";
    }

  if (cmp != NULL && strcmp(cmp, "le") == 0)
    {
      return "threshold_low";
    }

  return "threshold_high";
}

int vg_mqtt_format_alarm(char *out, size_t n, const char *device_id,
                         const char *alarm_id, const char *point_id,
                         const char *kind, const char *state, float value,
                         float thr, const char *level, long long ts_ms)
{
  int off;

  if (out == NULL || device_id == NULL || point_id == NULL || kind == NULL ||
      state == NULL || device_id[0] == '\0' || point_id[0] == '\0')
    {
      return -1;
    }

  off = snprintf(out, n,
                 "{\"ts\":%lld,\"device_id\":\"%s\"",
                 ts_ms, device_id);
  if (off < 0 || (size_t)off >= n)
    {
      return -1;
    }

  if (alarm_id != NULL && alarm_id[0] != '\0')
    {
      off = appendf(out, n, off, ",\"alarm_id\":\"%s\"", alarm_id);
      if (off < 0)
        {
          return -1;
        }
    }

  off = appendf(out, n, off,
                ",\"id\":\"%s\",\"kind\":\"%s\",\"value\":%.6g,\"thr\":%.6g,"
                "\"state\":\"%s\"",
                point_id, kind, (double)value, (double)thr, state);
  if (off < 0)
    {
      return -1;
    }

  if (level != NULL && level[0] != '\0')
    {
      off = appendf(out, n, off, ",\"level\":\"%s\"", level);
      if (off < 0)
        {
          return -1;
        }
    }

  off = appendf(out, n, off, "}");
  return (off < 0) ? -1 : 0;
}

int vg_mqtt_point_table_has_points(const char *json, size_t len)
{
  const char *p;
  const char *end;
  const char *arr;

  if (json == NULL || len == 0)
    {
      return 0;
    }

  end = json + len;
  arr = strstr(json, "\"points\"");
  if (arr == NULL || arr >= end)
    {
      return 0;
    }

  p = strchr(arr, '[');
  if (p == NULL || p >= end)
    {
      return 0;
    }

  for (p++; p < end; p++)
    {
      if (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t')
        {
          continue;
        }

      return (*p != ']');
    }

  return 0;
}
