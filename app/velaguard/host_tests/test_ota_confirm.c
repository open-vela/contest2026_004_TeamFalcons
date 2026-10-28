/****************************************************************************
 * app/velaguard/host_tests/test_ota_confirm.c
 *
 * Host unit tests for VelaGuard OTA local Install Confirmation:
 * - Authorization matrix (TOUCH, BUTTON ok; LOCAL_CLI denied; MQTT, PHONE, VOICE, AGENT rejected)
 * - 5 Safety Interlocks (alarm, storage, digest, idle, RJ45)
 * - Pending BCB commitment with VG_OTA_BCB_LOCAL_CONFIRMED
 * - Decline and Expire with duplicate task_id suppression
 * - Pending BCB write failure handling
 ****************************************************************************/

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <time.h>

#include "vg_device_id.h"
#include "vg_fw_identity.h"
#include "vg_ota_bcb.h"
#include "vg_ota_confirm.h"
#include "vg_ota_manifest.h"
#include "vg_ota_sha256.h"
#include "vg_ota_staging.h"

static uint64_t g_now_ms = 1000;
static int g_fail_fsync, g_fail_rename, g_fail_read;
int __real_fsync(int fd);
int __real_rename(const char *a, const char *b);
ssize_t __real_read(int fd, void *buf, size_t count);

int __wrap_clock_gettime(clockid_t clk, struct timespec *ts)
{
  ts->tv_sec = g_now_ms / 1000;
  ts->tv_nsec = (g_now_ms % 1000) * 1000000;
  return 0;
}
static int is_bcb_tmp(int fd)
{
  char path[64], dest[256];
  snprintf(path, sizeof(path), "/proc/self/fd/%d", fd);
  ssize_t n = readlink(path, dest, sizeof(dest)-1);
  if (n < 0) return 0;
  dest[n] = 0;
  return strstr(dest, "BCB0.TMP") != NULL;
}
int __wrap_fsync(int fd)
{
  if (g_fail_fsync && is_bcb_tmp(fd)) { errno = EIO; return -1; }
  return __real_fsync(fd);
}
int __wrap_rename(const char *a, const char *b)
{
  if (g_fail_rename && strstr(a, "BCB0.TMP")) { errno = EIO; return -1; }
  return __real_rename(a,b);
}
ssize_t __wrap_read(int fd, void *buf, size_t count)
{
  if (g_fail_read && is_bcb_tmp(fd)) { errno = EIO; return -1; }
  return __real_read(fd,buf,count);
}

static char g_temp_dir[128];
static uint8_t g_test_image[16384];
static int g_reboot_called = 0;

static void setup_temp_dir(void)
{
  char templ[] = "/tmp/vg_confirm_test_XXXXXX";
  char *d = mkdtemp(templ);
  assert(d != NULL);
  snprintf(g_temp_dir, sizeof(g_temp_dir), "%s", d);
  vg_ota_staging_set_dir(g_temp_dir);
  vg_ota_confirm_clear_faults();
  g_now_ms = 1000;
  g_reboot_called = 0;
}

static void cleanup_temp_dir(void)
{
  char cmd[256];
  snprintf(cmd, sizeof(cmd), "rm -rf %s", g_temp_dir);
  (void)system(cmd);
}

static void prepare_test_image(size_t len)
{
  size_t i;
  assert(len <= sizeof(g_test_image) && len >= 8);
  /* Valid ARM vector table for slot B:
   * sp = 0x24010000 (AXI SRAM)
   * pc = 0x90800101 (Thumb entry in slot B)
   */
  g_test_image[0] = 0x00; g_test_image[1] = 0x00;
  g_test_image[2] = 0x01; g_test_image[3] = 0x24;

  g_test_image[4] = 0x01; g_test_image[5] = 0x01;
  g_test_image[6] = 0x80; g_test_image[7] = 0x90;

  for (i = 8; i < len; i++)
    {
      g_test_image[i] = (uint8_t)(i ^ 0x3c);
    }
}

