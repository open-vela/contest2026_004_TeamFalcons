#ifndef __VG_OTA_MBOX_H
#define __VG_OTA_MBOX_H

#include <stdint.h>

/* DTCM2 tail, outside the NuttX .data/.bss regions.  It survives a warm
 * reset with VDD present, so the boot stub can report a QSPI probe result
 * to the application without touching eMMC or QSPI.
 */

#define VG_OTA_MBOX_ADDR     0x2001f000u
#define VG_OTA_MBOX_MAGIC    0x56474f54u /* VGOT */
#define VG_OTA_MBOX_VERSION  3u

enum vg_ota_mbox_request_e
{
  VG_OTA_MBOX_REQ_NONE = 0,
  VG_OTA_MBOX_REQ_QSPI_PROBE = 1
};

enum vg_ota_mbox_state_e
{
  VG_OTA_MBOX_IDLE = 0,
  VG_OTA_MBOX_REQUESTED = 1,
  VG_OTA_MBOX_RUNNING = 2,
  VG_OTA_MBOX_OK = 3,
  VG_OTA_MBOX_FAIL = 4
};

struct vg_ota_mbox
{
  uint32_t magic;
  uint32_t version;
  uint32_t request;
  uint32_t state;
  uint32_t error;
  uint32_t jedec1;
  uint32_t jedec2;
  uint32_t capacity;
  uint32_t erase_size;
  uint32_t write_size;
  uint32_t seq;
  uint32_t run_seq;
  uint32_t sr;
  uint32_t cr;
  uint32_t ccr;
  uint32_t dcr;
  uint32_t raw1;
  uint32_t raw2;
  uint32_t fail_index;
  uint32_t fail_got;
  uint32_t fail_want;

  /* Boot selection result, written on every reset. */
  uint32_t boot_slot;
  uint32_t boot_reason;
  uint32_t boot_bcb_which;
  uint32_t boot_bcb_state;
  uint32_t boot_bcb_seq;
  uint32_t boot_bcb_slot;
  uint32_t boot_valid0;
  uint32_t boot_valid1;
  uint32_t boot_bcb_error;
  uint32_t boot_fat_type;
  uint32_t boot_fat_error;
  uint32_t boot_emmc_error;
  uint32_t install_stage;
  uint32_t install_error;
  uint32_t install_bytes;
};

#define VG_OTA_MBOX ((volatile struct vg_ota_mbox *)VG_OTA_MBOX_ADDR)

#endif
