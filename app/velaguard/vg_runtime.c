/****************************************************************************
 * app/velaguard/vg_runtime.c
 *
 * Since-boot per-point online accounting and anomaly event ring.
 ****************************************************************************/

#include "vg_runtime.h"

#include "vg_frame_stats.h"

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

struct vg_runtime_point
{
  char id[24];
  uint8_t slave;
  uint8_t used;
  uint8_t online;
  enum vg_runtime_kind kind;
  uint32_t first_ok_ms;
  uint32_t last_ok_ms;
  uint32_t online_ms_accum;
  uint32_t online_since_ms;
  uint32_t offline_since_ms;
  float last_value;
  float last_thr;
  char cmp[4];
  char level[8];
};

struct vg_runtime_event
{
  uint32_t mono_ms;
  time_t wall;
  char id[24];
  enum vg_runtime_action action;
  enum vg_runtime_kind kind;
  float value;
  float threshold;
  char cmp[4];
  char reason[48];
};

static struct vg_runtime_point g_pts[VG_RUNTIME_MAX_POINTS];
static struct vg_runtime_event g_ev[VG_RUNTIME_EVENT_MAX];
static uint16_t g_ev_pos;
static uint16_t g_ev_len;
static uint32_t g_boot_mono_ms;
static int g_inited;

static uint32_t mono_ms(void)
{
  struct timespec ts;

  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint32_t)(ts.tv_sec * 1000u + (uint32_t)(ts.tv_nsec / 1000000L));
}

static int wall_ok(time_t *out)
{
  time_t now = time(NULL);

  /* Rough gate: before 2024-01-01 treat as unsynced. */
  if (now < (time_t)1704067200)
    {
      return 0;
    }

  if (out)
    {
      *out = now;
    }

  return 1;
}

void vg_mqtt_enqueue_alarm(const char *id, const char *kind,
                           const char *state, float value, float thr,
                           const char *level, long long ts_ms)
                           __attribute__((weak));

static void mqtt_note_event(const char *id, enum vg_runtime_action act,
                            enum vg_runtime_kind kind, float value,
                            float thr, const char *cmp, const char *level)
{
  const char *akind;
  const char *state;
  long long ts = 0;

  if (vg_mqtt_enqueue_alarm == NULL)
    {
      return;
    }

  if (kind == VG_RUNTIME_KIND_OFFLINE)
    {
      akind = "offline";
    }
  else if (cmp != NULL && strcmp(cmp, "le") == 0)
    {
      akind = "threshold_low";
    }
  else
    {
      akind = "threshold_high";
    }

  state = (act == VG_RUNTIME_ACT_RAISE) ? "raised" : "cleared";
  if (wall_ok(NULL))
    {
      ts = (long long)time(NULL) * 1000LL;
    }

  vg_mqtt_enqueue_alarm(id, akind, state, value, thr, level, ts);
}

static void push_event(const char *id, enum vg_runtime_action act,
                       enum vg_runtime_kind kind, float value, float thr,
                       const char *cmp, const char *reason,
                       const char *level)
{
  struct vg_runtime_event *e = &g_ev[g_ev_pos];

  memset(e, 0, sizeof(*e));
  e->mono_ms = mono_ms();
  if (!wall_ok(&e->wall))
    {
      e->wall = 0;
    }

  snprintf(e->id, sizeof(e->id), "%s", id ? id : "?");
  e->action = act;
  e->kind = kind;
  e->value = value;
  e->threshold = thr;
  if (cmp)
    {
      snprintf(e->cmp, sizeof(e->cmp), "%s", cmp);
    }

  if (reason)
    {
      snprintf(e->reason, sizeof(e->reason), "%s", reason);
    }

  g_ev_pos = (uint16_t)((g_ev_pos + 1) % VG_RUNTIME_EVENT_MAX);
  if (g_ev_len < VG_RUNTIME_EVENT_MAX)
    {
      g_ev_len++;
    }

  mqtt_note_event(id, act, kind, value, thr, cmp, level);
}

static struct vg_runtime_point *find_or_add(const char *id)
{
  int i;
  int free_i = -1;

  if (id == NULL || id[0] == '\0')
    {
      return NULL;
    }

  for (i = 0; i < VG_RUNTIME_MAX_POINTS; i++)
    {
      if (g_pts[i].used && strcmp(g_pts[i].id, id) == 0)
        {
          return &g_pts[i];
        }

      if (!g_pts[i].used && free_i < 0)
        {
          free_i = i;
        }
    }

