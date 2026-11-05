/****************************************************************************
 * app/velaguard/vg_config_file_source.c
 ****************************************************************************/

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>

#include "vg_config_file_source.h"
#include "vg_discover.h"
#include "../../boot/ota_core/vg_ota_sha256.h"

static int copy_path(char *out, size_t cap, const char *value)
{
  size_t length;

  if (out == NULL || value == NULL || value[0] == '\0')
    {
      return -EINVAL;
    }

  length = strlen(value);
  if (length >= cap)
    {
      return -ENAMETOOLONG;
    }

  memcpy(out, value, length + 1);
  return 0;
}

int vg_config_file_source_init(struct vg_config_file_source *source,
                               const char *points_path,
                               const char *store_root)
{
  int ret;

  if (source == NULL)
    {
      return -EINVAL;
    }

  memset(source, 0, sizeof(*source));
  ret = copy_path(source->points_path, sizeof(source->points_path),
                  points_path);
  if (ret != 0)
    {
      return ret;
    }

  return copy_path(source->store_root, sizeof(source->store_root),
                   store_root);
}

static void digest_to_hex(const uint8_t digest[32], char out[65])
{
  static const char digits[] = "0123456789abcdef";
  size_t i;

  for (i = 0; i < 32; i++)
    {
      out[i * 2] = digits[digest[i] >> 4];
      out[i * 2 + 1] = digits[digest[i] & 0x0f];
    }

  out[64] = '\0';
}

static int hash_file(const char *path, char out[VG_CONFIG_HASH_LEN])
{
  struct vg_ota_sha256 sha;
  uint8_t buf[512];
  uint8_t digest[32];
  size_t total = 0;
  ssize_t got;
  int fd;

  fd = open(path, O_RDONLY);
  if (fd < 0)
    {
      return -errno;
    }

  vg_ota_sha256_init(&sha);
  for (;;)
    {
      got = read(fd, buf, sizeof(buf));
      if (got < 0 && errno == EINTR)
        {
          continue;
        }
      if (got < 0)
        {
          int ret = -errno;
          close(fd);
          return ret;
        }
      if (got == 0)
        {
          break;
        }
      total += (size_t)got;
      if (total >= VG_POINT_TABLE_JSON_MAX)
        {
          close(fd);
          return -EFBIG;
        }
      vg_ota_sha256_update(&sha, buf, (uint32_t)got);
    }

  if (close(fd) != 0)
    {
      return -errno;
    }

  vg_ota_sha256_final(&sha, digest);
  digest_to_hex(digest, out);
  memset(digest, 0, sizeof(digest));
  memset(&sha, 0, sizeof(sha));
  return 0;
}

static void append_slave(struct vg_config_table_snapshot *out, uint8_t addr)
{
  size_t i;

  if (addr < 1 || addr > 247)
    {
      return;
    }

  for (i = 0; i < out->slave_count; i++)
    {
      if (out->slave_addresses[i] == addr)
        {
          return;
        }
    }

  if (out->slave_count < VG_DISCOVER_MAX_SLAVES)
    {
      out->slave_addresses[out->slave_count++] = addr;
    }
}

int vg_config_file_source_load(void *ctx,
                               struct vg_config_table_snapshot *out)
{
  struct vg_config_file_source *source = ctx;
  struct vg_discover_summary table;
  int ret;
  int i;

  if (source == NULL || out == NULL)
    {
      return -EINVAL;
    }

  memset(out, 0, sizeof(*out));
  memset(&table, 0, sizeof(table));
  ret = vg_point_table_read(&table, source->points_path);
  if (ret == -ENOENT)
    {
      ret = vg_point_store_root_ok(source->store_root);
      out->state = ret == 0 ? VG_CONFIG_TABLE_EMPTY :
                              VG_CONFIG_TABLE_UNAVAILABLE;
      return ret == 0 ? 0 : ret;
    }

  if (ret != 0 || table.n_points < 0 ||
      table.n_points > VG_DISCOVER_MAX_POINTS)
    {
      out->state = VG_CONFIG_TABLE_UNAVAILABLE;
      return ret != 0 ? ret : -EINVAL;
    }

  out->current = table;
  if (table.n_points == 0)
    {
      out->state = VG_CONFIG_TABLE_EMPTY;
      return 0;
    }

  out->state = VG_CONFIG_TABLE_VALID;
  out->point_count = (uint32_t)table.n_points;
  for (i = 0; i < table.n_points; i++)
    {
      memcpy(out->point_ids[i], table.points[i].id,
             sizeof(out->point_ids[i]));
      append_slave(out, table.points[i].addr);
    }

  ret = hash_file(source->points_path, out->base_hash);
  if (ret != 0)
    {
      memset(out, 0, sizeof(*out));
      out->state = VG_CONFIG_TABLE_UNAVAILABLE;
      return ret;
    }

  out->has_base_hash = true;
  return 0;
}
