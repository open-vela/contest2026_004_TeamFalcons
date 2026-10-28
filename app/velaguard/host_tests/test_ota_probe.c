#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "vg_ota_probe.h"

static void make_cid(uint8_t mid, uint8_t oid, const char pnm[6],
                     uint8_t prv, uint32_t psn, uint8_t mdt, uint8_t crc,
                     uint32_t cid[4])
{
  cid[0] = ((uint32_t)mid << 24) | ((uint32_t)oid << 8) | (uint8_t)pnm[0];
  cid[1] = ((uint32_t)pnm[1] << 24) | ((uint32_t)pnm[2] << 16) |
           ((uint32_t)pnm[3] << 8) | (uint8_t)pnm[4];
  cid[2] = ((uint32_t)pnm[5] << 24) | ((uint32_t)prv << 16) |
           ((psn >> 16) & 0xffff);
  cid[3] = ((psn & 0xffff) << 16) | ((uint32_t)mdt << 8) |
           ((uint32_t)crc << 1) | 1u;
}

static void make_csd(uint16_t csize, uint8_t csizemult, uint8_t readbllen,
                     uint32_t csd[4])
{
  csd[0] = (3u << 30) | (4u << 26);
  csd[1] = ((uint32_t)readbllen << 16) | ((csize >> 2) & 0x03ff);
  csd[2] = ((uint32_t)(csize & 3) << 30) |
           ((uint32_t)csizemult << 15);
  csd[3] = 0;
}

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

static void make_fat32_bpb(uint8_t bpb[512])
{
  memset(bpb, 0, 512);
  bpb[0] = 0xeb;
  bpb[1] = 0x58;
  bpb[2] = 0x90;
  memcpy(&bpb[3], "MSDOS5.0", 8);
  put16(&bpb[11], 512);
  bpb[13] = 8;
  put16(&bpb[14], 32);
  bpb[16] = 2;
  put16(&bpb[17], 0);
  put16(&bpb[19], 0);
  bpb[21] = 0xf8;
  put16(&bpb[22], 0);
  put32(&bpb[32], 0x00f00000u);
  put32(&bpb[36], 0x00003a00u);
  put32(&bpb[44], 2);
  memcpy(&bpb[82], "FAT32   ", 8);
  bpb[510] = 0x55;
  bpb[511] = 0xaa;
}

static void make_fat16_bpb(uint8_t bpb[512])
{
  memset(bpb, 0, 512);
  bpb[0] = 0xeb;
  bpb[1] = 0x3c;
  bpb[2] = 0x90;
  memcpy(&bpb[3], "MSDOS5.0", 8);
  put16(&bpb[11], 512);
  bpb[13] = 4;
  put16(&bpb[14], 4);
  bpb[16] = 2;
  put16(&bpb[17], 512);
  put16(&bpb[19], 0xffff);
  bpb[21] = 0xf8;
  put16(&bpb[22], 0x20);
  memcpy(&bpb[54], "FAT16   ", 8);
  bpb[510] = 0x55;
  bpb[511] = 0xaa;
}

static void test_cid(void)
{
  uint32_t cid[4];
  struct vg_ota_cid_info info;

  make_cid(0x11, 0x22, "ABCDEF", 0x34, 0x12345678, 0x56, 0x2a, cid);
  assert(vg_ota_parse_cid(cid, &info) == 0);
  assert(info.mid == 0x11);
  assert(info.oid == 0x22);
  assert(strcmp(info.pnm, "ABCDEF") == 0);
  assert(info.prv == 0x34);
  assert(info.psn == 0x12345678);
  assert(info.mdt == 0x56);
  assert(info.crc == 0x2a);
  assert(info.raw[0] == 0x11);
  assert(info.raw[15] == (((0x2a << 1) | 1u) & 0xff));
}

static void test_csd(void)
{
  uint32_t csd[4];
  struct vg_ota_csd_info info;
  uint64_t expected;

  make_csd(0x100, 7, 9, csd);
  assert(vg_ota_parse_csd(csd, &info) == 0);
  assert(info.structure == 3);
  assert(info.spec_vers == 4);
  assert(info.csize == 0x100);
  assert(info.csize_mult == 7);
  assert(info.read_bl_len == 9);
  expected = (uint64_t)(0x100 + 1) * (1u << (7 + 2)) * 512u;
  assert(info.capacity_bytes == expected);

  make_csd(0x0fff, 0, 9, csd);
  assert(vg_ota_parse_csd(csd, &info) == 0);
  assert(info.capacity_bytes == 0);
}

static void test_extcsd(void)
{
  uint8_t extcsd[512];
  struct vg_ota_extcsd_info info;

  memset(extcsd, 0, sizeof(extcsd));
  extcsd[192] = 7;
  put32(&extcsd[212], 0x007a1200u);
  assert(vg_ota_parse_extcsd(extcsd, &info) == 0);
  assert(info.rev == 7);
  assert(info.sec_count == 0x007a1200u);
  assert(info.capacity_bytes == (uint64_t)0x007a1200u * 512u);
}

static void test_fat(void)
{
  uint8_t bpb[512];
  struct vg_ota_fat_info info;

  make_fat32_bpb(bpb);
  assert(vg_ota_parse_fat_bpb(bpb, &info) == 0);
  assert(info.type == VG_OTA_FAT32);
  assert(info.bytes_per_sector == 512);
  assert(info.sectors_per_cluster == 8);
  assert(info.num_fats == 2);
  assert(info.fat_size_sectors == 0x3a00);
  assert(info.total_sectors == 0x00f00000u);
  assert(info.root_cluster == 2);
  assert(info.cluster_count > 65525u);

  make_fat16_bpb(bpb);
  assert(vg_ota_parse_fat_bpb(bpb, &info) == 0);
  assert(info.type == VG_OTA_FAT16);
  assert(info.root_entries == 512);
  assert(info.fat_size_sectors == 0x20);
  assert(info.cluster_count > 4085u);
  assert(info.cluster_count < 65525u);

  make_fat32_bpb(bpb);
  bpb[510] = 0;
  bpb[511] = 0;
  assert(vg_ota_parse_fat_bpb(bpb, &info) == -EINVAL);

  make_fat32_bpb(bpb);
  memcpy(&bpb[3], "EXFAT   ", 8);
  assert(vg_ota_parse_fat_bpb(bpb, &info) == -ENOTSUP);
  assert(info.type == VG_OTA_FAT_EXFAT);

  make_fat32_bpb(bpb);
  put16(&bpb[11], 0);
  assert(vg_ota_parse_fat_bpb(bpb, &info) == -EINVAL);

  make_fat32_bpb(bpb);
  bpb[13] = 3;
  assert(vg_ota_parse_fat_bpb(bpb, &info) == -EINVAL);
}

int main(void)
{
  test_cid();
  test_csd();
  test_extcsd();
  test_fat();
  printf("test_ota_probe: OK\n");
  return 0;
}