  if (free_i < 0)
    {
      return NULL;
    }

  memset(&g_pts[free_i], 0, sizeof(g_pts[free_i]));
  snprintf(g_pts[free_i].id, sizeof(g_pts[free_i].id), "%s", id);
  g_pts[free_i].used = 1;
  return &g_pts[free_i];
}

void vg_runtime_init(void)
{
  if (g_inited)
    {
      return;
    }

  memset(g_pts, 0, sizeof(g_pts));
  memset(g_ev, 0, sizeof(g_ev));
  g_ev_pos = 0;
  g_ev_len = 0;
  g_boot_mono_ms = mono_ms();
  g_inited = 1;
  vg_fs_init();
}

uint32_t vg_runtime_uptime_s(void)
{
  uint32_t now;

  if (!g_inited)
    {
      vg_runtime_init();
    }

  now = mono_ms();
  if (now < g_boot_mono_ms)
    {
      return 0;
    }

  return (now - g_boot_mono_ms) / 1000u;
}

void vg_runtime_note_sample(const char *id, uint8_t slave, bool online,
                            float value, enum vg_runtime_kind kind,
                            float threshold, const char *cmp,
                            const char *level)
{
  struct vg_runtime_point *p;
  uint32_t now;
  bool was_online;
  enum vg_runtime_kind prev_kind;

  if (!g_inited)
    {
      vg_runtime_init();
    }

  p = find_or_add(id);
  if (p == NULL)
    {
      return;
    }

  now = mono_ms();
  was_online = p->online != 0;
  prev_kind = p->kind;
  p->slave = slave;
  p->last_value = value;
  p->last_thr = threshold;
  if (cmp)
    {
      snprintf(p->cmp, sizeof(p->cmp), "%s", cmp);
    }

  if (level && level[0] != '\0')
    {
      snprintf(p->level, sizeof(p->level), "%s", level);
    }

  if (online)
    {
      if (p->first_ok_ms == 0)
        {
          p->first_ok_ms = now;
        }

      p->last_ok_ms = now;
      if (!was_online)
        {
          if (p->offline_since_ms != 0)
            {
              push_event(id, VG_RUNTIME_ACT_CLEAR, VG_RUNTIME_KIND_OFFLINE,
                         value, 0, cmp, "offline_recovered", p->level);
            }

          p->offline_since_ms = 0;
          p->online_since_ms = now;
        }
      else if (p->online_since_ms != 0 && now >= p->online_since_ms)
        {
          p->online_ms_accum += (now - p->online_since_ms);
          p->online_since_ms = now;
        }

      p->online = 1;
    }
  else
    {
      if (was_online && p->online_since_ms != 0 && now >= p->online_since_ms)
        {
          p->online_ms_accum += (now - p->online_since_ms);
        }

      p->online_since_ms = 0;
      if (was_online || p->offline_since_ms == 0)
        {
          p->offline_since_ms = now;
          if (was_online)
            {
              push_event(id, VG_RUNTIME_ACT_RAISE, VG_RUNTIME_KIND_OFFLINE,
                         value, 0, cmp, "modbus_fail_offline", "offline");
            }
        }

      p->online = 0;
    }

  /* Threshold episode edges (offline handled above). */
  if (kind == VG_RUNTIME_KIND_THRESHOLD && prev_kind != VG_RUNTIME_KIND_THRESHOLD)
    {
      char reason[48];

      snprintf(reason, sizeof(reason), "threshold_%s",
               (cmp && cmp[0]) ? cmp : "ge");
      push_event(id, VG_RUNTIME_ACT_RAISE, VG_RUNTIME_KIND_THRESHOLD, value,
                 threshold, cmp, reason,
                 (p->level[0] != '\0') ? p->level : level);
    }
  else if (kind != VG_RUNTIME_KIND_THRESHOLD &&
           prev_kind == VG_RUNTIME_KIND_THRESHOLD)
    {
      push_event(id, VG_RUNTIME_ACT_CLEAR, VG_RUNTIME_KIND_THRESHOLD, value,
                 threshold, cmp, "threshold_cleared",
                 (p->level[0] != '\0') ? p->level : level);
    }

  p->kind = kind;
}

