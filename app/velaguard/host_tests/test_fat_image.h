#ifndef __TEST_FAT_IMAGE_H
#define __TEST_FAT_IMAGE_H

#include <stdint.h>

struct test_fat_image
{
  uint8_t *mem;
  uint32_t sectors;
  uint32_t spc;
  int is_fat32;
  uint32_t fat_start;
  uint32_t fat_sectors;
  uint32_t root_start;
  uint32_t root_sectors;
  uint32_t first_data;
  uint32_t cluster_count;
  uint32_t next_cluster;
};

struct test_block_ctx
{
  const struct test_fat_image *img;
  int fail_enabled;
  uint32_t fail_sector;
  uint32_t fail_count;
  uint32_t reads;
};

int test_fat_init(struct test_fat_image *img, int fat32,
                   uint32_t cluster_count, uint8_t spc);
void test_fat_free(struct test_fat_image *img);

uint32_t test_fat_alloc_chain(struct test_fat_image *img, uint32_t clusters);
int test_fat_set_chain(struct test_fat_image *img, uint32_t first,
                       uint32_t clusters);
void test_fat_set_entry(struct test_fat_image *img, uint32_t cluster,
                        uint32_t value);
uint32_t test_fat_get_entry(const struct test_fat_image *img,
                            uint32_t cluster);

int test_fat_dir_add(struct test_fat_image *img, uint32_t dir_cluster,
                     const char name11[11], uint8_t attr,
                     uint32_t first_cluster, uint32_t size);
int test_fat_add_file(struct test_fat_image *img, uint32_t dir_cluster,
                      const char name11[11], const uint8_t *data,
                      uint32_t len);
uint8_t *test_fat_entry_raw(const struct test_fat_image *img,
                            uint32_t dir_cluster, const char name11[11]);
uint8_t *test_fat_dir_base(const struct test_fat_image *img,
                           uint32_t dir_cluster);

int test_block_read(void *ctx, uint32_t sector, uint8_t *buf, uint32_t count);

#endif
