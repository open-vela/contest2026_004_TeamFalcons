/****************************************************************************
 * app/velaguard/vg_frame_stats.c
 *
 * Sliding-window frame quality stats per Modbus slave (host-testable).
 ****************************************************************************/

#include "vg_frame_stats.h"

#include <errno.h>
#include <stdbool.h>
#include <string.h>

struct vg_fs_event
{
  enum vg_fs_result result;
  uint32_t          latency_ms;
};

struct vg_fs_bucket
{
  uint8_t           slave;
  uint8_t           len;
  uint16_t          pos;
  struct vg_fs_event ring[VG_FS_WINDOW];
  struct vg_fs_boot_summary boot;
};

static struct vg_fs_bucket g_buckets[VG_FS_MAX_SLAVES];
static int g_inited;

static struct vg_fs_bucket *find_bucket(uint8_t slave, bool create)
{
  int i;
  struct vg_fs_bucket *free_slot = NULL;

  if (slave == 0)
    {
      return NULL;
    }

  for (i = 0; i < VG_FS_MAX_SLAVES; i++)
    {
      if (g_buckets[i].slave == slave)
        {
          return &g_buckets[i];
        }

      if (g_buckets[i].slave == 0 && free_slot == NULL)
        {
          free_slot = &g_buckets[i];
        }
    }

  if (!create || free_slot == NULL)
    {
      return NULL;
    }

  memset(free_slot, 0, sizeof(*free_slot));
  free_slot->slave = slave;
  free_slot->boot.lat_min_ms = UINT32_MAX;
  return free_slot;
}

static void bucket_reset(struct vg_fs_bucket *b)
{
  if (b == NULL)
    {
      return;
    }

  b->len = 0;
  b->pos = 0;
  memset(b->ring, 0, sizeof(b->ring));
  memset(&b->boot, 0, sizeof(b->boot));
  b->boot.lat_min_ms = UINT32_MAX;
}

const char *vg_fs_result_name(enum vg_fs_result result)
{
  switch (result)
    {
      case VG_FS_OK:
        return "ok";

      case VG_FS_CRC:
        return "crc";

      case VG_FS_TIMEOUT:
        return "timeout";

      case VG_FS_ECHO:
        return "echo";

      default:
        return "other";
    }
}

void vg_fs_init(void)
{
  if (g_inited)
    {
      return;
    }

  memset(g_buckets, 0, sizeof(g_buckets));
  g_inited = 1;
}

int vg_fs_record(uint8_t slave, enum vg_fs_result result, uint32_t latency_ms)
{
  struct vg_fs_bucket *b;
  struct vg_fs_event *ev;

  if (!g_inited)
    {
      vg_fs_init();
    }

  b = find_bucket(slave, true);
  if (b == NULL)
    {
      return -ENOMEM;
    }

  ev = &b->ring[b->pos];
  ev->result = result;
  ev->latency_ms = latency_ms;
  b->pos = (uint16_t)((b->pos + 1) % VG_FS_WINDOW);
  if (b->len < VG_FS_WINDOW)
    {
      b->len++;
    }

  b->boot.total++;
  switch (result)
    {
      case VG_FS_OK:
        b->boot.ok++;
        if (latency_ms < b->boot.lat_min_ms)
          {
            b->boot.lat_min_ms = latency_ms;
          }

        if (latency_ms > b->boot.lat_max_ms)
          {
            b->boot.lat_max_ms = latency_ms;
          }

        b->boot.lat_sum_ms += latency_ms;
        break;

      case VG_FS_CRC:
        b->boot.crc_err++;
        break;

      case VG_FS_TIMEOUT:
        b->boot.timeout++;
        break;

      case VG_FS_ECHO:
        b->boot.echo++;
        break;

      default:
        b->boot.other++;
        break;
    }

  return 0;
}

int vg_fs_inject(uint8_t slave, enum vg_fs_result result, uint32_t latency_ms)
{
  return vg_fs_record(slave, result, latency_ms);
}

