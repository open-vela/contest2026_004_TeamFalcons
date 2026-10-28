#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "test_fat_image.h"
#include "vg_ota_fat.h"

#define NAME_VGOTA "VGOTA      "
#define NAME_BCB0  "BCB0    BIN"

struct fixture
{
  struct test_fat_image img;
  struct test_block_ctx block;
  struct vg_ota_fat_volume vol;
  uint32_t dir_cluster;
};

static void put16(uint8_t *p, uint16_t v)
{
  p[0] = (uint8_t)v;
  p[1] = (uint8_t)(v >> 8);
}

static void make_data(uint8_t *buf, uint32_t len)
{
  uint32_t i;

  for (i = 0; i < len; i++)
    {
      buf[i] = (uint8_t)(i * 7u + 3u);
    }
}

static void fixture_init(struct fixture *f, int fat32)
{
  memset(f, 0, sizeof(*f));
  assert(test_fat_init(&f->img, fat32, fat32 ? 300 : 5000, 1) == 0);
  f->dir_cluster = test_fat_alloc_chain(&f->img, 2);
  assert(f->dir_cluster >= 2);
  assert(test_fat_dir_add(&f->img, 0, NAME_VGOTA, 0x10,
                          f->dir_cluster, 0) == 0);

  f->block.img = &f->img;
  f->block.fail_enabled = 0;
}

static void fixture_mount(struct fixture *f)
{
  assert(vg_ota_fat_mount(&f->vol, test_block_read, &f->block,
                          f->img.sectors) == 0);
}

static void fixture_done(struct fixture *f)
{
  test_fat_free(&f->img);
}

static void test_mount_and_read(void)
{
  static const char name[11] = NAME_BCB0;
  struct fixture f;
  struct vg_ota_fat_entry dir;
  struct vg_ota_fat_entry file;
  uint8_t data[4096];
  uint8_t readback[4096];
  uint8_t part[1024];
  int fat32;

  for (fat32 = 0; fat32 <= 1; fat32++)
    {
      fixture_init(&f, fat32);
      make_data(data, sizeof(data));
      assert(test_fat_add_file(&f.img, f.dir_cluster, name, data,
                               sizeof(data)) == 0);
      fixture_mount(&f);

      assert(vg_ota_fat_find(&f.vol, NAME_VGOTA, 0, &dir) == 0);
      assert((dir.attr & 0x10) != 0);
      assert(dir.first_cluster == f.dir_cluster);

      assert(vg_ota_fat_find(&f.vol, name, f.dir_cluster, &file) == 0);
      assert(file.size == sizeof(data));
      assert(vg_ota_fat_read(&f.vol, &file, 0, readback,
                             sizeof(readback)) == 0);
      assert(memcmp(readback, data, sizeof(data)) == 0);

      assert(vg_ota_fat_read(&f.vol, &file, 512, part, sizeof(part)) == 0);
      assert(memcmp(part, data + 512, sizeof(part)) == 0);

      assert(vg_ota_fat_read(&f.vol, &file, 4095, part, 2) == -EINVAL);
      assert(vg_ota_fat_read(&f.vol, &file, 4096, part, 0) == 0);
      assert(vg_ota_fat_read(&f.vol, &file, 4096, part, 1) == -EINVAL);

      fixture_done(&f);
    }
}

static void test_missing_and_abnormal(void)
{
  static const char name[11] = NAME_BCB0;
  struct fixture f;
  struct vg_ota_fat_entry dir;
  struct vg_ota_fat_entry file;
  uint8_t data[4096];
  uint8_t *entry;
  uint8_t *base;
  uint32_t orig_first;

  fixture_init(&f, 0);
  make_data(data, sizeof(data));
  assert(test_fat_add_file(&f.img, f.dir_cluster, name, data,
                           sizeof(data)) == 0);
  fixture_mount(&f);

  assert(vg_ota_fat_find(&f.vol, "IMAGE   BIN", 0, &dir) == -ENOENT);
  assert(vg_ota_fat_find(&f.vol, "IMAGE   BIN", f.dir_cluster,
                         &file) == -ENOENT);

  entry = test_fat_entry_raw(&f.img, f.dir_cluster, name);
  assert(entry != 0);
  orig_first = (uint32_t)entry[26] | ((uint32_t)entry[27] << 8);
  put16(&entry[26], 0);
  assert(vg_ota_fat_find(&f.vol, name, f.dir_cluster, &file) == -EIO);
  put16(&entry[26], (uint16_t)orig_first);

  entry = test_fat_entry_raw(&f.img, f.dir_cluster, name);
  put16(&entry[26], 0x7000);
  assert(vg_ota_fat_find(&f.vol, name, f.dir_cluster, &file) == -EIO);
  put16(&entry[26], (uint16_t)orig_first);

  base = test_fat_dir_base(&f.img, f.dir_cluster);
  assert(base != 0);
  memset(base + 2 * 32, 0, 32);
  base[2 * 32] = 0x41;
  base[2 * 32 + 11] = 0x0f;
  assert(test_fat_dir_add(&f.img, f.dir_cluster, "IMAGE   BIN", 0x20,
                          12, 4096) == 0);
  assert(vg_ota_fat_find(&f.vol, "IMAGE   BIN", f.dir_cluster,
                         &file) == 0);

  fixture_done(&f);
}

