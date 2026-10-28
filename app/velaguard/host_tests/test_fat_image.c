#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "test_fat_image.h"

#define SECTOR 512u

static void put16(uint8_t *p, uint16_t v)
{
  p[0] = (uint8_t)v;
  p[1] = (uint8_t)(v >> 8);
}

static void put32(uint8_t *p, uint32_t v)
{
  p[0] = (uint8_t)v;
  p[1] = (uint8_t)(v >> 8);
  p[2] = (uint8_t)(v >> 16);
  p[3] = (uint8_t)(v >> 24);
}

static uint32_t div_up(uint32_t v, uint32_t d)
{
  return (v + d - 1u) / d;
}

static uint32_t cluster_bytes(const struct test_fat_image *img)
{
  return img->spc * SECTOR;
}

static uint8_t *cluster_ptr(const struct test_fat_image *img, uint32_t cluster)
{
  uint32_t sector = img->first_data + (cluster - 2u) * img->spc;

  return img->mem + sector * SECTOR;
}

int test_fat_init(struct test_fat_image *img, int fat32,
                  uint32_t cluster_count, uint8_t spc)
{
  uint32_t entries;
  uint32_t root_entries;
  uint32_t fat_bytes;
  uint32_t root_sectors;
  uint32_t total;

  memset(img, 0, sizeof(*img));
  img->is_fat32 = fat32 != 0;
  img->spc = spc;
  img->cluster_count = cluster_count;
  img->fat_start = 1;

  entries = cluster_count + 2u;
  fat_bytes = img->is_fat32 ? entries * 4u : entries * 2u;
  img->fat_sectors = div_up(fat_bytes, SECTOR);

  if (img->is_fat32)
    {
      root_entries = 0;
      root_sectors = 0;
    }
  else
    {
      root_entries = 512;
      root_sectors = div_up(root_entries * 32u, SECTOR);
    }

  img->root_start = img->fat_start + 2u * img->fat_sectors;
  img->root_sectors = root_sectors;
  img->first_data = img->root_start + root_sectors;
  total = img->first_data + cluster_count * spc;

  if (total >= 0x10000u)
    {
      if (img->is_fat32)
        {
          /* keep total32, fine */
        }
      else
        {
          return -1;
        }
    }

  img->mem = calloc(total, SECTOR);
  if (img->mem == 0)
    {
      return -1;
    }

  img->sectors = total;
  img->next_cluster = 2;

  {
    uint8_t *bpb = img->mem;

    bpb[0] = 0xeb;
    bpb[1] = 0x3c;
    bpb[2] = 0x90;
    memcpy(&bpb[3], "MSDOS5.0", 8);
    put16(&bpb[11], SECTOR);
    bpb[13] = spc;
    put16(&bpb[14], 1);
    bpb[16] = 2;
    put16(&bpb[17], (uint16_t)root_entries);
    put16(&bpb[19], !img->is_fat32 && total < 0x10000u ?
                       (uint16_t)total : 0);
    bpb[21] = 0xf8;
    put16(&bpb[22], img->is_fat32 ? 0 : (uint16_t)img->fat_sectors);
    put16(&bpb[24], 63);
    put16(&bpb[26], 255);
    put32(&bpb[28], 0);
    put32(&bpb[32], !img->is_fat32 && total < 0x10000u ? 0 : total);
    if (img->is_fat32)
      {
        put32(&bpb[36], img->fat_sectors);
        put32(&bpb[44], 2);
        bpb[66] = 0x29;
        memcpy(&bpb[71], "FAT32   ", 8);
        memcpy(&bpb[82], "FAT32   ", 8);
      }
    else
      {
        bpb[38] = 0x29;
        memcpy(&bpb[43], "NO NAME    ", 11);
        memcpy(&bpb[54], "FAT16   ", 8);
      }

    bpb[510] = 0x55;
    bpb[511] = 0xaa;
  }

  if (img->is_fat32)
    {
      test_fat_set_entry(img, 0, 0x0ffffff8u);
      test_fat_set_entry(img, 1, 0x0fffffffu);
      test_fat_set_entry(img, 2, 0x0fffffffu);
      img->next_cluster = 3;
    }
  else
    {
      test_fat_set_entry(img, 0, 0xfff8u);
      test_fat_set_entry(img, 1, 0xffffu);
    }

  return 0;
}

void test_fat_free(struct test_fat_image *img)
{
  free(img->mem);
  memset(img, 0, sizeof(*img));
}

void test_fat_set_entry(struct test_fat_image *img, uint32_t cluster,
                        uint32_t value)
{
  uint8_t *p = img->mem + img->fat_start * SECTOR +
               cluster * (img->is_fat32 ? 4u : 2u);

  if (img->is_fat32)
    {
      put32(p, value);
    }
  else
    {
      put16(p, (uint16_t)value);
    }
}

uint32_t test_fat_get_entry(const struct test_fat_image *img,
                            uint32_t cluster)
{
  const uint8_t *p = img->mem + img->fat_start * SECTOR +
                     cluster * (img->is_fat32 ? 4u : 2u);

  if (img->is_fat32)
    {
      return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
             ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
    }

  return (uint32_t)p[0] | ((uint32_t)p[1] << 8);
}

uint32_t test_fat_alloc_chain(struct test_fat_image *img, uint32_t clusters)
{
  uint32_t first;
  uint32_t i;

  if (clusters == 0 || clusters > img->cluster_count)
    {
      return 0;
    }

  if (img->next_cluster + clusters - 2u > img->cluster_count + 1u)
    {
      return 0;
    }

  first = img->next_cluster;

  for (i = 0; i < clusters; i++)
    {
      uint32_t cluster = first + i;
      uint32_t value;

      if (i + 1u < clusters)
        {
          value = cluster + 1u;
        }
      else
        {
          value = img->is_fat32 ? 0x0fffffffu : 0xffffu;
        }

      test_fat_set_entry(img, cluster, value);
    }

  img->next_cluster += clusters;
  return first;
}

