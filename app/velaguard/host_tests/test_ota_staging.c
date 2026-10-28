/****************************************************************************
 * app/velaguard/host_tests/test_ota_staging.c
 *
 * Host unit tests for VelaGuard OTA staging storage, manifest validation,
 * atomic rename, and READY_AWAITING_CONFIRM state machine.
 ****************************************************************************/

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "vg_device_id.h"
#include "vg_fw_identity.h"
#include "vg_ota_bcb.h"
#include "vg_ota_manifest.h"
#include "vg_ota_sha256.h"
#include "vg_ota_staging.h"

static char g_temp_dir[128];
static uint8_t g_test_image[32768];

static void setup_temp_dir(void)
{
  char templ[] = "/tmp/vg_staging_test_XXXXXX";
  char *d = mkdtemp(templ);
  assert(d != NULL);
  snprintf(g_temp_dir, sizeof(g_temp_dir), "%s", d);
  vg_ota_staging_set_dir(g_temp_dir);
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
      g_test_image[i] = (uint8_t)(i ^ 0x5a);
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

static ssize_t fail_source_read(void *ctx, uint32_t offset, uint8_t *buf, size_t len)
{
  (void)ctx; (void)buf; (void)len;
  if (offset > 1024) return -1;
  return (ssize_t)len;
}

static void test_space_check(void)
{
  uint64_t free_b, req_b;
  setup_temp_dir();

  /* 1.5MB image should have plenty of space on host */
  assert(vg_ota_staging_check_space(1524800, &free_b, &req_b) == 0);
  assert(free_b >= req_b);

  cleanup_temp_dir();
}

static void test_manifest_validation_matrix(void)
{
  struct vg_ota_manifest m;
  uint8_t actual_digest[32];
  char err[64];

  memset(&m, 0, sizeof(m));
  m.slot = VG_OTA_BCB_SLOT_B;
  m.image_size = 1524800;
  snprintf(m.version, sizeof(m.version), "0.2.0");
  snprintf(m.task_id, sizeof(m.task_id), "TASK-VALID");
  snprintf(m.device_id, sizeof(m.device_id), "%s", vg_device_id());
  snprintf(m.model, sizeof(m.model), "%s", VG_OTA_MODEL);
  snprintf(m.key_id, sizeof(m.key_id), "test-key");
  m.flags = VG_OTA_MANIFEST_FIXTURE;
  m.issued_at = 1000;
  m.expires_at = 2000;
  memset(m.sha256, 0x42, 32);
  memset(actual_digest, 0x42, 32);

  /* Valid manifest passes */
  assert(vg_ota_manifest_validate(&m, actual_digest, err, sizeof(err)) == 0);

  /* 1. Wrong target slot */
  m.slot = VG_OTA_BCB_SLOT_A;
  assert(vg_ota_manifest_validate(&m, actual_digest, err, sizeof(err)) != 0);
  assert(strcmp(err, "bad_target_slot") == 0);
  m.slot = VG_OTA_BCB_SLOT_B;

  /* 2. Wrong device_id */
  snprintf(m.device_id, sizeof(m.device_id), "vg-wrong-device");
  assert(vg_ota_manifest_validate(&m, actual_digest, err, sizeof(err)) != 0);
  assert(strcmp(err, "bad_device_id") == 0);
  snprintf(m.device_id, sizeof(m.device_id), "%s", vg_device_id());

  /* 3. Wrong model */
  snprintf(m.model, sizeof(m.model), "WRONG-MODEL");
  assert(vg_ota_manifest_validate(&m, actual_digest, err, sizeof(err)) != 0);
  assert(strcmp(err, "bad_model") == 0);
  snprintf(m.model, sizeof(m.model), "%s", VG_OTA_MODEL);

  /* 4. Bad image size */
  m.image_size = 4;
  assert(vg_ota_manifest_validate(&m, actual_digest, err, sizeof(err)) != 0);
  assert(strcmp(err, "bad_image_size") == 0);
  m.image_size = 1524800;

  /* 5. Bad version format */
  snprintf(m.version, sizeof(m.version), "not-semver");
  assert(vg_ota_manifest_validate(&m, actual_digest, err, sizeof(err)) != 0);
  assert(strcmp(err, "bad_version_format") == 0);
  snprintf(m.version, sizeof(m.version), "0.2.0");

  /* 6. Bad time window */
  m.expires_at = m.issued_at;
  assert(vg_ota_manifest_validate(&m, actual_digest, err, sizeof(err)) != 0);
  assert(strcmp(err, "bad_time_window") == 0);
  m.expires_at = 2000;

  /* 7. Missing signature on non-fixture */
  m.flags = 0; /* Not fixture */
  memset(m.signature, 0, sizeof(m.signature));
  assert(vg_ota_manifest_validate(&m, actual_digest, err, sizeof(err)) != 0);
  assert(strcmp(err, "signature_missing") == 0);
  m.flags = VG_OTA_MANIFEST_FIXTURE;

  /* 8. Digest mismatch */
  actual_digest[0] ^= 0x5a;
  assert(vg_ota_manifest_validate(&m, actual_digest, err, sizeof(err)) != 0);
  assert(strcmp(err, "digest_mismatch") == 0);
  actual_digest[0] ^= 0x5a;

  assert(vg_ota_manifest_validate(&m, actual_digest, err, sizeof(err)) == 0);
}

static void test_staging_success(void)
{
  struct vg_ota_manifest m;
  struct vg_ota_staging_info info;
  size_t img_len = 16384;
  char err[64];
  char path_tmp[160];
  char path_bin[160];
  char path_man[160];
  char path_state[160];
  struct stat st;

  setup_temp_dir();
  prepare_test_image(img_len);

  memset(&m, 0, sizeof(m));
  m.slot = VG_OTA_BCB_SLOT_B;
  m.image_size = (uint32_t)img_len;
  snprintf(m.version, sizeof(m.version), "0.2.0");
  snprintf(m.task_id, sizeof(m.task_id), "T06-SUCCESS");
  snprintf(m.device_id, sizeof(m.device_id), "%s", vg_device_id());
  snprintf(m.model, sizeof(m.model), "%s", VG_OTA_MODEL);
  snprintf(m.key_id, sizeof(m.key_id), "fixture");
  m.flags = VG_OTA_MANIFEST_FIXTURE;
  m.issued_at = 1;
  m.expires_at = 10000;
  compute_sha256(g_test_image, img_len, m.sha256);

  assert(vg_ota_staging_stage(&m, mock_source_read, &img_len, err, sizeof(err)) == 0);

  /* Verify status info */
  assert(vg_ota_staging_get_info(&info) == 0);
  assert(info.state == VG_OTA_STATE_READY_AWAITING_CONFIRM);
  assert(strcmp(info.version, "0.2.0") == 0);
  assert(strcmp(info.task_id, "T06-SUCCESS") == 0);
  assert(info.image_size == img_len);
  assert(memcmp(info.sha256, m.sha256, 32) == 0);

  /* Verify files on disk */
  snprintf(path_tmp, sizeof(path_tmp), "%s/%s", g_temp_dir, VG_OTA_IMAGE_TMP_NAME);
  snprintf(path_bin, sizeof(path_bin), "%s/%s", g_temp_dir, VG_OTA_IMAGE_BIN_NAME);
  snprintf(path_man, sizeof(path_man), "%s/%s", g_temp_dir, VG_OTA_MANIFEST_BIN_NAME);
  snprintf(path_state, sizeof(path_state), "%s/%s", g_temp_dir, VG_OTA_STATE_BIN_NAME);

  assert(stat(path_tmp, &st) != 0); /* IMAGE.TMP must NOT exist */
  assert(stat(path_bin, &st) == 0); /* IMAGE.BIN must exist */
  assert(st.st_size == (off_t)img_len);
  assert(stat(path_man, &st) == 0); /* MANIFEST.BIN must exist */
  assert(st.st_size == 512);
  assert(stat(path_state, &st) == 0); /* STATE.BIN must exist */

  /* Verify NO pending BCB was created */
  char bcb0[160], bcb1[160];
  snprintf(bcb0, sizeof(bcb0), "%s/BCB0.BIN", g_temp_dir);
  snprintf(bcb1, sizeof(bcb1), "%s/BCB1.BIN", g_temp_dir);
  assert(stat(bcb0, &st) != 0);
  assert(stat(bcb1, &st) != 0);

  cleanup_temp_dir();
}

static void test_staging_digest_mismatch_cleanup(void)
{
  struct vg_ota_manifest m;
  size_t img_len = 8192;
  char err[64];
  char path_tmp[160];
  char path_bin[160];
  struct stat st;

  setup_temp_dir();
  prepare_test_image(img_len);

  memset(&m, 0, sizeof(m));
  m.slot = VG_OTA_BCB_SLOT_B;
  m.image_size = (uint32_t)img_len;
  snprintf(m.version, sizeof(m.version), "0.2.0");
  snprintf(m.task_id, sizeof(m.task_id), "T06-BADHASH");
  snprintf(m.device_id, sizeof(m.device_id), "%s", vg_device_id());
  snprintf(m.model, sizeof(m.model), "%s", VG_OTA_MODEL);
  snprintf(m.key_id, sizeof(m.key_id), "fixture");
  m.flags = VG_OTA_MANIFEST_FIXTURE;
  m.issued_at = 1;
  m.expires_at = 10000;
  compute_sha256(g_test_image, img_len, m.sha256);
  m.sha256[0] ^= 0xaa; /* corrupt hash */

  assert(vg_ota_staging_stage(&m, mock_source_read, &img_len, err, sizeof(err)) != 0);
  assert(strcmp(err, "digest_mismatch") == 0);

  /* Neither TMP nor BIN should remain */
  snprintf(path_tmp, sizeof(path_tmp), "%s/%s", g_temp_dir, VG_OTA_IMAGE_TMP_NAME);
  snprintf(path_bin, sizeof(path_bin), "%s/%s", g_temp_dir, VG_OTA_IMAGE_BIN_NAME);
  assert(stat(path_tmp, &st) != 0);
  assert(stat(path_bin, &st) != 0);

  cleanup_temp_dir();
}

static void test_staging_vector_invalid_cleanup(void)
{
  struct vg_ota_manifest m;
  size_t img_len = 8192;
  char err[64];
  char path_tmp[160];
  char path_bin[160];
  struct stat st;

  setup_temp_dir();
  prepare_test_image(img_len);

  /* Corrupt vector table reset vector: point to slot A (0x90000101) instead of B */
  g_test_image[6] = 0x00;
  g_test_image[7] = 0x90;

  memset(&m, 0, sizeof(m));
  m.slot = VG_OTA_BCB_SLOT_B;
  m.image_size = (uint32_t)img_len;
  snprintf(m.version, sizeof(m.version), "0.2.0");
  snprintf(m.task_id, sizeof(m.task_id), "T06-BADVEC");
  snprintf(m.device_id, sizeof(m.device_id), "%s", vg_device_id());
  snprintf(m.model, sizeof(m.model), "%s", VG_OTA_MODEL);
  snprintf(m.key_id, sizeof(m.key_id), "fixture");
  m.flags = VG_OTA_MANIFEST_FIXTURE;
  m.issued_at = 1;
  m.expires_at = 10000;
  compute_sha256(g_test_image, img_len, m.sha256);

  assert(vg_ota_staging_stage(&m, mock_source_read, &img_len, err, sizeof(err)) != 0);
  assert(strcmp(err, "vector_table_invalid") == 0);

  snprintf(path_tmp, sizeof(path_tmp), "%s/%s", g_temp_dir, VG_OTA_IMAGE_TMP_NAME);
  snprintf(path_bin, sizeof(path_bin), "%s/%s", g_temp_dir, VG_OTA_IMAGE_BIN_NAME);
  assert(stat(path_tmp, &st) != 0);
  assert(stat(path_bin, &st) != 0);

  cleanup_temp_dir();
}

static void test_staging_read_fail_cleanup(void)
{
  struct vg_ota_manifest m;
  size_t img_len = 8192;
  char err[64];
  char path_tmp[160];
  char path_bin[160];
  struct stat st;

  setup_temp_dir();
  prepare_test_image(img_len);

  memset(&m, 0, sizeof(m));
  m.slot = VG_OTA_BCB_SLOT_B;
  m.image_size = (uint32_t)img_len;
  snprintf(m.version, sizeof(m.version), "0.2.0");
  snprintf(m.task_id, sizeof(m.task_id), "T06-FAILREAD");
  snprintf(m.device_id, sizeof(m.device_id), "%s", vg_device_id());
  snprintf(m.model, sizeof(m.model), "%s", VG_OTA_MODEL);
  snprintf(m.key_id, sizeof(m.key_id), "fixture");
  m.flags = VG_OTA_MANIFEST_FIXTURE;
  m.issued_at = 1;
  m.expires_at = 10000;
  compute_sha256(g_test_image, img_len, m.sha256);

  assert(vg_ota_staging_stage(&m, fail_source_read, &img_len, err, sizeof(err)) != 0);

  snprintf(path_tmp, sizeof(path_tmp), "%s/%s", g_temp_dir, VG_OTA_IMAGE_TMP_NAME);
  snprintf(path_bin, sizeof(path_bin), "%s/%s", g_temp_dir, VG_OTA_IMAGE_BIN_NAME);
  assert(stat(path_tmp, &st) != 0);
  assert(stat(path_bin, &st) != 0);

  cleanup_temp_dir();
}

static void test_staging_clear(void)
{
  struct vg_ota_manifest m;
  struct vg_ota_staging_info info;
  size_t img_len = 8192;
  char err[64];
  char path_bin[160];
  struct stat st;

  setup_temp_dir();
  prepare_test_image(img_len);

  memset(&m, 0, sizeof(m));
  m.slot = VG_OTA_BCB_SLOT_B;
  m.image_size = (uint32_t)img_len;
  snprintf(m.version, sizeof(m.version), "0.2.0");
  snprintf(m.task_id, sizeof(m.task_id), "T06-CLR");
  snprintf(m.device_id, sizeof(m.device_id), "%s", vg_device_id());
  snprintf(m.model, sizeof(m.model), "%s", VG_OTA_MODEL);
  snprintf(m.key_id, sizeof(m.key_id), "fixture");
  m.flags = VG_OTA_MANIFEST_FIXTURE;
  m.issued_at = 1;
  m.expires_at = 10000;
  compute_sha256(g_test_image, img_len, m.sha256);

  assert(vg_ota_staging_stage(&m, mock_source_read, &img_len, err, sizeof(err)) == 0);
  snprintf(path_bin, sizeof(path_bin), "%s/%s", g_temp_dir, VG_OTA_IMAGE_BIN_NAME);
  assert(stat(path_bin, &st) == 0);

  assert(vg_ota_staging_clear() == 0);
  assert(stat(path_bin, &st) != 0);
  assert(vg_ota_staging_get_info(&info) == 0);
  assert(info.state == VG_OTA_STATE_IDLE);

  cleanup_temp_dir();
}

int main(void)
{
  test_space_check();
  test_manifest_validation_matrix();
  test_staging_success();
  test_staging_digest_mismatch_cleanup();
  test_staging_vector_invalid_cleanup();
  test_staging_read_fail_cleanup();
  test_staging_clear();
  printf("test_ota_staging: OK\n");
  return 0;
}
