/****************************************************************************
 * app/velaguard/host_tests/test_ota_self_check.c
 *
 * Host unit tests for VelaGuard firmware self-check and rollback state machine.
 ****************************************************************************/

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "vg_fw_identity.h"
#include "vg_ota_bcb.h"
#include "vg_ota_boot.h"
#include "vg_ota_mbox.h"
#include "vg_ota_self_check.h"

static char g_mock_slot = 'B';
static uint32_t g_mock_link_base = VG_FW_SLOT_B_BASE;
static const char *g_mock_version = "0.2.0";
static int g_reboot_count = 0;

char vg_fw_slot(void)
{
  return g_mock_slot;
}

const char *vg_fw_slot_name(void)
{
  return g_mock_slot == 'B' ? "B" : "A";
}

uint32_t vg_fw_link_base(void)
{
  return g_mock_link_base;
}

const char *vg_fw_version(void)
{
  return g_mock_version;
}

int vg_ota_safe_reboot(void)
{
  g_reboot_count++;
  return 0;
}

static char g_temp_dir[128];

static void setup_temp_dir(void)
{
  char templ[] = "/tmp/vg_selfcheck_test_XXXXXX";
  char *d = mkdtemp(templ);
  assert(d != NULL);
  snprintf(g_temp_dir, sizeof(g_temp_dir), "%s", d);
  vg_ota_self_check_set_dir(g_temp_dir);
}

static void cleanup_temp_dir(void)
{
  char cmd[256];
  snprintf(cmd, sizeof(cmd), "rm -rf %s", g_temp_dir);
  (void)system(cmd);
}

static void write_bcb(const char *name, uint32_t seq, uint32_t state,
                      uint32_t slot, uint32_t trials, const char *ver,
                      const char *task)
{
  struct vg_ota_bcb b;
  uint8_t buf[VG_OTA_BCB_FILE_BYTES];
  char path[160];
  FILE *fp;
  uint32_t i;

  memset(&b, 0, sizeof(b));
  b.seq = seq;
  b.state = state;
  b.slot = slot;
  b.trial_count = trials;
  b.image_size = 1500000;
  snprintf(b.version, sizeof(b.version), "%s", ver ? ver : "0.2.0");
  snprintf(b.task_id, sizeof(b.task_id), "%s", task ? task : "T05-TASK");
  for (i = 0; i < sizeof(b.sha256); i++)
    {
      b.sha256[i] = (uint8_t)(i ^ 0x33);
    }

  assert(vg_ota_bcb_encode(&b, buf, sizeof(buf)) == 0);

  snprintf(path, sizeof(path), "%s/%s", g_temp_dir, name);
  fp = fopen(path, "wb");
  assert(fp != NULL);
  assert(fwrite(buf, 1, sizeof(buf), fp) == sizeof(buf));
  fclose(fp);
}

static void read_bcb(const char *name, struct vg_ota_bcb *out)
{
  uint8_t buf[VG_OTA_BCB_FILE_BYTES];
  char path[160];
  FILE *fp;

  snprintf(path, sizeof(path), "%s/%s", g_temp_dir, name);
  fp = fopen(path, "rb");
  assert(fp != NULL);
  assert(fread(buf, 1, sizeof(buf), fp) == sizeof(buf));
  fclose(fp);

  assert(vg_ota_bcb_decode(buf, sizeof(buf), out) == 0);
}

static void test_self_check_needed(void)
{
  setup_temp_dir();

  /* In slot A: never needed */
  g_mock_slot = 'A';
  assert(!vg_ota_self_check_needed());

  /* In slot B with no BCB: not needed */
  g_mock_slot = 'B';
  assert(!vg_ota_self_check_needed());

  /* In slot B with CONFIRMED BCB: not needed */
  write_bcb("BCB0.BIN", 10, VG_OTA_BCB_STATE_CONFIRMED, VG_OTA_BCB_SLOT_B, 0, "0.2.0", "T05");
  assert(!vg_ota_self_check_needed());

  /* In slot B with TRIAL BCB: needed! */
  write_bcb("BCB0.BIN", 11, VG_OTA_BCB_STATE_TRIAL, VG_OTA_BCB_SLOT_B, 1, "0.2.0", "T05");
  assert(vg_ota_self_check_needed() == 1);

  cleanup_temp_dir();
}

