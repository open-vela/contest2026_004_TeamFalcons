#ifndef __APP_VELAGUARD_VG_OTA_PROBE_H
#define __APP_VELAGUARD_VG_OTA_PROBE_H

#include <stddef.h>
#include <stdint.h>

#include "vg_ota_fat.h"

#define VG_OTA_CID_BYTES        16
#define VG_OTA_CSD_BYTES        16
#define VG_OTA_EXT_CSD_BYTES    512

struct vg_ota_cid_info
{
  uint8_t  raw[VG_OTA_CID_BYTES];
  uint8_t  mid;
  uint8_t  oid;
  char     pnm[7];
  uint8_t  prv;
  uint32_t psn;
  uint8_t  mdt;
  uint8_t  crc;
};

struct vg_ota_csd_info
{
  uint8_t  raw[VG_OTA_CSD_BYTES];
  uint8_t  structure;
  uint8_t  spec_vers;
  uint8_t  read_bl_len;
  uint16_t csize;
  uint8_t  csize_mult;
  uint64_t capacity_bytes;
};

struct vg_ota_extcsd_info
{
  uint8_t  rev;
  uint32_t sec_count;
  uint64_t capacity_bytes;
};

int vg_ota_parse_cid(const uint32_t cid[4], struct vg_ota_cid_info *out);
int vg_ota_parse_csd(const uint32_t csd[4], struct vg_ota_csd_info *out);
int vg_ota_parse_extcsd(const uint8_t extcsd[VG_OTA_EXT_CSD_BYTES],
                        struct vg_ota_extcsd_info *out);

#endif