static void compute_sha256(const uint8_t *data, size_t len, uint8_t out[32])
{
  struct vg_ota_sha256 sha;
  vg_ota_sha256_init(&sha);
  vg_ota_sha256_update(&sha, data, (uint32_t)len);
  vg_ota_sha256_final(&sha, out);
}

static ssize_t mock_source_read(void *ctx, uint32_t offset, uint8_t *buf, size_t len)
{
  size_t img_len = *(size_t *)ctx;
  if (offset >= img_len) return 0;
  if (offset + len > img_len) len = img_len - offset;
  memcpy(buf, g_test_image + offset, len);
  return (ssize_t)len;
}

static void stage_valid_offer(const char *task_id, const char *ver)
{
  struct vg_ota_manifest m;
  size_t img_len = sizeof(g_test_image);
  char err[64];

  prepare_test_image(img_len);

  memset(&m, 0, sizeof(m));
  m.slot = VG_OTA_BCB_SLOT_B;
  m.image_size = (uint32_t)img_len;
  snprintf(m.version, sizeof(m.version), "%s", ver ? ver : "0.2.0");
  snprintf(m.task_id, sizeof(m.task_id), "%s", task_id ? task_id : "T07-TEST");
  snprintf(m.device_id, sizeof(m.device_id), "%s", vg_device_id());
  snprintf(m.model, sizeof(m.model), "%s", VG_OTA_MODEL);
  snprintf(m.key_id, sizeof(m.key_id), "fixture");
  m.flags = VG_OTA_MANIFEST_FIXTURE;
  m.issued_at = 1;
  m.expires_at = 10000;
  compute_sha256(g_test_image, img_len, m.sha256);

  assert(vg_ota_staging_stage(&m, mock_source_read, &img_len, err, sizeof(err)) == 0);
}

static void mock_reboot_cb(void *ctx)
{
  int *cnt = (int *)ctx;
  if (cnt != NULL)
    {
      (*cnt)++;
    }
}

/* ── 1. Unauthorized sources test ────────────────────────────────────────── */

static void test_unauthorized_sources(void)
{
  struct vg_ota_staging_info info;
  struct vg_ota_interlock_ports ports;
  char err[64];

  setup_temp_dir();
  stage_valid_offer("T07-UNAUTH", "0.2.0");

  memset(&ports, 0, sizeof(ports));
  ports.reboot = mock_reboot_cb;
  ports.ctx = &g_reboot_called;

  assert(vg_ota_confirm_install(VG_OTA_CONFIRM_SRC_LOCAL_CLI, &ports, err, sizeof(err)) == -EPERM);
  assert(vg_ota_confirm_install(VG_OTA_CONFIRM_SRC_UNKNOWN, &ports, err, sizeof(err)) == -EPERM);
  /* MQTT is unauthorized */
  assert(vg_ota_confirm_install(VG_OTA_CONFIRM_SRC_MQTT, &ports, err, sizeof(err)) == -EPERM);
  assert(strstr(err, "unauthorized_source") != NULL);
  assert(g_reboot_called == 0);

  /* Phone is unauthorized */
  assert(vg_ota_confirm_install(VG_OTA_CONFIRM_SRC_PHONE, &ports, err, sizeof(err)) == -EPERM);
  assert(g_reboot_called == 0);

  /* Voice is unauthorized */
  assert(vg_ota_confirm_install(VG_OTA_CONFIRM_SRC_VOICE, &ports, err, sizeof(err)) == -EPERM);
  assert(g_reboot_called == 0);

  /* Agent is unauthorized */
  assert(vg_ota_confirm_install(VG_OTA_CONFIRM_SRC_AGENT, &ports, err, sizeof(err)) == -EPERM);
  assert(g_reboot_called == 0);

  /* Device must remain in READY_AWAITING_CONFIRM */
  assert(vg_ota_staging_get_info(&info) == 0);
  assert(info.state == VG_OTA_STATE_READY_AWAITING_CONFIRM);

  cleanup_temp_dir();
}

