#include <errno.h>

#include "vg_ota_fat.h"

#define FAT_ENTRY_FREE      0u
#define FAT16_EOC_MIN       0xfff8u
#define FAT16_BAD           0xfff7u
#define FAT16_RESERVED_MIN  0xfff0u
#define FAT32_EOC_MIN       0x0ffffff8u
#define FAT32_BAD           0x0ffffff7u

static uint16_t rd16(const uint8_t *p)
{
  return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t rd32(const uint8_t *p)
{
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
         ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static int is_power_of_two(uint32_t v)
{
  return v != 0 && (v & (v - 1)) == 0;
}

static int bytes_equal(const uint8_t *a, const uint8_t *b, uint32_t len)
{
  while (len-- > 0)
    {
      if (*a++ != *b++)
        {
          return 0;
        }
    }

  return 1;
}

static void bytes_copy(uint8_t *dst, const uint8_t *src, uint32_t len)
{
  while (len-- > 0)
    {
      *dst++ = *src++;
    }
}

static int add_u32(uint32_t a, uint32_t b, uint32_t *out)
{
  if (a > 0xffffffffu - b)
    {
      return -EINVAL;
    }

  *out = a + b;
  return 0;
}

int vg_ota_parse_fat_bpb(const uint8_t bpb[VG_OTA_FAT_BPB_BYTES],
                         struct vg_ota_fat_info *out)
{
  uint32_t total16;
  uint32_t total32;
  uint32_t fat16;
  uint32_t fat32;
  uint32_t fat_size;
  uint32_t root_dir_bytes;
  uint32_t root_dir_sectors;
  uint32_t fat_area;
  uint32_t first_data_sector;
  uint32_t data_sectors;

  if (bpb == 0 || out == 0)
    {
      return -EINVAL;
    }

  out->type = VG_OTA_FAT_UNKNOWN;

  if (bpb[3] == 'E' && bpb[4] == 'X' && bpb[5] == 'F' &&
      bpb[6] == 'A' && bpb[7] == 'T')
    {
      out->type = VG_OTA_FAT_EXFAT;
      return -ENOTSUP;
    }

  if (bpb[510] != 0x55 || bpb[511] != 0xaa)
    {
      return -EINVAL;
    }

  out->bytes_per_sector = rd16(&bpb[11]);
  out->sectors_per_cluster = bpb[13];
  out->reserved_sectors = rd16(&bpb[14]);
  out->num_fats = bpb[16];
  out->root_entries = rd16(&bpb[17]);
  total16 = rd16(&bpb[19]);
  fat16 = rd16(&bpb[22]);
  total32 = rd32(&bpb[32]);

  /* Offset 36 is BPB_FATSz32 only in a FAT32 volume.  A FAT16 BPB uses
   * the same bytes for drive number, signature and volume id, so it must
   * not be read as a FAT size there.
   */
  fat32 = fat16 == 0 ? rd32(&bpb[36]) : 0;
  out->root_cluster = fat16 == 0 ? rd32(&bpb[44]) : 0;

  if (out->bytes_per_sector != 512 &&
      out->bytes_per_sector != 1024 &&
      out->bytes_per_sector != 2048 &&
      out->bytes_per_sector != 4096)
    {
      return -EINVAL;
    }

  if (!is_power_of_two(out->sectors_per_cluster) ||
      out->sectors_per_cluster == 0 || out->sectors_per_cluster > 128)
    {
      return -EINVAL;
    }

  if (out->num_fats == 0 || out->reserved_sectors == 0)
    {
      return -EINVAL;
    }

  out->total_sectors = total16 != 0 ? total16 : total32;
  fat_size = fat16 != 0 ? fat16 : fat32;
  out->fat_size_sectors = fat_size;

  if (out->total_sectors == 0 || fat_size == 0 ||
      fat_size >= out->total_sectors)
    {
      return -EINVAL;
    }

  if (out->num_fats > out->total_sectors / fat_size)
    {
      return -EINVAL;
    }

  fat_area = (uint32_t)out->num_fats * fat_size;
  if (add_u32(out->reserved_sectors, fat_area, &first_data_sector) != 0)
    {
      return -EINVAL;
    }

  root_dir_bytes = (uint32_t)out->root_entries * 32u;
  root_dir_sectors = (root_dir_bytes + out->bytes_per_sector - 1u) /
                     out->bytes_per_sector;
  if (add_u32(first_data_sector, root_dir_sectors, &first_data_sector) != 0 ||
      first_data_sector >= out->total_sectors)
    {
      return -EINVAL;
    }

  data_sectors = out->total_sectors - first_data_sector;
  out->cluster_count = data_sectors / out->sectors_per_cluster;

  if (fat16 == 0 && fat32 != 0)
    {
      out->type = VG_OTA_FAT32;
    }
  else if (out->cluster_count < 4085)
    {
      out->type = VG_OTA_FAT12;
    }
  else if (out->cluster_count < 65525)
    {
      out->type = VG_OTA_FAT16;
    }
  else
    {
      out->type = VG_OTA_FAT32;
    }

  return 0;
}

const char *vg_ota_fat_type_name(enum vg_ota_fat_type_e type)
{
  switch (type)
    {
      case VG_OTA_FAT12:
        return "FAT12";
      case VG_OTA_FAT16:
        return "FAT16";
      case VG_OTA_FAT32:
        return "FAT32";
      case VG_OTA_FAT_EXFAT:
        return "exFAT";
      default:
        return "unknown";
    }
}

static int volume_sector_ok(const struct vg_ota_fat_volume *vol,
                            uint32_t sector)
{
  if (sector >= vol->info.total_sectors)
    {
      return 0;
    }

  if (vol->device_sectors != 0 && sector >= vol->device_sectors)
    {
      return 0;
    }

  return 1;
}

static int volume_read_sector(const struct vg_ota_fat_volume *vol,
                              uint32_t sector, uint8_t *buf)
{
  if (!volume_sector_ok(vol, sector))
    {
      return -EIO;
    }

  return vol->read(vol->ctx, sector, buf, 1);
}

int vg_ota_fat_mount(struct vg_ota_fat_volume *vol,
                     vg_ota_block_read_fn read, void *ctx,
                     uint32_t device_sectors)
{
  uint8_t bpb[VG_OTA_FAT_BPB_BYTES];
  uint32_t root_dir_bytes;
  uint32_t fat_area;
  uint32_t data_end;
  int ret;

  if (vol == 0 || read == 0)
    {
      return -EINVAL;
    }

  vol->read = read;
  vol->ctx = ctx;
  vol->device_sectors = device_sectors;
  vol->fat_cache_valid = 0;

  if (device_sectors != 0 && device_sectors < 1)
    {
      return -EINVAL;
    }

  ret = read(ctx, 0, bpb, 1);
  if (ret != 0)
    {
      return -EIO;
    }

  ret = vg_ota_parse_fat_bpb(bpb, &vol->info);
  if (ret != 0)
    {
      return ret;
    }

  if (vol->info.type != VG_OTA_FAT16 && vol->info.type != VG_OTA_FAT32)
    {
      return -ENOTSUP;
    }

  if (vol->info.bytes_per_sector != VG_OTA_FAT_SECTOR_BYTES ||
      vol->info.num_fats == 0 || vol->info.num_fats > 4 ||
      vol->info.cluster_count == 0)
    {
      return -EINVAL;
    }

  if (vol->device_sectors != 0 &&
      vol->info.total_sectors > vol->device_sectors)
    {
      return -EINVAL;
    }

  vol->fat_start = vol->info.reserved_sectors;
  fat_area = (uint32_t)vol->info.num_fats * vol->info.fat_size_sectors;
  if (add_u32(vol->fat_start, fat_area, &vol->root_start) != 0)
    {
      return -EINVAL;
    }

  root_dir_bytes = (uint32_t)vol->info.root_entries * 32u;
  vol->root_sectors = (root_dir_bytes + VG_OTA_FAT_SECTOR_BYTES - 1u) /
                      VG_OTA_FAT_SECTOR_BYTES;

  if (add_u32(vol->root_start, vol->root_sectors, &vol->first_data) != 0)
    {
      return -EINVAL;
    }

  vol->max_cluster = vol->info.cluster_count + 1u;

  if (vol->info.type == VG_OTA_FAT32 && vol->max_cluster >= 0x0ffffff0u)
    {
      return -EINVAL;
    }

  /* Every data cluster needs an entry in each FAT.  Do not interpret a
   * directory or data sector as a FAT entry when the BPB table is too short.
   */
  if (vol->max_cluster /
      (VG_OTA_FAT_SECTOR_BYTES /
       (vol->info.type == VG_OTA_FAT32 ? 4u : 2u)) >=
      vol->info.fat_size_sectors)
    {
      return -EINVAL;
    }

  if (vol->first_data >= vol->info.total_sectors ||
      vol->info.cluster_count >
        (vol->info.total_sectors - vol->first_data) /
        vol->info.sectors_per_cluster)
    {
      return -EINVAL;
    }

  if (vol->info.type == VG_OTA_FAT16)
    {
      if (vol->info.root_entries == 0 || vol->root_sectors == 0)
        {
          return -EINVAL;
        }
    }
  else
    {
      if (vol->info.root_entries != 0 ||
          vol->info.root_cluster < 2 ||
          vol->info.root_cluster > vol->max_cluster)
        {
          return -EINVAL;
        }
    }

  data_end = vol->first_data +
             vol->info.cluster_count * vol->info.sectors_per_cluster;
  if (data_end > vol->info.total_sectors)
    {
      return -EINVAL;
    }

  return 0;
}

static int fat_next_cluster(struct vg_ota_fat_volume *vol, uint32_t cluster,
                            uint32_t *next, int *eoc)
{
  uint32_t entry_size = vol->info.type == VG_OTA_FAT32 ? 4u : 2u;
  uint32_t byte_off;
  uint32_t sector;
  uint32_t offset;
  uint32_t value;
  int ret;

  *eoc = 0;

  if (cluster < 2 || cluster > vol->max_cluster)
    {
      return -EIO;
    }

  byte_off = cluster * entry_size;
  sector = vol->fat_start + byte_off / VG_OTA_FAT_SECTOR_BYTES;
  offset = byte_off % VG_OTA_FAT_SECTOR_BYTES;

  if (byte_off / VG_OTA_FAT_SECTOR_BYTES >= vol->info.fat_size_sectors ||
      !volume_sector_ok(vol, sector))
    {
      return -EIO;
    }

  if (!vol->fat_cache_valid || vol->fat_cache_sector != sector)
    {
      ret = vol->read(vol->ctx, sector, vol->fat_cache, 1);
      if (ret != 0)
        {
          return -EIO;
        }

      vol->fat_cache_sector = sector;
      vol->fat_cache_valid = 1;
    }

  if (vol->info.type == VG_OTA_FAT16)
    {
      value = rd16(&vol->fat_cache[offset]);
      if (value >= FAT16_EOC_MIN)
        {
          *eoc = 1;
          return 0;
        }

      if (value == FAT16_BAD || value >= FAT16_RESERVED_MIN ||
          value <= FAT_ENTRY_FREE + 1u)
        {
          return -EIO;
        }
    }
  else
    {
      value = rd32(&vol->fat_cache[offset]) & 0x0fffffffu;
      if (value >= FAT32_EOC_MIN)
        {
          *eoc = 1;
          return 0;
        }

      if (value == FAT32_BAD || value < 2u)
        {
          return -EIO;
        }
    }

  if (value < 2 || value > vol->max_cluster)
    {
      return -EIO;
    }

  *next = value;
  return 0;
}

/* Brent cycle detection uses constant memory and no extra storage reads. */
struct chain_guard
{
  uint32_t anchor;
  uint32_t power;
  uint32_t length;
};

static int chain_advance(struct chain_guard *guard, uint32_t next)
{
  if (next == guard->anchor)
    {
      return -EIO;
    }

  if (++guard->length == guard->power)
    {
      guard->anchor = next;
      guard->power *= 2u;
      guard->length = 0;
    }

  return 0;
}

static int dir_entry_parse(const struct vg_ota_fat_volume *vol,
                           const uint8_t *raw,
                           const char name[VG_OTA_FAT_NAME_BYTES],
                           struct vg_ota_fat_entry *out)
{
  uint8_t attr = raw[11];
  uint32_t first;
  uint32_t size;

  if (!bytes_equal(raw, (const uint8_t *)name, VG_OTA_FAT_NAME_BYTES))
    {
      return 0;
    }

  if ((attr & 0x10) != 0)
    {
      if (rd32(&raw[28]) != 0)
        {
          return -EIO;
        }
    }

  first = rd16(&raw[26]);
  if (vol->info.type == VG_OTA_FAT32)
    {
      first |= (uint32_t)rd16(&raw[20]) << 16;
    }

  size = rd32(&raw[28]);

  if (first == 0)
    {
      if (size != 0)
        {
          return -EIO;
        }
    }
  else if (first < 2 || first > vol->max_cluster)
    {
      return -EIO;
    }

  if ((attr & 0x10) != 0 && first < 2)
    {
      return -EIO;
    }

  bytes_copy(out->name, raw, VG_OTA_FAT_NAME_BYTES);
  out->attr = attr;
  out->first_cluster = first;
  out->size = size;
  return 1;
}

static int dir_scan_sector(struct vg_ota_fat_volume *vol, uint32_t sector,
                           const char name[VG_OTA_FAT_NAME_BYTES],
                           struct vg_ota_fat_entry *out)
{
  uint8_t buf[VG_OTA_FAT_SECTOR_BYTES];
  uint32_t i;
  int ret;

  ret = volume_read_sector(vol, sector, buf);
  if (ret != 0)
    {
      return ret;
    }

  for (i = 0; i + 32u <= VG_OTA_FAT_SECTOR_BYTES; i += 32u)
    {
      uint8_t first = buf[i];

      if (first == 0)
        {
          return -ENOENT;
        }

      if (first == 0xe5)
        {
          continue;
        }

      if (buf[i + 11] == 0x0f || (buf[i + 11] & 0x08) != 0)
        {
          continue;
        }

      ret = dir_entry_parse(vol, &buf[i], name, out);
      if (ret != 0)
        {
          return ret;
        }
    }

  return 0;
}

static int dir_scan_cluster_chain(struct vg_ota_fat_volume *vol,
                                  uint32_t first_cluster,
                                  const char name[VG_OTA_FAT_NAME_BYTES],
                                  struct vg_ota_fat_entry *out)
{
  uint32_t cluster = first_cluster;
  uint32_t steps = 0;
  uint32_t sectors = 0;
  struct chain_guard guard = { first_cluster, 1u, 0u };
  uint32_t i;
  int ret;

  for (; ; )
    {
      uint32_t base = vol->first_data +
                      (cluster - 2u) * vol->info.sectors_per_cluster;

      for (i = 0; i < vol->info.sectors_per_cluster; i++)
        {
          if (sectors++ >= VG_OTA_FAT_DIR_SECTORS)
            {
              return -EIO;
            }

          ret = dir_scan_sector(vol, base + i, name, out);
          if (ret == 1 || ret == -ENOENT)
            {
              return ret;
            }

          if (ret != 0)
            {
              return ret;
            }
        }

      if (++steps > vol->max_cluster)
        {
          return -EIO;
        }

      {
        int eoc = 0;
        uint32_t next = 0;

        ret = fat_next_cluster(vol, cluster, &next, &eoc);
        if (ret != 0)
          {
            return ret;
          }

        if (eoc)
          {
            return -ENOENT;
          }

        if (chain_advance(&guard, next) != 0)
          {
            return -EIO;
          }

        cluster = next;
      }
    }
}

int vg_ota_fat_find(struct vg_ota_fat_volume *vol,
                    const char name[VG_OTA_FAT_NAME_BYTES],
                    uint32_t dir_cluster, struct vg_ota_fat_entry *out)
{
  uint32_t sector;
  int ret;

  if (vol == 0 || name == 0 || out == 0)
    {
      return -EINVAL;
    }

  if (dir_cluster == 0)
    {
      if (vol->info.type == VG_OTA_FAT16)
        {
          for (sector = vol->root_start;
               sector < vol->root_start + vol->root_sectors; sector++)
            {
              if (sector - vol->root_start >= VG_OTA_FAT_DIR_SECTORS)
                {
                  return -EIO;
                }

              ret = dir_scan_sector(vol, sector, name, out);
              if (ret == 1)
                {
                  return 0;
                }

              if (ret == -ENOENT)
                {
                  return -ENOENT;
                }

              if (ret != 0)
                {
                  return ret;
                }
            }

          return -ENOENT;
        }

      dir_cluster = vol->info.root_cluster;
    }

  if (dir_cluster < 2 || dir_cluster > vol->max_cluster)
    {
      return -EINVAL;
    }

  ret = dir_scan_cluster_chain(vol, dir_cluster, name, out);
  return ret == 1 ? 0 : ret;
}

int vg_ota_fat_read(struct vg_ota_fat_volume *vol,
                    const struct vg_ota_fat_entry *entry,
                    uint32_t offset, uint8_t *buf, uint32_t len)
{
  uint32_t cluster_bytes;
  uint32_t cluster;
  uint32_t skip;
  uint32_t within;
  uint32_t remaining;
  uint32_t steps = 0;
  struct chain_guard guard;
  uint8_t sector_buf[VG_OTA_FAT_SECTOR_BYTES];
  int ret;

  if (vol == 0 || entry == 0 || (buf == 0 && len != 0))
    {
      return -EINVAL;
    }

  if (len == 0)
    {
      return 0;
    }

  if (offset > entry->size || len > entry->size - offset)
    {
      return -EINVAL;
    }

  if (entry->first_cluster < 2 || entry->first_cluster > vol->max_cluster)
    {
      return -EIO;
    }

  cluster_bytes = (uint32_t)vol->info.sectors_per_cluster *
                  VG_OTA_FAT_SECTOR_BYTES;
  skip = offset / cluster_bytes;
  within = offset % cluster_bytes;
  cluster = entry->first_cluster;
  guard.anchor = cluster;
  guard.power = 1u;
  guard.length = 0;

  while (skip-- > 0)
    {
      int eoc = 0;
      uint32_t next = 0;

      ret = fat_next_cluster(vol, cluster, &next, &eoc);
      if (ret != 0 || eoc || chain_advance(&guard, next) != 0)
        {
          return -EIO;
        }

      cluster = next;
      if (++steps > vol->max_cluster)
        {
          return -EIO;
        }
    }

  remaining = len;

  for (; ; )
    {
      uint32_t chunk = cluster_bytes - within;
      uint32_t base;
      uint32_t byte_off;
      uint32_t todo;

      if (chunk > remaining)
        {
          chunk = remaining;
        }

      base = vol->first_data +
             (cluster - 2u) * vol->info.sectors_per_cluster;
      byte_off = within;
      todo = chunk;

      while (todo > 0)
        {
          uint32_t sector = base + byte_off / VG_OTA_FAT_SECTOR_BYTES;
          uint32_t in_sector = byte_off % VG_OTA_FAT_SECTOR_BYTES;
          uint32_t copy = VG_OTA_FAT_SECTOR_BYTES - in_sector;

          if (copy > todo)
            {
              copy = todo;
            }

          if (in_sector == 0 && copy == VG_OTA_FAT_SECTOR_BYTES)
            {
              ret = volume_read_sector(vol, sector, buf);
            }
          else
            {
              ret = volume_read_sector(vol, sector, sector_buf);
            }

          if (ret != 0)
            {
              return ret;
            }

          if (buf != 0 && in_sector == 0 &&
              copy == VG_OTA_FAT_SECTOR_BYTES)
            {
              buf += copy;
            }
          else if (buf != 0)
            {
              uint32_t i;

              for (i = 0; i < copy; i++)
                {
                  buf[i] = sector_buf[in_sector + i];
                }

              buf += copy;
            }

          todo -= copy;
          byte_off += copy;
        }

      remaining -= chunk;
      within = 0;

      if (remaining == 0)
        {
          if (offset + len == entry->size)
            {
              int eoc = 0;
              uint32_t next = 0;

              ret = fat_next_cluster(vol, cluster, &next, &eoc);
              if (ret != 0 || !eoc)
                {
                  return -EIO;
                }
            }

          return 0;
        }

      {
        int eoc = 0;
        uint32_t next = 0;

        ret = fat_next_cluster(vol, cluster, &next, &eoc);
        if (ret != 0 || eoc || chain_advance(&guard, next) != 0)
          {
            return -EIO;
          }

        cluster = next;
        if (++steps > vol->max_cluster)
          {
            return -EIO;
          }
      }
    }
}

int vg_ota_fat_file_sector(struct vg_ota_fat_volume *vol,
                           const struct vg_ota_fat_entry *entry,
                           uint32_t offset, uint32_t *sector)
{
  uint32_t cluster, skip, cluster_bytes;
  struct chain_guard guard;
  if (!vol || !entry || !sector || offset % 512u ||
      offset >= entry->size || entry->first_cluster < 2 ||
      entry->first_cluster > vol->max_cluster) return -EINVAL;
  cluster_bytes=vol->info.sectors_per_cluster*512u;
  skip=offset/cluster_bytes;
  if (skip>=vol->info.cluster_count) return -EIO;
  cluster=entry->first_cluster;
  guard.anchor=cluster; guard.power=1; guard.length=0;
  while (skip--)
    {
      int eoc=0;
      uint32_t next=0;
      if (fat_next_cluster(vol,cluster,&next,&eoc) || eoc ||
          chain_advance(&guard,next)) return -EIO;
      cluster=next;
    }
  *sector=vol->first_data+(cluster-2u)*vol->info.sectors_per_cluster+
          (offset%cluster_bytes)/512u;
  return volume_sector_ok(vol,*sector) ? 0 : -EIO;
}
