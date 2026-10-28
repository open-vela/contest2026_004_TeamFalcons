#ifndef __VG_OTA_BOOT_H
#define __VG_OTA_BOOT_H

#include <stdint.h>

#include "vg_ota_bcb.h"
#include "vg_ota_fat.h"

enum vg_ota_boot_reason_e
{
  VG_OTA_BOOT_REASON_NONE = 0,
  VG_OTA_BOOT_REASON_SLOT_A,
  VG_OTA_BOOT_REASON_B_CONFIRMED,
  VG_OTA_BOOT_REASON_B_TRIAL,
  VG_OTA_BOOT_REASON_A_PENDING,
  VG_OTA_BOOT_REASON_A_FAILED,
  VG_OTA_BOOT_REASON_A_NO_STAGING,
  VG_OTA_BOOT_REASON_A_NO_BCB,
  VG_OTA_BOOT_REASON_A_BCB_INVALID,
  VG_OTA_BOOT_REASON_A_EMMC_ERROR,
  VG_OTA_BOOT_REASON_A_FAT_ERROR,
  VG_OTA_BOOT_REASON_A_READ_ERROR,
  VG_OTA_BOOT_REASON_A_IMAGE_INVALID,
  VG_OTA_BOOT_REASON_A_SAFE_STOP,
  VG_OTA_BOOT_REASON_B_INSTALLED,
  VG_OTA_BOOT_REASON_A_INSTALL_FAILED
};

struct vg_ota_boot_decision
{
  uint32_t slot;
  uint32_t reason;
  uint32_t bcb_state;
  uint32_t bcb_seq;
  uint32_t bcb_slot;
  uint32_t bcb_which;
  uint32_t bcb_valid0;
  uint32_t bcb_valid1;
  uint32_t bcb_error;
  uint32_t fat_type;
  uint32_t fat_error;
  uint32_t emmc_error;
  uint32_t install_stage;
  uint32_t install_error;
  uint32_t install_bytes;
  uint32_t bcb_trial_count;
};

/* Decides which QSPI slot to boot.  Storage errors always resolve to slot A;
 * this function never writes anything and does not check the target vector
 * table.  Returns 0 when a decision (including the slot A fallback) was
 * produced.
 */
int vg_ota_boot_decide(vg_ota_block_read_fn read, void *ctx,
                       uint32_t device_sectors,
                       struct vg_ota_boot_decision *out);

const char *vg_ota_boot_reason_name(uint32_t reason);

#endif