static void format_wall(time_t wall, char *buf, size_t n)
{
  struct tm tm;

  if (wall == 0 || !wall_ok(NULL))
    {
      snprintf(buf, n, "none");
      return;
    }

  if (localtime_r(&wall, &tm) == NULL)
    {
      snprintf(buf, n, "none");
      return;
    }

  snprintf(buf, n, "%04d-%02d-%02dT%02d:%02d:%02d",
           tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
           tm.tm_hour, tm.tm_min, tm.tm_sec);
}

static const char *kind_name(enum vg_runtime_kind k)
{
  switch (k)
    {
      case VG_RUNTIME_KIND_OFFLINE:
        return "offline";

      case VG_RUNTIME_KIND_THRESHOLD:
        return "threshold";

      default:
        return "none";
    }
}

static int appendf(char *out, size_t out_sz, int off, const char *fmt, ...)
{
  va_list ap;
  int n;

  if (off < 0 || (size_t)off >= out_sz)
    {
      return off;
    }

  va_start(ap, fmt);
  n = vsnprintf(out + off, out_sz - (size_t)off, fmt, ap);
  va_end(ap);
  if (n < 0)
    {
      return off;
    }

  if ((size_t)n >= out_sz - (size_t)off)
    {
      return (int)out_sz - 1;
    }

  return off + n;
}

int vg_runtime_format_dump(char *out, size_t out_sz)
{
  int off = 0;
  int i;
  uint32_t up_s;
  uint32_t bus_total = 0;
  uint32_t bus_ok = 0;
  uint32_t bus_crc = 0;
  uint32_t bus_to = 0;
  uint32_t bus_echo = 0;
  uint32_t bus_other = 0;
  char wallbuf[40];
  time_t now_wall = 0;

  if (out == NULL || out_sz == 0)
    {
      return 0;
    }

  if (!g_inited)
    {
      vg_runtime_init();
    }

  out[0] = '\0';
  up_s = vg_runtime_uptime_s();
  if (wall_ok(&now_wall))
    {
      format_wall(now_wall, wallbuf, sizeof(wallbuf));
    }
  else
    {
      snprintf(wallbuf, sizeof(wallbuf), "none");
    }

  off = appendf(out, out_sz, off,
                "vgruntime: uptime_s=%u wall=%s\n",
                (unsigned)up_s, wallbuf);

  for (i = 0; ; i++)
    {
      uint8_t slave = vg_fs_slave_at(i);
      struct vg_fs_boot_summary boot;
      struct vg_fs_summary win;
      uint32_t lat_avg = 0;

      if (slave == 0)
        {
          break;
        }

      vg_fs_boot_summary(slave, &boot);
      vg_fs_summary(slave, &win);
      if (boot.ok > 0)
        {
          lat_avg = boot.lat_sum_ms / boot.ok;
        }

      bus_total += boot.total;
      bus_ok += boot.ok;
      bus_crc += boot.crc_err;
      bus_to += boot.timeout;
      bus_echo += boot.echo;
      bus_other += boot.other;

      off = appendf(out, out_sz, off,
                    "vgruntime: slave=%u boot_total=%u boot_ok=%u boot_crc=%u "
                    "boot_timeout=%u boot_echo=%u boot_other=%u "
                    "boot_lat_min=%u boot_lat_max=%u boot_lat_avg=%u "
                    "win_total=%u win_ok=%u win_crc=%u win_timeout=%u "
                    "win_echo=%u win_other=%u\n",
                    (unsigned)slave,
                    (unsigned)boot.total, (unsigned)boot.ok,
                    (unsigned)boot.crc_err, (unsigned)boot.timeout,
                    (unsigned)boot.echo, (unsigned)boot.other,
                    (unsigned)boot.lat_min_ms, (unsigned)boot.lat_max_ms,
                    (unsigned)lat_avg,
                    (unsigned)win.total, (unsigned)win.ok,
                    (unsigned)win.crc_err, (unsigned)win.timeout,
                    (unsigned)win.echo, (unsigned)win.other);
    }

  off = appendf(out, out_sz, off,
                "vgruntime: bus boot_total=%u boot_ok=%u boot_crc=%u "
                "boot_timeout=%u boot_echo=%u boot_other=%u\n",
                (unsigned)bus_total, (unsigned)bus_ok, (unsigned)bus_crc,
                (unsigned)bus_to, (unsigned)bus_echo, (unsigned)bus_other);

  for (i = 0; i < VG_RUNTIME_MAX_POINTS; i++)
    {
      struct vg_runtime_point *p = &g_pts[i];
      uint32_t online_ms;
      uint32_t offline_s = 0;
      uint32_t last_ok_s = 0;
      uint32_t now;

      if (!p->used)
        {
          continue;
        }

      now = mono_ms();
      online_ms = p->online_ms_accum;
      if (p->online && p->online_since_ms != 0 && now >= p->online_since_ms)
        {
          online_ms += (now - p->online_since_ms);
        }

      if (!p->online && p->offline_since_ms != 0 && now >= p->offline_since_ms)
        {
          offline_s = (now - p->offline_since_ms) / 1000u;
        }

      if (p->last_ok_ms != 0 && now >= p->last_ok_ms)
        {
          last_ok_s = (now - p->last_ok_ms) / 1000u;
        }

      off = appendf(out, out_sz, off,
                    "vgruntime: point id=%s slave=%u online=%u online_ms=%u "
                    "offline_s=%u last_ok_ago_s=%u value=%.4g kind=%s\n",
                    p->id, (unsigned)p->slave, p->online ? 1u : 0u,
                    (unsigned)online_ms, (unsigned)offline_s,
                    (unsigned)last_ok_s, (double)p->last_value,
                    kind_name(p->kind));
    }

  /* Print oldest→newest within the ring. */
  for (i = 0; i < (int)g_ev_len; i++)
    {
      unsigned int idx;
      const struct vg_runtime_event *e;
      char wbuf[40];
      uint32_t t_s;

      idx = (g_ev_pos + VG_RUNTIME_EVENT_MAX - g_ev_len + (unsigned)i) %
            VG_RUNTIME_EVENT_MAX;
      e = &g_ev[idx];
      format_wall(e->wall, wbuf, sizeof(wbuf));
      t_s = (e->mono_ms >= g_boot_mono_ms)
                ? (e->mono_ms - g_boot_mono_ms) / 1000u
                : 0;

      off = appendf(out, out_sz, off,
                    "vgruntime: event t_boot_s=%u wall=%s id=%s action=%s "
                    "kind=%s value=%.4g thr=%.4g cmp=%s reason=%s\n",
                    (unsigned)t_s, wbuf, e->id,
                    e->action == VG_RUNTIME_ACT_RAISE ? "raise" : "clear",
                    kind_name(e->kind), (double)e->value, (double)e->threshold,
                    e->cmp[0] ? e->cmp : "-", e->reason);
    }

  return off;
}