static void test_chain_failures(void)
{
  static const char name[11] = NAME_BCB0;
  struct fixture f;
  struct vg_ota_fat_entry file;
  uint8_t data[4096];
  uint8_t readback[4608];
  uint8_t *big;
  uint8_t *entry;
  uint32_t first;

  fixture_init(&f, 0);
  make_data(data, sizeof(data));
  assert(test_fat_add_file(&f.img, f.dir_cluster, name, data,
                           sizeof(data)) == 0);
  fixture_mount(&f);
  entry = test_fat_entry_raw(&f.img, f.dir_cluster, name);
  assert(entry != 0);
  first = (uint32_t)entry[26] | ((uint32_t)entry[27] << 8);

  test_fat_set_entry(&f.img, first + 3, 0xffff);
  f.vol.fat_cache_valid = 0;
  assert(vg_ota_fat_find(&f.vol, name, f.dir_cluster, &file) == 0);
  assert(vg_ota_fat_read(&f.vol, &file, 0, readback, 4096) == -EIO);

  test_fat_set_chain(&f.img, first, 8);
  test_fat_set_entry(&f.img, first + 7, first);
  f.vol.fat_cache_valid = 0;
  entry[28] = 0x00;
  entry[29] = 0x00;
  entry[30] = 0x30;
  entry[31] = 0x00;
  assert(vg_ota_fat_find(&f.vol, name, f.dir_cluster, &file) == 0);
  assert(file.size == 0x00300000u);
  big = malloc(file.size);
  assert(big != 0);
  assert(vg_ota_fat_read(&f.vol, &file, 0, big, file.size) == -EIO);
  free(big);

  test_fat_set_chain(&f.img, first, 8);
  test_fat_set_entry(&f.img, first, 0);
  f.vol.fat_cache_valid = 0;
  assert(vg_ota_fat_find(&f.vol, name, f.dir_cluster, &file) == 0);
  assert(vg_ota_fat_read(&f.vol, &file, 0, readback, 1024) == -EIO);

  fixture_done(&f);
}

static void test_unsupported_and_bad_bpb(void)
{
  struct fixture f;
  struct vg_ota_fat_volume vol;

  memset(&f, 0, sizeof(f));
  assert(test_fat_init(&f.img, 0, 1000, 1) == 0);
  f.block.img = &f.img;
  assert(vg_ota_fat_mount(&vol, test_block_read, &f.block,
                          f.img.sectors) == -ENOTSUP);
  test_fat_free(&f.img);

  fixture_init(&f, 0);
  f.img.mem[3] = 'E';
  f.img.mem[4] = 'X';
  f.img.mem[5] = 'F';
  f.img.mem[6] = 'A';
  f.img.mem[7] = 'T';
  assert(vg_ota_fat_mount(&vol, test_block_read, &f.block,
                          f.img.sectors) == -ENOTSUP);
  fixture_done(&f);

  fixture_init(&f, 0);
  put16(&f.img.mem[11], 1024);
  assert(vg_ota_fat_mount(&vol, test_block_read, &f.block,
                          f.img.sectors) == -EINVAL);
  fixture_done(&f);

  fixture_init(&f, 0);
  assert(vg_ota_fat_mount(&vol, test_block_read, &f.block,
                          f.img.sectors - 1) == -EINVAL);
  fixture_done(&f);

  fixture_init(&f, 0);
  f.img.mem[13] = 3;
  assert(vg_ota_fat_mount(&vol, test_block_read, &f.block,
                          f.img.sectors) == -EINVAL);
  fixture_done(&f);

  fixture_init(&f, 1);
  put16(&f.img.mem[44], 1);
  put16(&f.img.mem[46], 0);
  assert(vg_ota_fat_mount(&vol, test_block_read, &f.block,
                          f.img.sectors) == -EINVAL);
  fixture_done(&f);

  fixture_init(&f, 1);
  /* A BPB cannot enlarge FAT32 cluster IDs beyond the 28-bit namespace. */
  put16(&f.img.mem[19], 0);
  put16(&f.img.mem[32], 0xffff);
  put16(&f.img.mem[34], 0x7fff);
  put16(&f.img.mem[36], 0);
  put16(&f.img.mem[38], 0x0100);
  assert(vg_ota_fat_mount(&vol, test_block_read, &f.block, 0) == -EINVAL);
  fixture_done(&f);
}