/* ── 2. Interlock: active alarm ─────────────────────────────────────────── */

static int mock_alarm_fail(void *ctx) { (void)ctx; return -EAGAIN; }
static int mock_ok(void *ctx) { (void)ctx; return 0; }

static void test_interlock_active_alarm(void)
{
  struct vg_ota_staging_info info;
  struct vg_ota_interlock_ports ports;
  char err[64];

  setup_temp_dir();
  stage_valid_offer("T07-ALARM", "0.2.0");

  memset(&ports, 0, sizeof(ports));
  ports.check_alarm = mock_alarm_fail;
  ports.check_storage = mock_ok;
  ports.check_digest = mock_ok;
  ports.check_idle = mock_ok;
  ports.check_rj45 = mock_ok;
  ports.reboot = mock_reboot_cb;
  ports.ctx = &g_reboot_called;

  assert(vg_ota_confirm_install(VG_OTA_CONFIRM_SRC_TOUCH, &ports, err, sizeof(err)) == -EAGAIN);
  assert(strcmp(err, "interlock_active_alarm") == 0);
  assert(g_reboot_called == 0);

  /* Stays in READY_AWAITING_CONFIRM, no BCB written */
  assert(vg_ota_staging_get_info(&info) == 0);
  assert(info.state == VG_OTA_STATE_READY_AWAITING_CONFIRM);

  struct stat st;
  char bcb0[160];
  snprintf(bcb0, sizeof(bcb0), "%s/BCB0.BIN", g_temp_dir);
  assert(stat(bcb0, &st) != 0);

  cleanup_temp_dir();
}

/* ── 3. Interlock: storage unwritable ────────────────────────────────────── */

static int mock_storage_fail(void *ctx) { (void)ctx; return -EIO; }

static void test_interlock_storage_unwritable(void)
{
  struct vg_ota_staging_info info;
  struct vg_ota_interlock_ports ports;
  char err[64];

  setup_temp_dir();
  stage_valid_offer("T07-STORAGE", "0.2.0");

  memset(&ports, 0, sizeof(ports));
  ports.check_alarm = mock_ok;
  ports.check_storage = mock_storage_fail;
  ports.check_digest = mock_ok;
  ports.check_idle = mock_ok;
  ports.check_rj45 = mock_ok;
  ports.reboot = mock_reboot_cb;
  ports.ctx = &g_reboot_called;

  assert(vg_ota_confirm_install(VG_OTA_CONFIRM_SRC_BUTTON, &ports, err, sizeof(err)) == -EIO);
  assert(strcmp(err, "interlock_storage_unwritable") == 0);
  assert(g_reboot_called == 0);

  assert(vg_ota_staging_get_info(&info) == 0);
  assert(info.state == VG_OTA_STATE_READY_AWAITING_CONFIRM);

  cleanup_temp_dir();
}

/* ── 4. Interlock: digest mismatch ───────────────────────────────────────── */

static int mock_digest_fail(void *ctx) { (void)ctx; return -EBADMSG; }

static void test_interlock_digest_mismatch(void)
{
  struct vg_ota_staging_info info;
  struct vg_ota_interlock_ports ports;
  char err[64];

  setup_temp_dir();
  stage_valid_offer("T07-DIGEST", "0.2.0");

  memset(&ports, 0, sizeof(ports));
  ports.check_alarm = mock_ok;
  ports.check_storage = mock_ok;
  ports.check_digest = mock_digest_fail;
  ports.check_idle = mock_ok;
  ports.check_rj45 = mock_ok;
  ports.reboot = mock_reboot_cb;
  ports.ctx = &g_reboot_called;

  assert(vg_ota_confirm_install(VG_OTA_CONFIRM_SRC_TOUCH, &ports, err, sizeof(err)) == -EBADMSG);
  assert(strcmp(err, "interlock_digest_mismatch") == 0);
  assert(g_reboot_called == 0);

  assert(vg_ota_staging_get_info(&info) == 0);
  assert(info.state == VG_OTA_STATE_READY_AWAITING_CONFIRM);

  cleanup_temp_dir();
}

