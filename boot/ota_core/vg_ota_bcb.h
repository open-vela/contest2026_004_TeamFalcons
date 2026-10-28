#ifndef __VG_OTA_BCB_H
#define __VG_OTA_BCB_H

#include <stdint.h>

/* Boot Control Block shared by the application and the internal boot stub.
 * Both files are fixed 4 KiB, little-endian, zero padded after the header.
 * The CRC covers every header byte before the crc32 field.
 */

#define VG_OTA_BCB_FILE_BYTES      4096u
#define VG_OTA_BCB_HEADER_BYTES    140u
#define VG_OTA_BCB_FORMAT          1u
#define VG_OTA_BCB_MAGIC           0x43424756u /* "VGBC" in memory */

#define VG_OTA_BCB_VERSION_BYTES   32u
#define VG_OTA_BCB_TASK_ID_BYTES   40u
#define VG_OTA_BCB_SHA256_BYTES    32u

#define VG_OTA_SLOT_A_BASE         0x90000000u
#define VG_OTA_SLOT_B_BASE         0x90800000u
#define VG_OTA_SLOT_BYTES          (8u * 1024u * 1024u)

enum vg_ota_bcb_state_e
{
  VG_OTA_BCB_STATE_INVALID = 0,
  VG_OTA_BCB_STATE_PENDING = 1,
  VG_OTA_BCB_STATE_TRIAL = 2,
  VG_OTA_BCB_STATE_CONFIRMED = 3,
  VG_OTA_BCB_STATE_FAILED = 4
};

enum vg_ota_bcb_slot_e
{
  VG_OTA_BCB_SLOT_A = 0,
  VG_OTA_BCB_SLOT_B = 1
};

struct vg_ota_bcb
{
  uint32_t seq;
  uint32_t state;
  uint32_t slot;
  uint32_t image_size;
  uint32_t trial_count;
  uint32_t flags;
  char     version[VG_OTA_BCB_VERSION_BYTES + 1];
  char     task_id[VG_OTA_BCB_TASK_ID_BYTES + 1];
  uint8_t  sha256[VG_OTA_BCB_SHA256_BYTES];
};

uint32_t vg_ota_crc32(const uint8_t *data, uint32_t len);

/* Returns 0, -EINVAL for malformed fields, -EBADMSG for a CRC mismatch. */
int vg_ota_bcb_decode(const uint8_t *buf, uint32_t len,
                      struct vg_ota_bcb *out);
int vg_ota_bcb_encode(const struct vg_ota_bcb *in, uint8_t *buf, uint32_t len);

/* Signed 32-bit sequence comparison; wraps like a serial number. */
int vg_ota_bcb_seq_newer(uint32_t a, uint32_t b);

/* Chooses the newer valid block.  have0/have1 say whether the file existed.
 * A decoded block is only accepted when decode() succeeds.  Returns 0 when a
 * block was selected, -ENOENT when neither file was usable, -EINVAL when
 * both sequences differ by exactly half the serial space.
 */
int vg_ota_bcb_select(const uint8_t *buf0, uint32_t len0, int have0,
                      const uint8_t *buf1, uint32_t len1, int have1,
                      struct vg_ota_bcb *out, uint32_t *which,
                      int *valid0, int *valid1);

const char *vg_ota_bcb_state_name(uint32_t state);
const char *vg_ota_bcb_slot_name(uint32_t slot);

#endif
