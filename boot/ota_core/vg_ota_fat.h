#ifndef __VG_OTA_FAT_H
#define __VG_OTA_FAT_H

#include <stdint.h>

#define VG_OTA_FAT_BPB_BYTES     512u
#define VG_OTA_FAT_SECTOR_BYTES  512u
#define VG_OTA_FAT_NAME_BYTES    11u
#define VG_OTA_FAT_DIR_SECTORS   256u

enum vg_ota_fat_type_e
{
  VG_OTA_FAT_UNKNOWN = 0,
  VG_OTA_FAT12,
  VG_OTA_FAT16,
  VG_OTA_FAT32,
  VG_OTA_FAT_EXFAT
};

struct vg_ota_fat_info
{
  enum vg_ota_fat_type_e type;
  uint16_t bytes_per_sector;
  uint8_t  sectors_per_cluster;
  uint16_t reserved_sectors;
  uint8_t  num_fats;
  uint16_t root_entries;
  uint32_t total_sectors;
  uint32_t fat_size_sectors;
  uint32_t root_cluster;
  uint32_t cluster_count;
};

int vg_ota_parse_fat_bpb(const uint8_t bpb[VG_OTA_FAT_BPB_BYTES],
                         struct vg_ota_fat_info *out);
const char *vg_ota_fat_type_name(enum vg_ota_fat_type_e type);

/* Reads 512-byte device sectors.  Returns 0 on success. */
typedef int (*vg_ota_block_read_fn)(void *ctx, uint32_t sector,
                                    uint8_t *buf, uint32_t count);

struct vg_ota_fat_volume
{
  struct vg_ota_fat_info info;
  vg_ota_block_read_fn read;
  void *ctx;
  uint32_t device_sectors;
  uint32_t fat_start;
  uint32_t root_start;
  uint32_t root_sectors;
  uint32_t first_data;
  uint32_t max_cluster;
  uint32_t fat_cache_sector;
  uint8_t  fat_cache_valid;
  uint8_t  fat_cache[VG_OTA_FAT_SECTOR_BYTES];
};

struct vg_ota_fat_entry
{
  uint8_t  name[VG_OTA_FAT_NAME_BYTES];
  uint8_t  attr;
  uint32_t first_cluster;
  uint32_t size;
};

/* device_sectors may be 0 when the capacity is unknown; the BPB total is
 * then trusted.  Only FAT16 and FAT32 with 512-byte sectors are accepted.
 */
int vg_ota_fat_mount(struct vg_ota_fat_volume *vol,
                     vg_ota_block_read_fn read, void *ctx,
                     uint32_t device_sectors);

/* dir_cluster 0 means the root directory.  The 11-byte name must match the
 * on-disk 8.3 form exactly.  Long names, labels and deleted entries are
 * skipped.  Each lookup scans at most 128 KiB of directory data so corrupt
 * or unexpectedly large directories cannot delay boot indefinitely.
 * Returns -ENOENT when the name is absent.
 */
int vg_ota_fat_find(struct vg_ota_fat_volume *vol,
                    const char name[VG_OTA_FAT_NAME_BYTES],
                    uint32_t dir_cluster, struct vg_ota_fat_entry *out);

/* Reads a byte range of a file.  The range must lie inside entry->size;
 * short or looping cluster chains are reported as -EIO.
 */
int vg_ota_fat_read(struct vg_ota_fat_volume *vol,
                    const struct vg_ota_fat_entry *entry,
                    uint32_t offset, uint8_t *buf, uint32_t len);

#endif