/* ── 5. Interlock: device busy ───────────────────────────────────────────── */

static int mock_idle_fail(void *ctx) { (void)ctx; return -EBUSY; }

static void test_interlock_device_busy(void)
{
  struct vg_ota_staging_info info;
  struct vg_ota_interlock_ports ports;
  char err[64];

  setup_temp_dir();
  stage_valid_offer("T07-BUSY", "0.2.0");

  memset(&ports, 0, sizeof(ports));
  ports.check_alarm = mock_ok;
  ports.check_storage = mock_ok;
  ports.check_digest = mock_ok;
  ports.check_idle = mock_idle_fail;
  ports.check_rj45 = mock_ok;
  ports.reboot = mock_reboot_cb;
  ports.ctx = &g_reboot_called;

  assert(vg_ota_confirm_install(VG_OTA_CONFIRM_SRC_TOUCH, &ports, err, sizeof(err)) == -EBUSY);
  assert(strcmp(err, "interlock_device_busy") == 0);
  assert(g_reboot_called == 0);

  assert(vg_ota_staging_get_info(&info) == 0);
  assert(info.state == VG_OTA_STATE_READY_AWAITING_CONFIRM);

  cleanup_temp_dir();
}

/* ── 6. Interlock: RJ45 link down ────────────────────────────────────────── */

static int mock_rj45_fail(void *ctx) { (void)ctx; return -ENETDOWN; }

static void test_interlock_rj45_down(void)
{
  struct vg_ota_staging_info info;
  struct vg_ota_interlock_ports ports;
  char err[64];

  setup_temp_dir();
  stage_valid_offer("T07-RJ45", "0.2.0");

  memset(&ports, 0, sizeof(ports));
  ports.check_alarm = mock_ok;
  ports.check_storage = mock_ok;
  ports.check_digest = mock_ok;
  ports.check_idle = mock_ok;
  ports.check_rj45 = mock_rj45_fail;
  ports.reboot = mock_reboot_cb;
  ports.ctx = &g_reboot_called;

  assert(vg_ota_confirm_install(VG_OTA_CONFIRM_SRC_BUTTON, &ports, err, sizeof(err)) == -ENETDOWN);
  assert(strcmp(err, "interlock_rj45_down") == 0);
  assert(g_reboot_called == 0);

  assert(vg_ota_staging_get_info(&info) == 0);
  assert(info.state == VG_OTA_STATE_READY_AWAITING_CONFIRM);

  cleanup_temp_dir();
}

/* ── 7. Successful confirmation, pending BCB write, and reboot ──────────── */

