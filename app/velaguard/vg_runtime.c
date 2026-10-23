/****************************************************************************
 * app/velaguard/vg_runtime.c
 *
 * Since-boot per-point online accounting and anomaly event ring.
 ****************************************************************************/

#include "vg_runtime.h"

#include "vg_frame_stats.h"

#include <errno.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

static pthread_mutex_t g_snapshot_lock = PTHREAD_MUTEX_INITIALIZER;

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
  uint32_t thr_since_ms;   /* when the threshold episode began */
  uint32_t episode_thr;    /* instance id of that episode */
  uint32_t episode_off;    /* instance id of the offline episode */
};

struct vg_runtime_event
{
  uint32_t mono_ms;
  time_t wall;
  uint32_t serial;        /* 1-based order in this boot; 0 marks an empty slot */
  uint32_t episode;       /* RAISE and its CLEAR share this instance id */
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
static uint32_t g_ev_seq;
static uint32_t g_ev_dropped;
static uint32_t g_episode_seq;
static uint32_t g_boot_id;
static int g_inited;
static uint32_t g_config_version;

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
                       const char *level, uint32_t episode)
{
  struct vg_runtime_event *e = &g_ev[g_ev_pos];

  memset(e, 0, sizeof(*e));
  e->mono_ms = mono_ms();
  e->serial = ++g_ev_seq;
  e->episode = episode;
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
  else
    {
      /* The slot being written next was the oldest event: it is gone. */
      g_ev_dropped++;
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

static void runtime_init_locked(void)
{
  if (g_inited)
    {
      return;
    }

  memset(g_pts, 0, sizeof(g_pts));
  memset(g_ev, 0, sizeof(g_ev));
  g_ev_pos = 0;
  g_ev_len = 0;
  g_ev_seq = 0;
  g_ev_dropped = 0;
  g_episode_seq = 0;
  g_boot_mono_ms = mono_ms();
  g_boot_id = (g_boot_mono_ms != 0) ? g_boot_mono_ms : 1u;
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

static void runtime_note_sample_locked(const char *id, uint8_t slave, bool online,
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
      runtime_init_locked();
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
  if (online)
    {
      p->last_value = value;
      p->last_thr = threshold;
      if (cmp) snprintf(p->cmp, sizeof(p->cmp), "%s", cmp);
      if (level && level[0]) snprintf(p->level, sizeof(p->level), "%s", level);
    }
  else
    {
      /* A failed read proves no new value and cannot clear a threshold.
       * Retain that episode until a successful sample proves recovery. */
      kind = prev_kind == VG_RUNTIME_KIND_THRESHOLD
               ? VG_RUNTIME_KIND_THRESHOLD : VG_RUNTIME_KIND_OFFLINE;
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
                         value, 0, cmp, "offline_recovered", p->level,
                         p->episode_off);
              p->episode_off = 0;
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
              p->episode_off = ++g_episode_seq;
              push_event(id, VG_RUNTIME_ACT_RAISE, VG_RUNTIME_KIND_OFFLINE,
                         value, 0, cmp, "modbus_fail_offline", "offline",
                         p->episode_off);
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
      p->episode_thr = ++g_episode_seq;
      p->thr_since_ms = now;
      push_event(id, VG_RUNTIME_ACT_RAISE, VG_RUNTIME_KIND_THRESHOLD, value,
                 threshold, cmp, reason,
                 (p->level[0] != '\0') ? p->level : level,
                 p->episode_thr);
    }
  else if (kind != VG_RUNTIME_KIND_THRESHOLD &&
           prev_kind == VG_RUNTIME_KIND_THRESHOLD)
    {
      push_event(id, VG_RUNTIME_ACT_CLEAR, VG_RUNTIME_KIND_THRESHOLD, value,
                 threshold, cmp, "threshold_cleared",
                 (p->level[0] != '\0') ? p->level : level,
                 p->episode_thr);
      p->episode_thr = 0;
      p->thr_since_ms = 0;
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

/* The report body is written once and reaches two destinations: a FILE for
 * the report page's fallback file, and a buffer for the agent tool that
 * answers "运行报告" over the console.  Keeping one body means the screen and
 * the spoken answer cannot drift apart. */

struct vg_report_sink
{
  FILE  *fp;     /* set for the FILE destination */
  char  *buf;    /* set for the buffer destination */
  size_t cap;
  int    off;
};

static void sink_put(struct vg_report_sink *s, const char *fmt, ...)
{
  va_list ap;

  if (s == NULL)
    {
      return;
    }

  if (s->fp != NULL)
    {
      va_start(ap, fmt);
      vfprintf(s->fp, fmt, ap);
      va_end(ap);
      return;
    }

  if (s->buf == NULL || s->cap == 0 || s->off >= (int)s->cap - 1)
    {
      return;
    }

  va_start(ap, fmt);
  /* snprintf returns the untruncated length, so saturate rather than letting
   * the offset walk past the buffer on the next call. */
  {
    int n = vsnprintf(s->buf + s->off, s->cap - (size_t)s->off, fmt, ap);

    if (n > 0)
      {
        s->off += n;
        if (s->off > (int)s->cap - 1)
          {
            s->off = (int)s->cap - 1;
          }
      }
  }
  va_end(ap);
}

/* Provenance line shared by the file and buffer renderings.  The agent
 * paraphrases this text, so it should know the numbers are measured on the
 * board rather than supplied by it. */
#define VG_RUNTIME_PROVENANCE \
  "运行报告由板上统计生成，数字不是推测。\n"

static void report_emit(struct vg_report_sink *s)
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

  if (s == NULL)
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

  sink_put(s, "运行报告（本次上电起）\n%s  已运行 %s\n\n", wallbuf, dur);

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

  sink_put(s, "通信质量\n");
  if (bus_total == 0)
    {
      sink_put(s, "暂无通信统计。\n");
    }
  else
    {
      sink_put(s, "总线成功 %u%%（%u/%u），超时 %u，CRC %u\n",
              pct_u(bus_ok, bus_total), (unsigned)bus_ok,
              (unsigned)bus_total, (unsigned)bus_to, (unsigned)bus_crc);
      if (win_total == 0)
        {
          sink_put(s, "近窗尚无采样。\n");
        }
      else if (win_to == 0 && win_crc == 0 && win_ok == win_total)
        {
          sink_put(s, "近窗全部正常。\n");
        }
      else
        {
          sink_put(s, "近窗成功 %u%%（%u/%u），超时 %u，CRC %u\n",
                  pct_u(win_ok, win_total), (unsigned)win_ok,
                  (unsigned)win_total, (unsigned)win_to, (unsigned)win_crc);
        }

      if (worst_slave != 0 && worst_pct < 100)
        {
          sink_put(s, "最差从站 %u：成功 %u%%（%u/%u）\n",
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

  sink_put(s, "\n点位在线\n");
  if (n_pts == 0)
    {
      sink_put(s, "暂无点位。\n");
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
          sink_put(s, "%s 在线 %s  %.4g%s\n", p->id, ondur,
                  (double)p->last_value,
                  p->kind == VG_RUNTIME_KIND_THRESHOLD ? " 越限" : "");
        }
      else
        {
          format_dur(offline_s, ondur, sizeof(ondur));
          sink_put(s, "%s 离线 %s  %.4g\n", p->id, ondur,
                  (double)p->last_value);
        }

      shown_pts++;
    }

  if (n_pts > VG_RUNTIME_REPORT_POINTS)
    {
      sink_put(s, "其余 %d 个未列出\n", n_pts - VG_RUNTIME_REPORT_POINTS);
    }

  ev_n = (int)g_ev_len;
  ev_start = 0;
  if (ev_n > VG_RUNTIME_REPORT_EVENTS)
    {
      ev_start = ev_n - VG_RUNTIME_REPORT_EVENTS;
    }

  sink_put(s, "\n异常时间线\n");
  if (ev_n == 0)
    {
      sink_put(s, "暂无异常记录。\n");
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
          sink_put(s, "%s %s %s\n", hm, e->id,
                  e->action == VG_RUNTIME_ACT_RAISE ? "离线" : "恢复在线");
        }
      else if (e->action == VG_RUNTIME_ACT_RAISE)
        {
          sink_put(s, "%s %s 越限 %.4g%s%.4g\n", hm, e->id,
                  (double)e->value, cmp_sym(e->cmp), (double)e->threshold);
        }
      else
        {
          sink_put(s, "%s %s 越限解除\n", hm, e->id);
        }
    }
}

void vg_runtime_fprint_report(FILE *fp)
{
  struct vg_report_sink s;

  if (fp == NULL)
    {
      return;
    }

  memset(&s, 0, sizeof(s));
  s.fp = fp;
  report_emit(&s);
}

int vg_runtime_format_report(char *out, size_t cap)
{
  struct vg_report_sink s;

  if (out == NULL || cap == 0)
    {
      return 0;
    }

  out[0] = '\0';
  memset(&s, 0, sizeof(s));
  s.buf = out;
  s.cap = cap;

  /* Same provenance line the file gets.  It matters more here: the model
   * reads this text and then paraphrases it, so it should know the numbers
   * are measured on the board rather than supplied by it. */
  sink_put(&s, VG_RUNTIME_PROVENANCE);
  report_emit(&s);
  return s.off;
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

  fprintf(fp, VG_RUNTIME_PROVENANCE);
  vg_runtime_fprint_report(fp);
  fclose(fp);
  return 0;
}

/* ── Anomaly and recovery queries (BVO-09) ───────────────────────── */

/* Worst category decides the order: a critical threshold outranks a warning,
 * a warning outranks an offline point, and stale data alone is last.  A point
 * that carries several flags keeps its worst one here and all of them in
 * flags. */

static uint8_t anomaly_rank(uint8_t flags, const char *level)
{
  if ((flags & VG_RUNTIME_ANOMALY_F_THRESHOLD) != 0)
    {
      return (level != NULL && strcmp(level, "crit") == 0) ? 0 : 1;
    }

  if ((flags & VG_RUNTIME_ANOMALY_F_OFFLINE) != 0)
    {
      return 2;
    }

  return 3;
}

static int runtime_anomalies_locked(struct vg_runtime_anomaly *out, int max,
                         int *total_out, uint32_t stale_ms)
{
  struct vg_runtime_anomaly all[VG_RUNTIME_MAX_POINTS];
  uint32_t now;
  int total = 0;
  int i;
  int j;

  if (total_out != NULL)
    {
      *total_out = 0;
    }

  if (out == NULL || max < 0)
    {
      return -1;
    }

  if (!g_inited)
    {
      runtime_init_locked();
    }

  now = mono_ms();

  for (i = 0; i < VG_RUNTIME_MAX_POINTS; i++)
    {
      struct vg_runtime_point *p = &g_pts[i];
      struct vg_runtime_anomaly *a;
      bool has_sample;
      uint32_t age;

      if (!p->used)
        {
          continue;
        }

      a = &all[total];
      memset(a, 0, sizeof(*a));
      snprintf(a->id, sizeof(a->id), "%s", p->id);
      a->slave = p->slave;

      has_sample = p->first_ok_ms != 0;
      a->has_value = has_sample;
      a->value = p->last_value;
      a->threshold = p->last_thr;
      snprintf(a->cmp, sizeof(a->cmp), "%s", p->cmp);
      snprintf(a->level, sizeof(a->level), "%s", p->level);

      age = (has_sample && now >= p->last_ok_ms)
                ? now - p->last_ok_ms
                : 0;
      a->age_ms = age;

      if (!p->online || p->kind == VG_RUNTIME_KIND_OFFLINE)
        {
          a->flags |= VG_RUNTIME_ANOMALY_F_OFFLINE;
        }

      if (p->kind == VG_RUNTIME_KIND_THRESHOLD)
        {
          a->flags |= VG_RUNTIME_ANOMALY_F_THRESHOLD;
        }

      /* A point that never sampled successfully is not stale: it has no
       * sample at all, which is what the offline flag already says. */

      if (stale_ms > 0 && has_sample && age > stale_ms)
        {
          a->flags |= VG_RUNTIME_ANOMALY_F_STALE;
        }

      if (a->flags == 0)
        {
          continue;
        }

      a->rank = anomaly_rank(a->flags, a->level);

      if ((a->flags & VG_RUNTIME_ANOMALY_F_OFFLINE) != 0)
        {
          a->since_ms = (p->offline_since_ms != 0 && now >= p->offline_since_ms)
                            ? now - p->offline_since_ms
                            : 0;
        }
      else if ((a->flags & VG_RUNTIME_ANOMALY_F_THRESHOLD) != 0)
        {
          a->since_ms = (p->thr_since_ms != 0 && now >= p->thr_since_ms)
                            ? now - p->thr_since_ms
                            : 0;
        }
      else
        {
          a->since_ms = age;
        }

      total++;
    }

  /* Insertion sort by rank then id.  At most VG_RUNTIME_MAX_POINTS entries,
   * and the order has to be stable enough that the same snapshot always
   * produces the same speech summary. */

  for (i = 1; i < total; i++)
    {
      struct vg_runtime_anomaly key = all[i];

      j = i - 1;
      while (j >= 0 &&
             (all[j].rank > key.rank ||
              (all[j].rank == key.rank && strcmp(all[j].id, key.id) > 0)))
        {
          all[j + 1] = all[j];
          j--;
        }

      all[j + 1] = key;
    }

  if (total_out != NULL)
    {
      *total_out = total;
    }

  if (total > max)
    {
      total = max;
    }

  for (i = 0; i < total; i++)
    {
      out[i] = all[i];
    }

  return total;
}

static int runtime_recovery_locked(const char *id, enum vg_runtime_kind kind,
                        struct vg_runtime_recovery *out)
{
  const struct vg_runtime_event *latest = NULL;
  const struct vg_runtime_event *raise = NULL;
  struct vg_runtime_point *p = NULL;
  int i;

  if (out == NULL || id == NULL || id[0] == '\0')
    {
      return -1;
    }

  memset(out, 0, sizeof(*out));

  if (!g_inited)
    {
      runtime_init_locked();
    }

  snprintf(out->id, sizeof(out->id), "%s", id);
  out->kind = kind;
  out->state = VG_RUNTIME_RECOVERY_UNKNOWN;

  for (i = 0; i < VG_RUNTIME_MAX_POINTS; i++)
    {
      if (g_pts[i].used && strcmp(g_pts[i].id, id) == 0)
        {
          p = &g_pts[i];
          break;
        }
    }

  /* Newest first.  The first event for this point and kind decides whether
   * the episode is open; a CLEAR then looks back for the RAISE that shares
   * its episode id, so an unrelated event in between cannot break the pair. */

  for (i = 0; i < g_ev_len; i++)
    {
      int idx = (int)g_ev_pos - 1 - i;
      const struct vg_runtime_event *e;

      while (idx < 0)
        {
          idx += VG_RUNTIME_EVENT_MAX;
        }

      e = &g_ev[idx];
      if (e->serial == 0)
        {
          continue;
        }

      if (strcmp(e->id, id) != 0 || e->kind != kind)
        {
          continue;
        }

      if (latest == NULL)
        {
          latest = e;
          if (e->action == VG_RUNTIME_ACT_RAISE)
            {
              break;
            }

          continue;
        }

      if (e->action == VG_RUNTIME_ACT_RAISE && e->episode == latest->episode)
        {
          raise = e;
          break;
        }
    }

  if (latest == NULL)
    {
      /* No retained event.  The current state can still show an open episode,
       * but a point that is normal now is not evidence of a past recovery. */

      bool active = false;

      if (p != NULL)
        {
          if (kind == VG_RUNTIME_KIND_OFFLINE)
            {
              active = (!p->online || p->kind == VG_RUNTIME_KIND_OFFLINE);
            }
          else if (kind == VG_RUNTIME_KIND_THRESHOLD)
            {
              active = (p->kind == VG_RUNTIME_KIND_THRESHOLD);
            }
        }

      if (active)
        {
          out->state = VG_RUNTIME_RECOVERY_ACTIVE;
        }

      return 0;
    }

  out->episode = latest->episode;
  snprintf(out->cmp, sizeof(out->cmp), "%s", latest->cmp);

  if (latest->action == VG_RUNTIME_ACT_RAISE)
    {
      out->state = VG_RUNTIME_RECOVERY_ACTIVE;
      out->history_complete = true;
      out->raise_ms = latest->mono_ms;
      out->raise_wall = latest->wall;
      out->value = latest->value;
      out->threshold = latest->threshold;
      return 0;
    }

  out->clear_ms = latest->mono_ms;
  out->clear_wall = latest->wall;
  out->value = latest->value;
  out->threshold = latest->threshold;

  if (raise != NULL)
    {
      out->state = VG_RUNTIME_RECOVERY_RECOVERED;
      out->history_complete = true;
      out->has_duration = true;
      out->raise_ms = raise->mono_ms;
      out->raise_wall = raise->wall;
      out->duration_ms = (latest->mono_ms >= raise->mono_ms)
                             ? latest->mono_ms - raise->mono_ms
                             : 0;
    }
  else
    {
      /* The CLEAR is real, the start is gone.  Report the recovery and say
       * the duration is unknown rather than guessing from the current state. */

      out->state = VG_RUNTIME_RECOVERY_RECOVERED_PARTIAL;
      out->history_complete = false;
    }

  return 0;
}

uint32_t vg_runtime_boot_id(void)
{
  vg_runtime_init();
  return g_boot_id; /* immutable after the synchronized initializer */
}

uint32_t vg_runtime_events_dropped(void)
{
  vg_runtime_init();
  pthread_mutex_lock(&g_snapshot_lock);
  uint32_t dropped = g_ev_dropped;
  pthread_mutex_unlock(&g_snapshot_lock);
  return dropped;
}

int vg_runtime_points_seen(void)
{
  int seen = 0;
  vg_runtime_init();
  pthread_mutex_lock(&g_snapshot_lock);
  for (int i = 0; i < VG_RUNTIME_MAX_POINTS; i++)
    if (g_pts[i].used) seen++;
  pthread_mutex_unlock(&g_snapshot_lock);
  return seen;
}

/* Acquisition and the two voice snapshot queries share one publication
 * lock. No file, TLS, model or speech operation runs while it is held. */
void vg_runtime_init(void)
{
  pthread_mutex_lock(&g_snapshot_lock);
  runtime_init_locked();
  pthread_mutex_unlock(&g_snapshot_lock);
}

void vg_runtime_config_changed(uint32_t version)
{
  if (!version) return;
  pthread_mutex_lock(&g_snapshot_lock);
  runtime_init_locked();
  if (version != g_config_version)
    {
      /* 配置换代后旧实例不能归给同名新点；不产生虚假的恢复事件。 */
      memset(g_pts, 0, sizeof(g_pts));
      memset(g_ev, 0, sizeof(g_ev));
      g_ev_pos = g_ev_len = 0;
      g_ev_dropped = 0;
      g_config_version = version;
    }
  pthread_mutex_unlock(&g_snapshot_lock);
}

int vg_runtime_note_sample_versioned(uint32_t version, const char *id,
                            uint8_t slave, bool online, float value,
                            enum vg_runtime_kind kind, float threshold,
                            const char *cmp, const char *level)
{
  pthread_mutex_lock(&g_snapshot_lock);
  if (!version || version != g_config_version)
    { pthread_mutex_unlock(&g_snapshot_lock); return -ESTALE; }
  runtime_note_sample_locked(id, slave, online, value, kind, threshold, cmp, level);
  pthread_mutex_unlock(&g_snapshot_lock);
  return 0;
}

void vg_runtime_note_sample(const char *id, uint8_t slave, bool online,
                            float value, enum vg_runtime_kind kind,
                            float threshold, const char *cmp, const char *level)
{
  pthread_mutex_lock(&g_snapshot_lock);
  if (!g_config_version)
    runtime_note_sample_locked(id, slave, online, value, kind, threshold, cmp, level);
  pthread_mutex_unlock(&g_snapshot_lock);
}

int vg_runtime_anomalies(struct vg_runtime_anomaly *out, int max,
                         int *total, uint32_t stale_ms)
{
  pthread_mutex_lock(&g_snapshot_lock);
  int n = runtime_anomalies_locked(out, max, total, stale_ms);
  pthread_mutex_unlock(&g_snapshot_lock);
  return n;
}

int vg_runtime_recovery(const char *id, enum vg_runtime_kind kind,
                        struct vg_runtime_recovery *out)
{
  pthread_mutex_lock(&g_snapshot_lock);
  int ret = runtime_recovery_locked(id, kind, out);
  pthread_mutex_unlock(&g_snapshot_lock);
  return ret;
}