static void test_self_check_success(void)
{
  struct vg_ota_bcb bcb;
  char detail[128];
  char state_file[160];
  FILE *fp;
  char line[128];

  setup_temp_dir();
  g_mock_slot = 'B';
  g_mock_link_base = VG_FW_SLOT_B_BASE;
  g_mock_version = "0.2.0";

  VG_OTA_MBOX->magic = VG_OTA_MBOX_MAGIC;
  VG_OTA_MBOX->version = VG_OTA_MBOX_VERSION;
  VG_OTA_MBOX->boot_slot = VG_OTA_BCB_SLOT_B;

  write_bcb("BCB0.BIN", 20, VG_OTA_BCB_STATE_TRIAL, VG_OTA_BCB_SLOT_B, 1, "0.2.0", "T05-SUCCESS");
  write_bcb("BCB1.BIN", 19, VG_OTA_BCB_STATE_FAILED, VG_OTA_BCB_SLOT_B, 0, "0.2.0", "T05-PREV");

  assert(vg_ota_self_check_needed() == 1);
  assert(vg_ota_self_check_run(detail, sizeof(detail)) == 0);
  assert(strstr(detail, "confirmed") != NULL);

  /* BCB1 should now be CONFIRMED with seq=21 */
  read_bcb("BCB1.BIN", &bcb);
  assert(bcb.seq == 21);
  assert(bcb.state == VG_OTA_BCB_STATE_CONFIRMED);
  assert(bcb.slot == VG_OTA_BCB_SLOT_B);
  assert(!strcmp(bcb.version, "0.2.0"));
  assert(!strcmp(bcb.task_id, "T05-SUCCESS"));

  /* STATE.BIN exists and has result=confirmed */
  snprintf(state_file, sizeof(state_file), "%s/%s", g_temp_dir, VG_OTA_STATE_NAME);
  fp = fopen(state_file, "r");
  assert(fp != NULL);
  assert(fgets(line, sizeof(line), fp) != NULL);
  fclose(fp);
  assert(strstr(line, "result=confirmed") != NULL);

  /* After confirmation, self check is no longer needed */
  assert(!vg_ota_self_check_needed());

  /* Second call is idempotent and succeeds without rewriting */
  assert(vg_ota_self_check_run(detail, sizeof(detail)) == 0);

  cleanup_temp_dir();
}

static void test_self_check_failure_inject_file(void)
{
  struct vg_ota_bcb bcb;
  char detail[128];
  char state_file[160];
  FILE *fp;
  char line[128];

  setup_temp_dir();
  g_mock_slot = 'B';
  g_mock_link_base = VG_FW_SLOT_B_BASE;
  g_mock_version = "0.2.0";

  VG_OTA_MBOX->magic = VG_OTA_MBOX_MAGIC;
  VG_OTA_MBOX->version = VG_OTA_MBOX_VERSION;
  VG_OTA_MBOX->boot_slot = VG_OTA_BCB_SLOT_B;

  write_bcb("BCB0.BIN", 30, VG_OTA_BCB_STATE_TRIAL, VG_OTA_BCB_SLOT_B, 1, "0.2.0", "T05-TASK");

  /* Inject failure */
  assert(vg_ota_self_check_set_inject_fail(1) == 0);
  assert(vg_ota_self_check_get_inject_fail() == 1);

  assert(vg_ota_self_check_run(detail, sizeof(detail)) != 0);
  assert(strstr(detail, "FORCE_FAIL") != NULL);

  /* BCB1 should now be FAILED with seq=31 */
  read_bcb("BCB1.BIN", &bcb);
  assert(bcb.seq == 31);
  assert(bcb.state == VG_OTA_BCB_STATE_FAILED);
  assert(bcb.slot == VG_OTA_BCB_SLOT_B);

  /* Trigger file was consumed */
  assert(vg_ota_self_check_get_inject_fail() == 0);

  /* STATE.BIN records failure */
  snprintf(state_file, sizeof(state_file), "%s/%s", g_temp_dir, VG_OTA_STATE_NAME);
  fp = fopen(state_file, "r");
  assert(fp != NULL);
  assert(fgets(line, sizeof(line), fp) != NULL);
  fclose(fp);
  assert(strstr(line, "result=self_check_failed") != NULL);

  cleanup_temp_dir();
}