static void test_confirm_success_writes_pending_bcb(void)
{
  struct vg_ota_staging_info info;
  struct vg_ota_interlock_ports ports;
  struct vg_ota_bcb bcb0, bcb1;
  uint8_t buf[VG_OTA_BCB_FILE_BYTES];
  char path0[160], path1[160];
  char err[64];
  int fd;

  setup_temp_dir();
  stage_valid_offer("T07-SUCCESS", "0.2.0");

  memset(&ports, 0, sizeof(ports));
  ports.check_alarm = mock_ok;
  ports.check_storage = mock_ok;
  ports.check_digest = mock_ok;
  ports.check_idle = mock_ok;
  ports.check_rj45 = mock_ok;
  ports.reboot = mock_reboot_cb;
  ports.ctx = &g_reboot_called;

  assert(vg_ota_confirm_install(VG_OTA_CONFIRM_SRC_TOUCH, &ports, err, sizeof(err)) == 0);
  assert(g_reboot_called == 1);

  /* Verify state transitioned to REBOOTING */
  assert(vg_ota_staging_get_info(&info) == 0);
  assert(info.state == VG_OTA_STATE_REBOOTING);

  /* Verify both BCB0.BIN and BCB1.BIN exist on disk */
  snprintf(path0, sizeof(path0), "%s/BCB0.BIN", g_temp_dir);
  snprintf(path1, sizeof(path1), "%s/BCB1.BIN", g_temp_dir);

  fd = open(path0, O_RDONLY);
  assert(fd >= 0);
  assert(read(fd, buf, sizeof(buf)) == sizeof(buf));
  close(fd);
  assert(vg_ota_bcb_decode(buf, sizeof(buf), &bcb0) == 0);

  fd = open(path1, O_RDONLY);
  assert(fd >= 0);
  assert(read(fd, buf, sizeof(buf)) == sizeof(buf));
  close(fd);
  assert(vg_ota_bcb_decode(buf, sizeof(buf), &bcb1) == 0);

  /* BCB0 should be PENDING with seq=1 and VG_OTA_BCB_LOCAL_CONFIRMED */
  assert(bcb0.state == VG_OTA_BCB_STATE_PENDING);
  assert(bcb0.slot == VG_OTA_BCB_SLOT_B);
  assert(bcb0.seq == 1);
  assert(bcb0.flags == VG_OTA_BCB_LOCAL_CONFIRMED);
  assert(bcb0.trial_count == 0);
  assert(strcmp(bcb0.task_id, "T07-SUCCESS") == 0);
  assert(strcmp(bcb0.version, "0.2.0") == 0);

  /* BCB1 should exist as FAILED with seq=0 */
  assert(bcb1.state == VG_OTA_BCB_STATE_FAILED);
  assert(bcb1.seq == 0);

  cleanup_temp_dir();
}

/* ── 8. Decline and Expire with duplicate task_id suppression ─────────────── */

static void test_decline_and_suppression(void)
{
  struct vg_ota_staging_info info;
  struct stat st;
  char path_bin[160];
  char err[64];

  setup_temp_dir();
  stage_valid_offer("T07-DECLINE", "0.2.0");

  snprintf(path_bin, sizeof(path_bin), "%s/%s", g_temp_dir, VG_OTA_IMAGE_BIN_NAME);
  assert(stat(path_bin, &st) == 0);

  /* Decline offer */
  assert(vg_ota_confirm_decline("T07-DECLINE", "user_declined") == 0);

  /* Files must be cleared */
  assert(stat(path_bin, &st) != 0);
  assert(vg_ota_staging_get_info(&info) == 0);
  assert(info.state == VG_OTA_STATE_DECLINED);

  /* Same task_id is now suppressed: re-staging should be rejected */
  size_t img_len = sizeof(g_test_image);
  struct vg_ota_manifest m;
  memset(&m, 0, sizeof(m));
  m.slot = VG_OTA_BCB_SLOT_B;
  m.image_size = (uint32_t)img_len;
  snprintf(m.task_id, sizeof(m.task_id), "T07-DECLINE");
  snprintf(m.version, sizeof(m.version), "0.2.0");
  snprintf(m.device_id, sizeof(m.device_id), "%s", vg_device_id());
  snprintf(m.model, sizeof(m.model), "%s", VG_OTA_MODEL);
  m.flags = VG_OTA_MANIFEST_FIXTURE;
  m.issued_at = 1; m.expires_at = 2;
  compute_sha256(g_test_image, img_len, m.sha256);

  assert(vg_ota_staging_stage(&m, mock_source_read, &img_len, err, sizeof(err)) == -EALREADY);
  assert(strcmp(err, "task_suppressed") == 0);
  assert(vg_ota_staging_get_info(&info) == 0);
  assert(info.state == VG_OTA_STATE_DECLINED);
  assert(strcmp(info.task_id,"T07-DECLINE") == 0);

  cleanup_temp_dir();
}