void vg_runtime_fprint_dump(FILE *fp)
{
  int i;
  uint32_t up_s;
  uint32_t bus_total = 0;
  uint32_t bus_ok = 0;
  uint32_t bus_crc = 0;
  uint32_t bus_to = 0;
  uint32_t bus_echo = 0;
  uint32_t bus_other = 0;
  char wallbuf[40];
  time_t now_wall = 0;

  if (fp == NULL)
    {
      return;
    }

  if (!g_inited)
    {
      vg_runtime_init();
    }

  up_s = vg_runtime_uptime_s();
  if (wall_ok(&now_wall))
    {
      format_wall(now_wall, wallbuf, sizeof(wallbuf));
    }
  else
    {
      snprintf(wallbuf, sizeof(wallbuf), "none");
    }

  fprintf(fp, "vgruntime: uptime_s=%u wall=%s\n", (unsigned)up_s, wallbuf);

  for (i = 0; ; i++)
    {
      uint8_t slave = vg_fs_slave_at(i);
      struct vg_fs_boot_summary boot;
      struct vg_fs_summary win;
      uint32_t lat_avg = 0;

      if (slave == 0)
        {
          break;
        }

      vg_fs_boot_summary(slave, &boot);
      vg_fs_summary(slave, &win);
      if (boot.ok > 0)
        {
          lat_avg = boot.lat_sum_ms / boot.ok;
        }

      bus_total += boot.total;
      bus_ok += boot.ok;
      bus_crc += boot.crc_err;
      bus_to += boot.timeout;
      bus_echo += boot.echo;
      bus_other += boot.other;

      fprintf(fp,
              "vgruntime: slave=%u boot_total=%u boot_ok=%u boot_crc=%u "
              "boot_timeout=%u boot_echo=%u boot_other=%u "
              "boot_lat_min=%u boot_lat_max=%u boot_lat_avg=%u "
              "win_total=%u win_ok=%u win_crc=%u win_timeout=%u "
              "win_echo=%u win_other=%u\n",
              (unsigned)slave,
              (unsigned)boot.total, (unsigned)boot.ok,
              (unsigned)boot.crc_err, (unsigned)boot.timeout,
              (unsigned)boot.echo, (unsigned)boot.other,
              (unsigned)boot.lat_min_ms, (unsigned)boot.lat_max_ms,
              (unsigned)lat_avg,
              (unsigned)win.total, (unsigned)win.ok,
              (unsigned)win.crc_err, (unsigned)win.timeout,
              (unsigned)win.echo, (unsigned)win.other);
    }

  fprintf(fp,
          "vgruntime: bus boot_total=%u boot_ok=%u boot_crc=%u "
          "boot_timeout=%u boot_echo=%u boot_other=%u\n",
          (unsigned)bus_total, (unsigned)bus_ok, (unsigned)bus_crc,
          (unsigned)bus_to, (unsigned)bus_echo, (unsigned)bus_other);

  for (i = 0; i < VG_RUNTIME_MAX_POINTS; i++)
    {
      struct vg_runtime_point *p = &g_pts[i];
      uint32_t online_ms;
      uint32_t offline_s = 0;
      uint32_t last_ok_s = 0;
      uint32_t now;

      if (!p->used)
        {
          continue;
        }

      now = mono_ms();
      online_ms = p->online_ms_accum;
      if (p->online && p->online_since_ms != 0 && now >= p->online_since_ms)
        {
          online_ms += (now - p->online_since_ms);
        }

      if (!p->online && p->offline_since_ms != 0 && now >= p->offline_since_ms)
        {
          offline_s = (now - p->offline_since_ms) / 1000u;
        }

      if (p->last_ok_ms != 0 && now >= p->last_ok_ms)
        {
          last_ok_s = (now - p->last_ok_ms) / 1000u;
        }

      fprintf(fp,
              "vgruntime: point id=%s slave=%u online=%u online_ms=%u "
              "offline_s=%u last_ok_ago_s=%u value=%.4g kind=%s\n",
              p->id, (unsigned)p->slave, p->online ? 1u : 0u,
              (unsigned)online_ms, (unsigned)offline_s,
              (unsigned)last_ok_s, (double)p->last_value,
              kind_name(p->kind));
    }

  for (i = 0; i < (int)g_ev_len; i++)
    {
      unsigned int idx;
      const struct vg_runtime_event *e;
      char wbuf[40];
      uint32_t t_s;

      idx = (g_ev_pos + VG_RUNTIME_EVENT_MAX - g_ev_len + (unsigned)i) %
            VG_RUNTIME_EVENT_MAX;
      e = &g_ev[idx];
      format_wall(e->wall, wbuf, sizeof(wbuf));
      t_s = (e->mono_ms >= g_boot_mono_ms)
                ? (e->mono_ms - g_boot_mono_ms) / 1000u
                : 0;

      fprintf(fp,
              "vgruntime: event t_boot_s=%u wall=%s id=%s action=%s "
              "kind=%s value=%.4g thr=%.4g cmp=%s reason=%s\n",
              (unsigned)t_s, wbuf, e->id,
              e->action == VG_RUNTIME_ACT_RAISE ? "raise" : "clear",
              kind_name(e->kind), (double)e->value, (double)e->threshold,
              e->cmp[0] ? e->cmp : "-", e->reason);
    }
}

