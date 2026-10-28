#ifndef __VG_OTA_SHA256_H
#define __VG_OTA_SHA256_H
#include <stdint.h>

struct vg_ota_sha256
{
  uint32_t h[8];
  uint64_t bytes;
  uint8_t block[64];
  uint32_t used;
};

void vg_ota_sha256_init(struct vg_ota_sha256 *s);
void vg_ota_sha256_update(struct vg_ota_sha256 *s, const uint8_t *p,
                          uint32_t len);
void vg_ota_sha256_final(struct vg_ota_sha256 *s, uint8_t digest[32]);
#endif