static void test_expire_and_suppression(void)
{
  struct vg_ota_staging_info info;
  struct stat st;
  char path_bin[160];
  char err[64];

  setup_temp_dir();
  stage_valid_offer("T07-EXPIRE", "0.2.0");

  snprintf(path_bin, sizeof(path_bin), "%s/%s", g_temp_dir, VG_OTA_IMAGE_BIN_NAME);
  assert(stat(path_bin, &st) == 0);

  /* Expire offer */
  assert(vg_ota_confirm_expire("T07-EXPIRE") == 0);

  /* Files must be cleared */
  assert(stat(path_bin, &st) != 0);
  assert(vg_ota_staging_get_info(&info) == 0);
  assert(info.state == VG_OTA_STATE_EXPIRED);

  /* Same task_id is now suppressed */
  size_t img_len = sizeof(g_test_image);
  struct vg_ota_manifest m;
  memset(&m, 0, sizeof(m));
  m.slot = VG_OTA_BCB_SLOT_B;
  m.image_size = (uint32_t)img_len;
  snprintf(m.task_id, sizeof(m.task_id), "T07-EXPIRE");
  snprintf(m.version, sizeof(m.version), "0.2.0");
  snprintf(m.device_id, sizeof(m.device_id), "%s", vg_device_id());
  snprintf(m.model, sizeof(m.model), "%s", VG_OTA_MODEL);
  m.flags = VG_OTA_MANIFEST_FIXTURE;
  m.issued_at = 1; m.expires_at = 2;
  compute_sha256(g_test_image, img_len, m.sha256);

  assert(vg_ota_staging_stage(&m, mock_source_read, &img_len, err, sizeof(err)) == -EALREADY);
  assert(strcmp(err, "task_suppressed") == 0);

  cleanup_temp_dir();
}

static struct vg_ota_interlock_ports success_ports(void)
{
  struct vg_ota_interlock_ports ports = {
    mock_ok, mock_ok, mock_ok, mock_ok, mock_ok, mock_reboot_cb, &g_reboot_called
  };
  return ports;
}