static void format_dur(uint32_t s, char *buf, size_t n)
{
  unsigned h = (unsigned)(s / 3600u);
  unsigned m = (unsigned)((s % 3600u) / 60u);
  unsigned sec = (unsigned)(s % 60u);

  if (h > 0)
    {
      snprintf(buf, n, "%u小时%u分", h, m);
    }
  else if (m > 0)
    {
      snprintf(buf, n, "%u分%u秒", m, sec);
    }
  else
    {
      snprintf(buf, n, "%u秒", sec);
    }
}

static void format_hm(time_t wall, uint32_t boot_s, char *buf, size_t n)
{
  struct tm tm;

  if (wall != 0 && wall_ok(NULL) && localtime_r(&wall, &tm) != NULL)
    {
      snprintf(buf, n, "%02d:%02d", tm.tm_hour, tm.tm_min);
      return;
    }

  snprintf(buf, n, "T+%u", (unsigned)boot_s);
}

static const char *cmp_sym(const char *cmp)
{
  if (cmp == NULL || cmp[0] == '\0' || cmp[0] == '-')
    {
      return ">=";
    }

  if (strcmp(cmp, "le") == 0)
    {
      return "<=";
    }

  if (strcmp(cmp, "eq") == 0)
    {
      return "=";
    }

  return ">=";
}

