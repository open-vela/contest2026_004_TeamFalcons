/****************************************************************************
 * app/velaguard/vg_ota_staging.h
 *
 * Staging image storage, manifest validation, and READY_AWAITING_CONFIRM.
 ****************************************************************************/

#ifndef __VG_OTA_STAGING_H
#define __VG_OTA_STAGING_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

#include "vg_ota_manifest.h"

#define VG_OTA_READY_TIMEOUT_MS       600000u

/* Serialize staging, local confirmation and cancellation as one transaction. */
void vg_ota_staging_lock(void);
void vg_ota_staging_unlock(void);
void vg_ota_staging_tick(void);

#define VG_OTA_STAGING_DIR            "/mnt/emmc/VGOTA"
#define VG_OTA_IMAGE_TMP_NAME         "IMAGE.TMP"
#define VG_OTA_IMAGE_BIN_NAME         "IMAGE.BIN"
#define VG_OTA_MANIFEST_BIN_NAME      "MANIFEST.BIN"
#define VG_OTA_STATE_BIN_NAME         "STATE.BIN"
#define VG_OTA_STAGING_SAFETY_MARGIN  (1024u * 1024u) /* 1 MiB margin */

enum vg_ota_state_e
{
  VG_OTA_STATE_IDLE = 0,
  VG_OTA_STATE_OFFER_RECEIVED,
  VG_OTA_STATE_DOWNLOADING,
  VG_OTA_STATE_VERIFYING,
  VG_OTA_STATE_READY_AWAITING_CONFIRM,
  VG_OTA_STATE_INSTALL_PENDING,
  VG_OTA_STATE_REBOOTING,
  VG_OTA_STATE_CONFIRMED,
  VG_OTA_STATE_DECLINED,
  VG_OTA_STATE_EXPIRED,
  VG_OTA_STATE_FAILED,
  VG_OTA_STATE_ROLLED_BACK
};

struct vg_ota_staging_info
{
  uint32_t state;               /* enum vg_ota_state_e */
  char     task_id[41];
  char     version[33];
  uint32_t image_size;
  uint8_t  sha256[32];
  char     error_code[48];      /* e.g. "insufficient_space", "bad_device_id" */
  uint64_t free_bytes;
  uint64_t required_bytes;
};

/* Overrides staging dir (for host unit testing). */
void vg_ota_staging_set_dir(const char *dir);
const char *vg_ota_staging_get_dir(void);

/* Check space on eMMC mount point for given image size. */
int vg_ota_staging_check_space(uint32_t image_size, uint64_t *free_out, uint64_t *req_out);

/* Validate manifest fields against board identity, model, slot, version, time, digest. */
int vg_ota_manifest_validate(const struct vg_ota_manifest *m,
                             const uint8_t actual_digest[32],
                             char *err_out, size_t err_len);

/* Stage image data:
 * - Checks space
 * - Writes IMAGE.TMP, fsyncs
 * - Reads back and verifies SHA-256 + vector table
 * - Atomically renames to IMAGE.BIN
 * - Writes MANIFEST.BIN
 * - Enters READY_AWAITING_CONFIRM (does NOT write pending BCB)
 */
typedef ssize_t (*vg_ota_source_read_fn)(void *ctx, uint32_t offset, uint8_t *buf, size_t len);

int vg_ota_staging_stage(const struct vg_ota_manifest *m,
                         vg_ota_source_read_fn source_read, void *source_ctx,
                         char *err_out, size_t err_len);

/* Query live staging / OTA status. */
int vg_ota_staging_get_info(struct vg_ota_staging_info *info);

/* Set live staging state and error code. */
int vg_ota_staging_set_state(uint32_t state, const char *err_code);
int vg_ota_staging_set_error(const char *err_code);

/* Load MANIFEST.BIN from staging directory. */
int vg_ota_staging_load_manifest(struct vg_ota_manifest *m);

/* Calculate SHA-256 of staged IMAGE.BIN. */
int vg_ota_staging_calc_image_sha256(uint8_t out[32]);

/* Verify staged IMAGE.BIN SHA-256 against MANIFEST.BIN. */
int vg_ota_staging_verify_digest(void);

/* Decline staged offer: removes staging files, writes DECLINED to STATE.BIN,
 * and remembers task_id to suppress repeat popups.
 */
int vg_ota_staging_decline(const char *task_id, const char *reason);

/* Expire staged offer on timeout: removes staging files, writes EXPIRED to STATE.BIN,
 * and remembers task_id to suppress repeat popups.
 */
int vg_ota_staging_expire(const char *task_id);

/* Task suppression checks. */
bool vg_ota_staging_is_suppressed(const char *task_id);

/* Clear staging files (IMAGE.TMP, IMAGE.BIN, MANIFEST.BIN) and return to IDLE. */
int vg_ota_staging_clear(void);

/* State name helper. */
const char *vg_ota_state_name(uint32_t state);

#endif
