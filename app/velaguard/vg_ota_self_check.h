/****************************************************************************
 * app/velaguard/vg_ota_self_check.h
 *
 * New firmware self-check, confirmed / failed BCB update, and watchdog feed.
 ****************************************************************************/

#ifndef __VG_OTA_SELF_CHECK_H
#define __VG_OTA_SELF_CHECK_H

#include <stddef.h>
#include <stdint.h>

#define VG_OTA_DIR                "/mnt/emmc/VGOTA"
#define VG_OTA_FAIL_INJECT_NAME   "FORCE_FAIL.BIN"
#define VG_OTA_STATE_NAME         "STATE.BIN"

enum vg_ota_check_state_e
{
  VG_OTA_CHECK_STATE_NONE = 0,
  VG_OTA_CHECK_STATE_PASSED,
  VG_OTA_CHECK_STATE_FAILED
};

struct vg_ota_self_check_status
{
  uint32_t state;           /* enum vg_ota_check_state_e */
  uint32_t active_bcb_which;
  uint32_t active_bcb_seq;
  uint32_t active_bcb_state;
  uint32_t active_bcb_slot;
  uint32_t trial_count;
  char     active_version[33];
  char     active_task_id[41];
  char     detail[128];
};

/* Overrides the base directory (for host tests). Defaults to /mnt/emmc/VGOTA. */
void vg_ota_self_check_set_dir(const char *dir);
const char *vg_ota_self_check_get_dir(void);

/* Periodic kick to the STM32 independent watchdog (IWDG1). */
void vg_ota_watchdog_kick(void);

/* Returns 1 if running in B under TRIAL and self-check has not run yet. */
int vg_ota_self_check_needed(void);

/* Runs the deterministic self-check.
 * On success: writes CONFIRMED to the other BCB, updates STATE.BIN, returns 0.
 * On failure: writes FAILED to the other BCB, updates STATE.BIN, returns -1.
 */
int vg_ota_self_check_run(char *detail, size_t detail_len);

/* Called at system startup from velaguard.c.
 * If running in B and self-check is needed, runs self-check.
 * If self-check fails, triggers safe reboot back to Slot A.
 * If self-check passes, stays in B and enables periodic watchdog kick.
 */
int vg_ota_self_check_boot(void);

/* Queries the current self-check status. */
int vg_ota_self_check_get_status(struct vg_ota_self_check_status *status);

/* Sets or clears the force-fail injection flag file. */
int vg_ota_self_check_set_inject_fail(int enable);
int vg_ota_self_check_get_inject_fail(void);

/* Manually trigger confirmation (writes CONFIRMED) or rollback (writes FAILED + reboot). */
int vg_ota_self_check_manual_confirm(void);
int vg_ota_self_check_manual_rollback(const char *reason);

#endif
