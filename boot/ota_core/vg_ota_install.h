#ifndef __VG_OTA_INSTALL_H
#define __VG_OTA_INSTALL_H
#include "vg_ota_boot.h"
#include "vg_ota_manifest.h"

#define VG_OTA_ERASE_BYTES 8192u
#define VG_OTA_PROGRAM_BYTES 256u
enum vg_ota_install_stage
{
  VG_OTA_INSTALL_NONE=0, VG_OTA_INSTALL_METADATA, VG_OTA_INSTALL_IMAGE,
  VG_OTA_INSTALL_FENCE, VG_OTA_INSTALL_ERASE, VG_OTA_INSTALL_PROGRAM,
  VG_OTA_INSTALL_READBACK, VG_OTA_INSTALL_TRIAL, VG_OTA_INSTALL_DONE
};
struct vg_ota_install_ports
{
  void *ctx;
  vg_ota_block_read_fn read;
  uint32_t sectors;
  int (*write_sector)(void *ctx, uint32_t sector, const uint8_t *data);
  int (*sync)(void *ctx);
  int (*erase)(void *ctx, uint32_t address, uint32_t len);
  int (*program)(void *ctx, uint32_t address, const uint8_t *data, uint32_t len);
  int (*read_flash)(void *ctx, uint32_t address, uint8_t *data, uint32_t len);
  const char *device_id;
  int allow_fixture;
  void (*progress)(void *ctx, uint32_t stage, uint32_t bytes);
};

/* pending 安装专用。失败保持 A；擦写前持久化较新 FAILED，成功后再提交 TRIAL。 */
int vg_ota_install_pending(const struct vg_ota_install_ports *ports,
                            struct vg_ota_boot_decision *decision);
int vg_ota_image_vector_valid(const uint8_t vector[8], uint32_t size);
#endif