int test_fat_set_chain(struct test_fat_image *img, uint32_t first,
                       uint32_t clusters)
{
  uint32_t i;

  if (clusters == 0 || first < 2 ||
      first + clusters - 2u > img->cluster_count + 1u)
    {
      return -1;
    }

  for (i = 0; i < clusters; i++)
    {
      uint32_t cluster = first + i;
      uint32_t value = (i + 1u < clusters) ? cluster + 1u :
                       (img->is_fat32 ? 0x0fffffffu : 0xffffu);

      test_fat_set_entry(img, cluster, value);
    }

  return 0;
}

static uint8_t *dir_entry_slot(struct test_fat_image *img,
                               uint32_t dir_cluster, uint32_t *sector_out,
                               uint32_t *offset_out)
{
  uint32_t sectors_per_dir = img->spc;

  if (dir_cluster == 0 && !img->is_fat32)
    {
      uint32_t s;

      for (s = 0; s < img->root_sectors; s++)
        {
          uint8_t *base = img->mem + (img->root_start + s) * SECTOR;
          uint32_t i;

          for (i = 0; i < SECTOR; i += 32u)
            {
              if (base[i] == 0 || base[i] == 0xe5)
                {
                  *sector_out = img->root_start + s;
                  *offset_out = i;
                  return &base[i];
                }
            }
        }

      return 0;
    }

  if (dir_cluster == 0)
    {
      dir_cluster = 2;
    }

  if (dir_cluster < 2)
    {
      return 0;
    }

  {
    uint8_t *base = cluster_ptr(img, dir_cluster);
    uint32_t i;

    (void)sectors_per_dir;

    for (i = 0; i < cluster_bytes(img); i += 32u)
      {
        if (base[i] == 0 || base[i] == 0xe5)
          {
            *offset_out = i;
            return &base[i];
          }
      }
  }

  return 0;
}

int test_fat_dir_add(struct test_fat_image *img, uint32_t dir_cluster,
                     const char name11[11], uint8_t attr,
                     uint32_t first_cluster, uint32_t size)
{
  uint32_t sector = 0;
  uint32_t offset = 0;
  uint8_t *entry = dir_entry_slot(img, dir_cluster, &sector, &offset);

  (void)sector;

  if (entry == 0)
    {
      return -1;
    }

  memcpy(entry, name11, 11);
  entry[11] = attr;
  entry[12] = 0;
  put16(&entry[20], (uint16_t)(first_cluster >> 16));
  put16(&entry[26], (uint16_t)first_cluster);
  put32(&entry[28], size);
  return 0;
}

int test_fat_add_file(struct test_fat_image *img, uint32_t dir_cluster,
                      const char name11[11], const uint8_t *data,
                      uint32_t len)
{
  uint32_t first = 0;
  uint32_t cb = cluster_bytes(img);

  if (len > 0)
    {
      uint32_t clusters = div_up(len, cb);
      uint32_t done = 0;
      uint32_t cluster;

      first = test_fat_alloc_chain(img, clusters);
      if (first == 0)
        {
          return -1;
        }

      cluster = first;
      while (done < len)
        {
          uint32_t chunk = len - done;

          if (chunk > cb)
            {
              chunk = cb;
            }

          memcpy(cluster_ptr(img, cluster), data + done, chunk);
          done += chunk;
          cluster = cluster + 1u;
        }
    }

  return test_fat_dir_add(img, dir_cluster, name11, 0x20, first, len);
}

uint8_t *test_fat_dir_base(const struct test_fat_image *img,
                           uint32_t dir_cluster)
{
  if (dir_cluster == 0)
    {
      dir_cluster = 2;
    }

  if (dir_cluster < 2)
    {
      return 0;
    }

  return cluster_ptr(img, dir_cluster);
}

uint8_t *test_fat_entry_raw(const struct test_fat_image *img,
                            uint32_t dir_cluster, const char name11[11])
{
  if (dir_cluster == 0 && !img->is_fat32)
    {
      uint32_t s;

      for (s = 0; s < img->root_sectors; s++)
        {
          uint8_t *base = img->mem + (img->root_start + s) * SECTOR;
          uint32_t i;

          for (i = 0; i < SECTOR; i += 32u)
            {
              if (base[i] == 0)
                {
                  return 0;
                }

              if ((base[i + 11] & 0x3f) == 0x0f)
                {
                  continue;
                }

              if (memcmp(&base[i], name11, 11) == 0)
                {
                  return &base[i];
                }
            }
        }

      return 0;
    }

  if (dir_cluster == 0)
    {
      dir_cluster = 2;
    }

  {
    uint8_t *base = cluster_ptr(img, dir_cluster);
    uint32_t i;

    for (i = 0; i < cluster_bytes(img); i += 32u)
      {
        if (base[i] == 0)
          {
            return 0;
          }

        if ((base[i + 11] & 0x3f) == 0x0f)
          {
            continue;
          }

        if (memcmp(&base[i], name11, 11) == 0)
          {
            return &base[i];
          }
      }
  }

  return 0;
}

int test_block_read(void *ctx, uint32_t sector, uint8_t *buf, uint32_t count)
{
  struct test_block_ctx *t = ctx;

  t->reads++;

  if (t->fail_enabled &&
      sector + count > t->fail_sector &&
      sector < t->fail_sector + t->fail_count)
    {
      return -EIO;
    }

  if (sector + count > t->img->sectors || sector + count < sector)
    {
      return -EIO;
    }

  memcpy(buf, t->img->mem + sector * SECTOR, count * SECTOR);
  return 0;
}