static void aggregate_bucket(const struct vg_fs_bucket *b,
                             struct vg_fs_summary *out)
{
  unsigned int i;
  uint32_t lat_sum = 0;
  uint32_t lat_cnt = 0;

  memset(out, 0, sizeof(*out));
  out->lat_min_ms = UINT32_MAX;

  if (b == NULL || b->len == 0)
    {
      out->lat_min_ms = 0;
      return;
    }

  for (i = 0; i < b->len; i++)
    {
      unsigned int idx = (unsigned int)((b->pos + VG_FS_WINDOW - b->len + i)
                                        % VG_FS_WINDOW);
      const struct vg_fs_event *ev = &b->ring[idx];

      out->total++;
      switch (ev->result)
        {
          case VG_FS_OK:
            out->ok++;
            if (ev->latency_ms < out->lat_min_ms)
              {
                out->lat_min_ms = ev->latency_ms;
              }

            if (ev->latency_ms > out->lat_max_ms)
              {
                out->lat_max_ms = ev->latency_ms;
              }

            lat_sum += ev->latency_ms;
            lat_cnt++;
            break;

          case VG_FS_CRC:
            out->crc_err++;
            break;

          case VG_FS_TIMEOUT:
            out->timeout++;
            break;

          case VG_FS_ECHO:
            out->echo++;
            break;

          default:
            out->other++;
            break;
        }
    }

  if (lat_cnt == 0)
    {
      out->lat_min_ms = 0;
      out->lat_max_ms = 0;
      out->lat_avg_ms = 0;
    }
  else
    {
      out->lat_avg_ms = lat_sum / lat_cnt;
    }
}

int vg_fs_boot_summary(uint8_t slave, struct vg_fs_boot_summary *out)
{
  struct vg_fs_bucket *b;

  if (out == NULL)
    {
      return -EINVAL;
    }

  if (!g_inited)
    {
      vg_fs_init();
    }

  memset(out, 0, sizeof(*out));
  b = find_bucket(slave, false);
  if (b == NULL)
    {
      return 0;
    }

  *out = b->boot;
  if (out->ok == 0)
    {
      out->lat_min_ms = 0;
      out->lat_max_ms = 0;
      out->lat_sum_ms = 0;
    }
  else if (out->lat_min_ms == UINT32_MAX)
    {
      out->lat_min_ms = 0;
    }

  return 0;
}

uint8_t vg_fs_slave_at(int idx)
{
  int n = 0;
  int i;

  if (!g_inited)
    {
      vg_fs_init();
    }

  if (idx < 0)
    {
      return 0;
    }

  for (i = 0; i < VG_FS_MAX_SLAVES; i++)
    {
      if (g_buckets[i].slave == 0)
        {
          continue;
        }

      if (n == idx)
        {
          return g_buckets[i].slave;
        }

      n++;
    }

  return 0;
}

int vg_fs_summary(uint8_t slave, struct vg_fs_summary *out)
{
  struct vg_fs_bucket *b;

  if (out == NULL)
    {
      return -EINVAL;
    }

  if (!g_inited)
    {
      vg_fs_init();
    }

  b = find_bucket(slave, false);
  aggregate_bucket(b, out);
  return 0;
}

int vg_fs_reset(uint8_t slave)
{
  int i;

  if (!g_inited)
    {
      vg_fs_init();
    }

  if (slave == 0)
    {
      for (i = 0; i < VG_FS_MAX_SLAVES; i++)
        {
          g_buckets[i].slave = 0;
          bucket_reset(&g_buckets[i]);
        }

      return 0;
    }

  for (i = 0; i < VG_FS_MAX_SLAVES; i++)
    {
      if (g_buckets[i].slave == slave)
        {
          g_buckets[i].slave = 0;
          bucket_reset(&g_buckets[i]);
          return 0;
        }
    }

  return 0;
}
