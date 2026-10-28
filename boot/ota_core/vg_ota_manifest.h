#ifndef __VG_OTA_MANIFEST_H
#define __VG_OTA_MANIFEST_H
#include "vg_ota_bcb.h"

#define VG_OTA_MANIFEST_BYTES 512u
#define VG_OTA_MANIFEST_SIGNED_BYTES 220u
#define VG_OTA_MANIFEST_FIXTURE 1u
#define VG_OTA_MODEL "STM32H750B-DK"
#define VG_OTA_IMAGE_MAX_BYTES (VG_OTA_SLOT_BYTES - 8192u)
#define VG_OTA_BCB_LOCAL_CONFIRMED 1u

struct vg_ota_manifest
{
  uint32_t slot;
  uint32_t image_size;
  char version[32];
  char task_id[40];
  uint8_t sha256[32];
  char device_id[32];
  char model[32];
  uint64_t issued_at;
  uint64_t expires_at;
  char key_id[16];
  uint8_t signature[64];
  uint32_t flags;
};

/* 固定小端格式，CRC 在偏移 508；这里只校验格式和 CRC，不验证签名。 */
int vg_ota_manifest_decode(const uint8_t *buf, uint32_t size,
                            struct vg_ota_manifest *out);
int vg_ota_manifest_encode(const struct vg_ota_manifest *in,
                            uint8_t buf[VG_OTA_MANIFEST_BYTES]);
int vg_ota_manifest_bind(const struct vg_ota_manifest *m,
                          const struct vg_ota_bcb *bcb,
                          const char *device_id, int allow_fixture);
#endif
