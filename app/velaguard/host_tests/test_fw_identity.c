/****************************************************************************
 * app/velaguard/host_tests/test_fw_identity.c
 *
 * Version formatting and slot-marker boundaries for the OTA identity layer.
 ****************************************************************************/

#include <stdio.h>
#include <string.h>

#include "vg_fw_identity.h"

static int g_fail;

static void expect(int cond, const char *msg)
{
  if (!cond)
    {
      fprintf(stderr, "FAIL: %s\n", msg);
      g_fail++;
    }
}

static void test_version(void)
{
  char buf[32];

  expect(strcmp(vg_fw_version(), "9.8.7-test") == 0, "version string");
  expect(strcmp(vg_fw_build_id(), "bid-123") == 0, "build id");
  expect(vg_fw_version_valid("9.8.7-test") == 0, "suffix rejected");
  expect(vg_fw_version_valid("0.1.0") == 1, "semver accepted");
  expect(vg_fw_version_valid("10.20.30") == 1, "multi-digit accepted");
  expect(vg_fw_version_valid("0.1") == 0, "missing patch rejected");
  expect(vg_fw_version_valid("0..1") == 0, "empty part rejected");
  expect(vg_fw_version_valid("v0.1.0") == 0, "prefix rejected");
  expect(vg_fw_version_valid("") == 0, "empty rejected");
  expect(vg_fw_version_valid(NULL) == 0, "null rejected");
  expect(vg_fw_identity_format(buf, sizeof(buf)) == 0, "identity format");
  expect(strcmp(buf, "9.8.7-test A") == 0, "identity text");
  expect(vg_fw_identity_format(buf, 4) != 0, "short buffer rejected");
  expect(vg_fw_identity_format(NULL, sizeof(buf)) != 0, "null out rejected");
}

static void test_slot_marker(void)
{
  expect(vg_fw_slot() == 'A', "linked slot A");
  expect(strcmp(vg_fw_slot_name(), "A") == 0, "slot name A");
  expect(vg_fw_link_base() == VG_FW_SLOT_A_BASE, "link base A");

  expect(vg_fw_slot_from_marker('A', VG_FW_SLOT_A_BASE) == 'A',
         "marker A");
  expect(vg_fw_slot_from_marker('B', VG_FW_SLOT_A_BASE) == 'B',
         "marker B wins over base");
  expect(vg_fw_slot_from_marker(0, VG_FW_SLOT_B_BASE) == 'B',
         "invalid marker falls back to B base");
  expect(vg_fw_slot_from_marker(0, VG_FW_SLOT_B_BASE + VG_FW_SLOT_SIZE - 1)
           == 'B',
         "last B byte");
  expect(vg_fw_slot_from_marker(0, VG_FW_SLOT_B_BASE + VG_FW_SLOT_SIZE)
           == 'A',
         "past B slot falls back to A");
  expect(vg_fw_slot_from_marker(0, 0) == 'A', "zero base is A");
}

int main(void)
{
  test_version();
  test_slot_marker();

  if (g_fail != 0)
    {
      fprintf(stderr, "test_fw_identity: %d failure(s)\n", g_fail);
      return 1;
    }

  printf("test_fw_identity: OK\n");
  return 0;
}