static void test_block_failures(void)
{
  static const char name[11] = NAME_BCB0;
  struct fixture f;
  struct vg_ota_fat_entry file;
  struct vg_ota_fat_volume vol;
  uint8_t data[4096];
  uint8_t readback[4096];

  fixture_init(&f, 0);
  f.block.fail_enabled = 1;
  f.block.fail_sector = 0;
  f.block.fail_count = 1;
  assert(vg_ota_fat_mount(&vol, test_block_read, &f.block,
                          f.img.sectors) == -EIO);
  fixture_done(&f);

  fixture_init(&f, 0);
  make_data(data, sizeof(data));
  assert(test_fat_add_file(&f.img, f.dir_cluster, name, data,
                           sizeof(data)) == 0);
  fixture_mount(&f);
  assert(vg_ota_fat_find(&f.vol, name, f.dir_cluster, &file) == 0);
  f.block.fail_enabled = 1;
  f.block.fail_sector = f.img.first_data;
  f.block.fail_count = f.img.sectors;
  assert(vg_ota_fat_read(&f.vol, &file, 0, readback, 512) == -EIO);
  fixture_done(&f);
}

static void test_corrupt_table_and_cycles(void)
{
  struct fixture f;
  struct vg_ota_fat_entry file;
  uint8_t data[4096];
  uint8_t result[4096];
  uint32_t first;
  uint32_t reads;
  int fat32;

  for (fat32 = 0; fat32 <= 1; fat32++)
    {
      fixture_init(&f, fat32);
      /* The data area is large enough but the advertised FAT is not. */
      put16(&f.img.mem[fat32 ? 36 : 22], 1);
      assert(vg_ota_fat_mount(&f.vol, test_block_read, &f.block,
                              f.img.sectors) == -EINVAL);
      fixture_done(&f);

      fixture_init(&f, fat32);
      make_data(data, sizeof(data));
      assert(test_fat_add_file(&f.img, f.dir_cluster, NAME_BCB0,
                               data, sizeof(data)) == 0);
      fixture_mount(&f);
      assert(vg_ota_fat_find(&f.vol, NAME_BCB0, f.dir_cluster, &file) == 0);
      first = file.first_cluster;
      test_fat_set_entry(&f.img, first, first);
      f.vol.fat_cache_valid = 0;
      reads = f.block.reads;
      assert(vg_ota_fat_read(&f.vol, &file, 0, result,
                             sizeof(result)) == -EIO);
      assert(f.block.reads - reads < 16);

      /* A cycle beginning exactly at EOF must also be rejected. */
      test_fat_set_chain(&f.img, first, 8);
      test_fat_set_entry(&f.img, first + 7, first);
      f.vol.fat_cache_valid = 0;
      assert(vg_ota_fat_read(&f.vol, &file, 0, result,
                             sizeof(result)) == -EIO);
      fixture_done(&f);

      fixture_init(&f, fat32);
      memset(test_fat_dir_base(&f.img, f.dir_cluster), 0xe5, 512);
      test_fat_set_entry(&f.img, f.dir_cluster, f.dir_cluster);
      fixture_mount(&f);
      reads = f.block.reads;
      assert(vg_ota_fat_find(&f.vol, NAME_BCB0, f.dir_cluster,
                             &file) == -EIO);
      assert(f.block.reads - reads < 16);
      fixture_done(&f);
    }

  fixture_init(&f, 0);
  put16(&f.img.mem[17], 8192);
  memset(f.img.mem + f.img.root_start * 512u, 0xe5,
         VG_OTA_FAT_DIR_SECTORS * 512u);
  fixture_mount(&f);
  reads = f.block.reads;
  assert(vg_ota_fat_find(&f.vol, NAME_BCB0, 0, &file) == -EIO);
  assert(f.block.reads - reads == VG_OTA_FAT_DIR_SECTORS);
  fixture_done(&f);
}

int main(void)
{
  test_mount_and_read();
  test_missing_and_abnormal();
  test_chain_failures();
  test_unsupported_and_bad_bpb();
  test_block_failures();
  test_corrupt_table_and_cycles();
  printf("test_ota_fat: OK\n");
  return 0;
}