static unsigned pct_u(uint32_t ok, uint32_t total)
{
  if (total == 0)
    {
      return 0;
    }

  return (unsigned)((ok * 100u + total / 2u) / total);
}

void vg_runtime_fprint_report(FILE *fp)
{
  int i;
  int n_pts = 0;
  int shown_pts = 0;
  int ev_n;
  int ev_start;
  uint32_t up_s;
  uint32_t now;
  uint32_t bus_total = 0;
  uint32_t bus_ok = 0;
  uint32_t bus_crc = 0;
  uint32_t bus_to = 0;
  uint32_t win_total = 0;
  uint32_t win_ok = 0;
  uint32_t win_crc = 0;
  uint32_t win_to = 0;
  uint8_t worst_slave = 0;
  unsigned worst_pct = 101;
  uint32_t worst_ok = 0;
  uint32_t worst_total = 0;
  char wallbuf[40];
  char dur[32];
  time_t now_wall = 0;

  if (fp == NULL)
    {
      return;
    }

  if (!g_inited)
    {
      vg_runtime_init();
    }

  up_s = vg_runtime_uptime_s();
  now = mono_ms();
  format_dur(up_s, dur, sizeof(dur));
  if (wall_ok(&now_wall))
    {
      format_wall(now_wall, wallbuf, sizeof(wallbuf));
      if (strlen(wallbuf) >= 16 && wallbuf[10] == 'T')
        {
          wallbuf[10] = ' ';
          wallbuf[16] = '\0';
        }
    }
  else
    {
      snprintf(wallbuf, sizeof(wallbuf), "-");
    }

  fprintf(fp, "运行报告（本次上电起）\n%s  已运行 %s\n\n", wallbuf, dur);

  for (i = 0; ; i++)
    {
      uint8_t slave = vg_fs_slave_at(i);
      struct vg_fs_boot_summary boot;
      struct vg_fs_summary win;
      unsigned p;

      if (slave == 0)
        {
          break;
        }

      vg_fs_boot_summary(slave, &boot);
      vg_fs_summary(slave, &win);
      bus_total += boot.total;
      bus_ok += boot.ok;
      bus_crc += boot.crc_err;
      bus_to += boot.timeout;
      win_total += win.total;
      win_ok += win.ok;
      win_crc += win.crc_err;
      win_to += win.timeout;
      p = pct_u(boot.ok, boot.total);
      if (boot.total > 0 && p < worst_pct)
        {
          worst_pct = p;
          worst_slave = slave;
          worst_ok = boot.ok;
          worst_total = boot.total;
        }
    }

  fprintf(fp, "通信质量\n");
  if (bus_total == 0)
    {
      fprintf(fp, "暂无通信统计。\n");
    }
  else
    {
      fprintf(fp, "总线成功 %u%%（%u/%u），超时 %u，CRC %u\n",
              pct_u(bus_ok, bus_total), (unsigned)bus_ok,
              (unsigned)bus_total, (unsigned)bus_to, (unsigned)bus_crc);
      if (win_total == 0)
        {
          fprintf(fp, "近窗尚无采样。\n");
        }
      else if (win_to == 0 && win_crc == 0 && win_ok == win_total)
        {
          fprintf(fp, "近窗全部正常。\n");
        }
      else
        {
          fprintf(fp, "近窗成功 %u%%（%u/%u），超时 %u，CRC %u\n",
                  pct_u(win_ok, win_total), (unsigned)win_ok,
                  (unsigned)win_total, (unsigned)win_to, (unsigned)win_crc);
        }

      if (worst_slave != 0 && worst_pct < 100)
        {
          fprintf(fp, "最差从站 %u：成功 %u%%（%u/%u）\n",
                  (unsigned)worst_slave, worst_pct,
                  (unsigned)worst_ok, (unsigned)worst_total);
        }
    }

  for (i = 0; i < VG_RUNTIME_MAX_POINTS; i++)
    {
      if (g_pts[i].used)
        {
          n_pts++;
        }
    }

  fprintf(fp, "\n点位在线\n");
  if (n_pts == 0)
    {
      fprintf(fp, "暂无点位。\n");
    }

  for (i = 0; i < VG_RUNTIME_MAX_POINTS && shown_pts < VG_RUNTIME_REPORT_POINTS;
       i++)
    {
      struct vg_runtime_point *p = &g_pts[i];
      uint32_t online_ms;
      uint32_t offline_s = 0;
      char ondur[32];

      if (!p->used)
        {
          continue;
        }

      online_ms = p->online_ms_accum;
      if (p->online && p->online_since_ms != 0 && now >= p->online_since_ms)
        {
          online_ms += (now - p->online_since_ms);
        }

      if (!p->online && p->offline_since_ms != 0 && now >= p->offline_since_ms)
        {
          offline_s = (now - p->offline_since_ms) / 1000u;
        }

      if (p->online)
        {
          format_dur(online_ms / 1000u, ondur, sizeof(ondur));
          fprintf(fp, "%s 在线 %s  %.4g%s\n", p->id, ondur,
                  (double)p->last_value,
                  p->kind == VG_RUNTIME_KIND_THRESHOLD ? " 越限" : "");
        }
      else
        {
          format_dur(offline_s, ondur, sizeof(ondur));
          fprintf(fp, "%s 离线 %s  %.4g\n", p->id, ondur,
                  (double)p->last_value);
        }

      shown_pts++;
    }

  if (n_pts > VG_RUNTIME_REPORT_POINTS)
    {
      fprintf(fp, "其余 %d 个未列出\n", n_pts - VG_RUNTIME_REPORT_POINTS);
    }

  ev_n = (int)g_ev_len;
  ev_start = 0;
  if (ev_n > VG_RUNTIME_REPORT_EVENTS)
    {
      ev_start = ev_n - VG_RUNTIME_REPORT_EVENTS;
    }

  fprintf(fp, "\n异常时间线\n");
  if (ev_n == 0)
    {
      fprintf(fp, "暂无异常记录。\n");
    }

  for (i = ev_n - 1; i >= ev_start; i--)
    {
      unsigned int idx;
      const struct vg_runtime_event *e;
      char hm[16];
      uint32_t t_s;

      idx = (g_ev_pos + VG_RUNTIME_EVENT_MAX - g_ev_len + (unsigned)i) %
            VG_RUNTIME_EVENT_MAX;
      e = &g_ev[idx];
      t_s = (e->mono_ms >= g_boot_mono_ms)
                ? (e->mono_ms - g_boot_mono_ms) / 1000u
                : 0;
      format_hm(e->wall, t_s, hm, sizeof(hm));

      if (e->kind == VG_RUNTIME_KIND_OFFLINE)
        {
          fprintf(fp, "%s %s %s\n", hm, e->id,
                  e->action == VG_RUNTIME_ACT_RAISE ? "离线" : "恢复在线");
        }
      else if (e->action == VG_RUNTIME_ACT_RAISE)
        {
          fprintf(fp, "%s %s 越限 %.4g%s%.4g\n", hm, e->id,
                  (double)e->value, cmp_sym(e->cmp), (double)e->threshold);
        }
      else
        {
          fprintf(fp, "%s %s 越限解除\n", hm, e->id);
        }
    }
}

int vg_runtime_write_report(const char *path)
{
  FILE *fp;

  if (path == NULL || path[0] == '\0')
    {
      return -EINVAL;
    }

  if (!g_inited)
    {
      vg_runtime_init();
    }

  fp = fopen(path, "w");
  if (fp == NULL && errno == ENOENT)
    {
      char dir[160];
      char *slash;

      strncpy(dir, path, sizeof(dir) - 1);
      dir[sizeof(dir) - 1] = '\0';
      slash = strrchr(dir, '/');
      if (slash != NULL && slash != dir)
        {
          *slash = '\0';
          (void)mkdir(dir, 0755);
          fp = fopen(path, "w");
        }
    }

  if (fp == NULL)
    {
      return -errno;
    }

  fprintf(fp, "运行报告由板上统计生成，数字不是推测。\n");
  vg_runtime_fprint_report(fp);
  fclose(fp);
  return 0;
}