static void test_self_check_failure_task_id(void)
{
  struct vg_ota_bcb bcb;
  char detail[128];

  setup_temp_dir();
  g_mock_slot = 'B';
  g_mock_link_base = VG_FW_SLOT_B_BASE;
  g_mock_version = "0.2.0";

  VG_OTA_MBOX->magic = VG_OTA_MBOX_MAGIC;
  VG_OTA_MBOX->version = VG_OTA_MBOX_VERSION;
  VG_OTA_MBOX->boot_slot = VG_OTA_BCB_SLOT_B;

  write_bcb("BCB0.BIN", 40, VG_OTA_BCB_STATE_TRIAL, VG_OTA_BCB_SLOT_B, 1, "0.2.0", "FAIL_TRIGGER");

  assert(vg_ota_self_check_run(detail, sizeof(detail)) != 0);
  assert(strstr(detail, "task_id") != NULL);

  read_bcb("BCB1.BIN", &bcb);
  assert(bcb.seq == 41);
  assert(bcb.state == VG_OTA_BCB_STATE_FAILED);

  cleanup_temp_dir();
}

static void test_self_check_version_mismatch(void)
{
  struct vg_ota_bcb bcb;
  char detail[128];

  setup_temp_dir();
  g_mock_slot = 'B';
  g_mock_link_base = VG_FW_SLOT_B_BASE;
  g_mock_version = "0.2.0";

  VG_OTA_MBOX->magic = VG_OTA_MBOX_MAGIC;
  VG_OTA_MBOX->version = VG_OTA_MBOX_VERSION;
  VG_OTA_MBOX->boot_slot = VG_OTA_BCB_SLOT_B;

  /* Image BCB says 0.3.0 while running fw is 0.2.0 */
  write_bcb("BCB0.BIN", 50, VG_OTA_BCB_STATE_TRIAL, VG_OTA_BCB_SLOT_B, 1, "0.3.0", "T05-TASK");

  assert(vg_ota_self_check_run(detail, sizeof(detail)) != 0);
  assert(strstr(detail, "version") != NULL);

  read_bcb("BCB1.BIN", &bcb);
  assert(bcb.seq == 51);
  assert(bcb.state == VG_OTA_BCB_STATE_FAILED);

  cleanup_temp_dir();
}

static void test_boot_integration(void)
{
  setup_temp_dir();
  g_mock_slot = 'B';
  g_mock_link_base = VG_FW_SLOT_B_BASE;
  g_mock_version = "0.2.0";
  g_reboot_count = 0;

  VG_OTA_MBOX->magic = VG_OTA_MBOX_MAGIC;
  VG_OTA_MBOX->version = VG_OTA_MBOX_VERSION;
  VG_OTA_MBOX->boot_slot = VG_OTA_BCB_SLOT_B;

  /* Boot with trial -> passes, no reboot */
  write_bcb("BCB0.BIN", 60, VG_OTA_BCB_STATE_TRIAL, VG_OTA_BCB_SLOT_B, 1, "0.2.0", "T05-PASS");
  assert(vg_ota_self_check_boot() == 0);
  assert(g_reboot_count == 0);

  /* Boot with failure injection -> fails, calls safe reboot */
  write_bcb("BCB0.BIN", 70, VG_OTA_BCB_STATE_TRIAL, VG_OTA_BCB_SLOT_B, 1, "0.2.0", "FAIL_TEST");
  assert(vg_ota_self_check_boot() < 0);
  assert(g_reboot_count == 1);

  cleanup_temp_dir();
}

static void test_manual_confirm_and_rollback(void)
{
  struct vg_ota_bcb bcb;

  setup_temp_dir();
  g_mock_slot = 'B';
  g_mock_link_base = VG_FW_SLOT_B_BASE;
  g_mock_version = "0.2.0";
  g_reboot_count = 0;

  /* Manual confirm */
  write_bcb("BCB0.BIN", 80, VG_OTA_BCB_STATE_TRIAL, VG_OTA_BCB_SLOT_B, 1, "0.2.0", "T05");
  assert(vg_ota_self_check_manual_confirm() == 0);
  read_bcb("BCB1.BIN", &bcb);
  assert(bcb.seq == 81);
  assert(bcb.state == VG_OTA_BCB_STATE_CONFIRMED);

  /* Manual rollback */
  assert(vg_ota_self_check_manual_rollback("test-rollback") == 0);
  assert(g_reboot_count == 1);
  read_bcb("BCB0.BIN", &bcb);
  assert(bcb.seq == 82);
  assert(bcb.state == VG_OTA_BCB_STATE_FAILED);

  cleanup_temp_dir();
}

int main(void)
{
  test_self_check_needed();
  test_self_check_success();
  test_self_check_failure_inject_file();
  test_self_check_failure_task_id();
  test_self_check_version_mismatch();
  test_boot_integration();
  test_manual_confirm_and_rollback();
  printf("test_ota_self_check: OK\n");
  return 0;
}
