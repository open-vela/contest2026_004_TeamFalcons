/****************************************************************************
 * app/velaguard/vg_ota_confirm.h
 *
 * Local Install Confirmation, safety interlocks, pending BCB commitment,
 * and handoff to the boot stub installer.
 ****************************************************************************/

#ifndef __VG_OTA_CONFIRM_H
#define __VG_OTA_CONFIRM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "vg_ota_bcb.h"
#include "vg_ota_manifest.h"
#include "vg_ota_staging.h"

/* Confirmation source: only local sources are authorized */
enum vg_ota_confirm_source_e
{
  VG_OTA_CONFIRM_SRC_UNKNOWN = 0,
  VG_OTA_CONFIRM_SRC_TOUCH,      /* 板载触摸屏长按 (UI 层长按 2 秒) */
  VG_OTA_CONFIRM_SRC_BUTTON,     /* 板载物理 USER 按键长按 */
  VG_OTA_CONFIRM_SRC_LOCAL_CLI,  /* 串口只能查看状态，不能确认安装 */

  /* 以下为非法远程/非本地来源，调用必须拒绝并返回 -EPERM */
  VG_OTA_CONFIRM_SRC_MQTT,       /* 远程 MQTT (禁止) */
  VG_OTA_CONFIRM_SRC_PHONE,      /* 手机 App (禁止) */
  VG_OTA_CONFIRM_SRC_VOICE,      /* 语音识别结果 (禁止) */
  VG_OTA_CONFIRM_SRC_AGENT       /* 设备端 Agent (禁止) */
};

/* Safety interlock check callbacks */
struct vg_ota_interlock_ports
{
  /* 检查高等级活动告警：返回 0 表示无严重告警，非 0 表示存在严重告警 */
  int (*check_alarm)(void *ctx);
  /* 检查存储可写性：返回 0 表示可写，非 0 表示只读或写错误 */
  int (*check_storage)(void *ctx);
  /* 检查镜像摘要：返回 0 表示一致，非 0 表示摘要不符 */
  int (*check_digest)(void *ctx);
  /* 检查设备空闲：返回 0 表示空闲，非 0 表示有活动配置事务 */
  int (*check_idle)(void *ctx);
  /* 检查 RJ45 链路：返回 0 表示正常连接，非 0 表示断开 */
  int (*check_rj45)(void *ctx);
  /* 系统重启 */
  void (*reboot)(void *ctx);
  void *ctx;
};

/* Fault injection bitmask for testing interlock rejections */
#define VG_OTA_FAULT_NONE       0u
#define VG_OTA_FAULT_ALARM      (1u << 0)
#define VG_OTA_FAULT_STORAGE    (1u << 1)
#define VG_OTA_FAULT_DIGEST     (1u << 2)
#define VG_OTA_FAULT_BUSY       (1u << 3)
#define VG_OTA_FAULT_RJ45       (1u << 4)

void vg_ota_confirm_set_fault(uint32_t fault_mask);
uint32_t vg_ota_confirm_get_fault(void);
void vg_ota_confirm_clear_faults(void);

/* Check safety interlocks.
 * Returns 0 on pass, or negative errno on failure with reason_out filled.
 */
int vg_ota_check_interlocks(const struct vg_ota_interlock_ports *ports,
                            char *reason_out, size_t reason_len);

/* Perform local install confirmation:
 * 1. Validates caller source (TOUCH or BUTTON only; others -> -EPERM)
 * 2. Checks state == READY_AWAITING_CONFIRM
 * 3. Runs 5 safety interlocks (alarm, storage, digest, idle, rj45)
 *    If any fails: stays in READY, reports error, does NOT write BCB, does NOT reboot.
 * 4. Writes pending BCB with flags = VG_OTA_BCB_LOCAL_CONFIRMED to alternate BCB slot
 *    If write fails: stays in READY, reports error, does NOT reboot.
 * 5. Syncs filesystem and reboots into boot stub.
 */
int vg_ota_confirm_install(enum vg_ota_confirm_source_e src,
                           const struct vg_ota_interlock_ports *ports,
                           char *err_out, size_t err_len);

/* Write pending BCB bound to the manifest into the alternate BCB slot. */

/* Decline current offer:
 * Sets state to DECLINED, unlinks staging image/manifest, writes STATE.BIN,
 * and remembers task_id to suppress repeat popups.
 */
int vg_ota_confirm_decline(const char *task_id, const char *reason);

/* Expire current offer on timeout:
 * Sets state to EXPIRED, unlinks staging image/manifest, writes STATE.BIN,
 * and remembers task_id to suppress repeat popups.
 */
int vg_ota_confirm_expire(const char *task_id);

/* Check whether a task_id was declined or expired to prevent repeat popups */
bool vg_ota_confirm_is_suppressed(const char *task_id);

/* Get the board's default live interlock ports */
const struct vg_ota_interlock_ports *vg_ota_get_board_interlock_ports(void);

#endif /* __VG_OTA_CONFIRM_H */