static void assert_ready_without_pending(void)
{
  struct vg_ota_staging_info info;
  char path[160];
  vg_ota_staging_get_info(&info);
  assert(info.state == VG_OTA_STATE_READY_AWAITING_CONFIRM);
  snprintf(path,sizeof(path),"%s/BCB0.BIN",g_temp_dir);
  assert(access(path,F_OK) != 0);
  assert(g_reboot_called == 0);
}
static void test_pending_write_failures(void)
{
  char err[64];
  int *faults[] = {&g_fail_fsync, &g_fail_rename, &g_fail_read};
  for (unsigned i=0; i<3; i++) {
    setup_temp_dir();
    stage_valid_offer("T07-IO", "0.2.0");
    struct vg_ota_interlock_ports ports = success_ports();
    *faults[i] = 1;
    assert(vg_ota_confirm_install(VG_OTA_CONFIRM_SRC_TOUCH,&ports,err,sizeof(err)) < 0);
    *faults[i] = 0;
    assert_ready_without_pending();
    /* A subsequent fresh user confirmation can retry successfully. */
    assert(vg_ota_confirm_install(VG_OTA_CONFIRM_SRC_TOUCH,&ports,err,sizeof(err)) == 0);
    cleanup_temp_dir();
  }
}
static void test_timeout_and_restart_suppression(void)
{
  struct vg_ota_staging_info info;
  setup_temp_dir();
  stage_valid_offer("T07-TIMEOUT", "0.2.0");
  g_now_ms += VG_OTA_READY_TIMEOUT_MS - 1;
  vg_ota_staging_tick();
  assert_ready_without_pending();
  g_now_ms++;
  vg_ota_staging_tick();
  vg_ota_staging_get_info(&info);
  assert(info.state == VG_OTA_STATE_EXPIRED);
  assert(strcmp(info.task_id,"T07-TIMEOUT") == 0);
  /* Reset RAM, retain the FAT directory. */
  vg_ota_staging_set_dir(g_temp_dir);
  assert(vg_ota_staging_is_suppressed("T07-TIMEOUT"));
  cleanup_temp_dir();
}
static void test_missing_interlock_and_stale_manifest(void)
{
  char err[64],path[160];
  uint8_t buf[512];
  struct vg_ota_manifest m;
  struct vg_ota_interlock_ports ports = success_ports();
  setup_temp_dir();
  stage_valid_offer("T07-BOUND", "0.2.0");
  /* A new offer cannot replace the image under a held confirmation gesture. */
  struct vg_ota_manifest replacement;
  size_t len=sizeof(g_test_image);
  vg_ota_staging_load_manifest(&replacement);
  snprintf(replacement.task_id,sizeof(replacement.task_id),"NEW-TASK");
  assert(vg_ota_staging_stage(&replacement,mock_source_read,&len,err,sizeof(err)) == -EBUSY);
  struct vg_ota_staging_info original;
  vg_ota_staging_get_info(&original);
  assert(strcmp(original.task_id,"T07-BOUND") == 0);
  ports.check_alarm = NULL;
  assert(vg_ota_confirm_install(VG_OTA_CONFIRM_SRC_TOUCH,&ports,err,sizeof(err)) == -EINVAL);
  assert_ready_without_pending();
  ports = success_ports();
  assert(vg_ota_staging_load_manifest(&m) == 0);
  snprintf(m.task_id,sizeof(m.task_id),"T07-SUBSTITUTED");
  assert(vg_ota_manifest_encode(&m,buf) == 0);
  snprintf(path,sizeof(path),"%s/MANIFEST.BIN",g_temp_dir);
  int fd=open(path,O_WRONLY|O_TRUNC);
  assert(fd >= 0 && write(fd,buf,sizeof(buf)) == sizeof(buf));
  close(fd);
  assert(vg_ota_confirm_install(VG_OTA_CONFIRM_SRC_TOUCH,&ports,err,sizeof(err)) == -EBADMSG);
  assert_ready_without_pending();
  cleanup_temp_dir();
}

static void test_suppression_not_evicted_and_torn_journal(void)
{
  char task[32],path[160];
  setup_temp_dir();
  for (unsigned i=0;i<8;i++) {
    snprintf(task,sizeof(task),"T07-DECLINE-%u",i);
    stage_valid_offer(task,"0.2.0");
    assert(vg_ota_confirm_decline(task,"user_declined") == 0);
  }
  vg_ota_staging_set_dir(g_temp_dir);
  for (unsigned i=0;i<8;i++) {
    snprintf(task,sizeof(task),"T07-DECLINE-%u",i);
    assert(vg_ota_staging_is_suppressed(task));
  }
  snprintf(path,sizeof(path),"%s/SUPPRESS.BIN",g_temp_dir);
  int fd=open(path,O_WRONLY|O_APPEND);
  assert(fd >= 0 && write(fd,"torn",4) == 4);
  close(fd);
  assert(vg_ota_staging_is_suppressed("ANY-NEW-OFFER"));
  cleanup_temp_dir();
}

int main(void)
{
  test_suppression_not_evicted_and_torn_journal();
  test_pending_write_failures();
  test_timeout_and_restart_suppression();
  test_missing_interlock_and_stale_manifest();
  test_unauthorized_sources();
  test_interlock_active_alarm();
  test_interlock_storage_unwritable();
  test_interlock_digest_mismatch();
  test_interlock_device_busy();
  test_interlock_rj45_down();
  test_confirm_success_writes_pending_bcb();
  test_decline_and_suppression();
  test_expire_and_suppression();
  printf("test_ota_confirm: OK\n");
  return 0;
}
