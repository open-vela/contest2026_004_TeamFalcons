#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "../vg_config_confirmation.h"

static struct vg_config_pending_preview g_pending;
static int g_load_ret;
static struct vg_config_confirmation_action g_action;
static int g_submit_count;
static int g_submit_ret;

static void fill_text(char *out, size_t size, char value)
{
  memset(out, value, size - 1);
  out[size - 1] = '\0';
}

static void seed_pending(char revision_char, char confirmation_char)
{
  memset(&g_pending, 0, sizeof(g_pending));
  g_pending.valid = true;
  snprintf(g_pending.task_id, sizeof(g_pending.task_id), "task-%c", revision_char);
  fill_text(g_pending.session_id, sizeof(g_pending.session_id), '2');
  g_pending.revision = (uint32_t)(revision_char - '0');
  g_pending.has_base_hash = true;
  fill_text(g_pending.base_hash, sizeof(g_pending.base_hash), 'a');
  fill_text(g_pending.payload_hash, sizeof(g_pending.payload_hash), 'b');
  fill_text(g_pending.candidate_hash, sizeof(g_pending.candidate_hash), 'c');
  fill_text(g_pending.confirmation_id, sizeof(g_pending.confirmation_id),
            confirmation_char);
  g_pending.candidate.schema_version = 1;
  strcpy(g_pending.candidate.mode, "add_sensor");
  g_pending.candidate.baud = 9600;
  g_pending.candidate.slave_address = 7;
  strcpy(g_pending.candidate.model, "TH-01");
  strcpy(g_pending.candidate.manual_id, "manual-1");
  fill_text(g_pending.candidate.manual_sha256,
            sizeof(g_pending.candidate.manual_sha256), 'd');
  g_pending.candidate.evidence_count = 1;
  strcpy(g_pending.candidate.evidence[0].id, "ev1");
  g_pending.candidate.evidence[0].page = 12;
  strcpy(g_pending.candidate.evidence[0].quote,
         "temperature register 0x0000");
  g_pending.candidate.point_count = 1;
  strcpy(g_pending.candidate.points[0].id, "s7_temp");
  strcpy(g_pending.candidate.points[0].name, "温度");
  g_pending.candidate.points[0].addr = 7;
  g_pending.candidate.points[0].fc = 3;
  g_pending.candidate.points[0].reg = 0;
  strcpy(g_pending.candidate.points[0].dtype, "int16");
  g_pending.candidate.points[0].scale = 0.1f;
  strcpy(g_pending.candidate.points[0].unit, "C");
  g_pending.candidate.points[0].evidence_count = 1;
  strcpy(g_pending.candidate.points[0].evidence_ids[0], "ev1");
}

static int load_pending(void *ctx,
                        struct vg_config_pending_preview *out)
{
  (void)ctx;
  if (g_load_ret != 0)
    {
      return g_load_ret;
    }
  *out = g_pending;
  return 0;
}

static int submit(void *ctx,
                  const struct vg_config_confirmation_action *action)
{
  (void)ctx;
  g_submit_count++;
  if (g_submit_ret == 0)
    {
      g_action = *action;
    }
  return g_submit_ret;
}

static void reset_fixture(void)
{
  seed_pending('1', '4');
  g_load_ret = 0;
  g_submit_ret = 0;
  g_submit_count = 0;
  memset(&g_action, 0, sizeof(g_action));
}

static void test_alarm_and_reopen(void)
{
  struct vg_config_confirmation confirmation;
  struct vg_config_confirmation_ports ports = {
    load_pending, submit, NULL
  };

  reset_fixture();
  assert(vg_config_confirmation_init(&confirmation, &ports) == 0);
  assert(vg_config_confirmation_sync(&confirmation, false) == 0);
  assert(vg_config_confirmation_is_visible(&confirmation));
  assert(vg_config_confirmation_sync(&confirmation, true) == 0);
  assert(!vg_config_confirmation_is_visible(&confirmation));
  assert(vg_config_confirmation_submit(
           &confirmation, VG_CONFIG_CONFIRMATION_CONFIRM, true) == -EALREADY);
  assert(g_submit_count == 0);
  assert(vg_config_confirmation_sync(&confirmation, false) == 0);
  assert(vg_config_confirmation_is_visible(&confirmation));
  vg_config_confirmation_deinit(&confirmation);
}

static void test_confirm_is_claimed_once(void)
{
  struct vg_config_confirmation confirmation;
  struct vg_config_confirmation_ports ports = {
    load_pending, submit, NULL
  };
  struct vg_config_confirmation_action old_action;

  reset_fixture();
  assert(vg_config_confirmation_init(&confirmation, &ports) == 0);
  assert(vg_config_confirmation_sync(&confirmation, false) == 0);
  assert(vg_config_confirmation_submit(
           &confirmation, VG_CONFIG_CONFIRMATION_CONFIRM, false) == 0);
  assert(g_submit_count == 1);
  assert(g_action.kind == VG_CONFIG_CONFIRMATION_CONFIRM);
  assert(g_action.request.confirm.revision == 1);
  assert(strcmp(g_action.request.confirm.task_id, "task-1") == 0);
  assert(strcmp(g_action.request.confirm.confirmation_id,
                g_pending.confirmation_id) == 0);
  assert(!vg_config_confirmation_is_visible(&confirmation));
  assert(vg_config_confirmation_submit(
           &confirmation, VG_CONFIG_CONFIRMATION_CONFIRM, false) == -EALREADY);
  assert(g_submit_count == 1);

  old_action = g_action;
  assert(vg_config_confirmation_claim(&confirmation, &old_action));
  assert(!vg_config_confirmation_claim(&confirmation, &old_action));
  vg_config_confirmation_deinit(&confirmation);
}

static void test_alarm_revokes_queued_click(void)
{
  struct vg_config_confirmation confirmation;
  struct vg_config_confirmation_ports ports = {
    load_pending, submit, NULL
  };
  struct vg_config_confirmation_action old_action;

  reset_fixture();
  assert(vg_config_confirmation_init(&confirmation, &ports) == 0);
  assert(vg_config_confirmation_sync(&confirmation, false) == 0);
  assert(vg_config_confirmation_submit(
           &confirmation, VG_CONFIG_CONFIRMATION_CONFIRM, false) == 0);
  old_action = g_action;
  assert(vg_config_confirmation_sync(&confirmation, true) == 0);
  assert(!vg_config_confirmation_is_visible(&confirmation));
  assert(vg_config_confirmation_sync(&confirmation, false) == 0);
  assert(vg_config_confirmation_is_visible(&confirmation));
  assert(!vg_config_confirmation_claim(&confirmation, &old_action));
  vg_config_confirmation_deinit(&confirmation);
}

static void test_new_revision_invalidates_old_action(void)
{
  struct vg_config_confirmation confirmation;
  struct vg_config_confirmation_ports ports = {
    load_pending, submit, NULL
  };
  struct vg_config_confirmation_action old_action;

  reset_fixture();
  assert(vg_config_confirmation_init(&confirmation, &ports) == 0);
  assert(vg_config_confirmation_sync(&confirmation, false) == 0);
  assert(vg_config_confirmation_submit(
           &confirmation, VG_CONFIG_CONFIRMATION_CONFIRM, false) == 0);
  old_action = g_action;

  seed_pending('2', '5');
  assert(vg_config_confirmation_sync(&confirmation, false) == 0);
  assert(vg_config_confirmation_is_visible(&confirmation));
  assert(!vg_config_confirmation_claim(&confirmation, &old_action));
  assert(vg_config_confirmation_submit(
           &confirmation, VG_CONFIG_CONFIRMATION_CONFIRM, false) == 0);
  assert(g_action.request.confirm.revision == 2);
  assert(strcmp(g_action.request.confirm.task_id, "task-2") == 0);
  assert(strcmp(g_action.request.confirm.confirmation_id,
                g_pending.confirmation_id) == 0);
  vg_config_confirmation_deinit(&confirmation);
}

static void test_cancel_and_missing_candidate(void)
{
  struct vg_config_confirmation confirmation;
  struct vg_config_confirmation_ports ports = {
    load_pending, submit, NULL
  };

  reset_fixture();
  assert(vg_config_confirmation_init(&confirmation, &ports) == 0);
  assert(vg_config_confirmation_sync(&confirmation, false) == 0);
  assert(vg_config_confirmation_submit(
           &confirmation, VG_CONFIG_CONFIRMATION_CANCEL, false) == 0);
  assert(g_action.kind == VG_CONFIG_CONFIRMATION_CANCEL);
  assert(g_action.request.cancel.revision == 1);
  assert(strcmp(g_action.request.cancel.confirmation_id,
                g_pending.confirmation_id) == 0);

  g_load_ret = -ENOENT;
  assert(vg_config_confirmation_sync(&confirmation, false) == 0);
  assert(!vg_config_confirmation_is_visible(&confirmation));
  assert(vg_config_confirmation_sync(&confirmation, false) == 0);
  vg_config_confirmation_deinit(&confirmation);
}

static void test_submit_failure_keeps_candidate_retryable(void)
{
  struct vg_config_confirmation confirmation;
  struct vg_config_confirmation_ports ports = {
    load_pending, submit, NULL
  };

  reset_fixture();
  g_submit_ret = -ENOMEM;
  assert(vg_config_confirmation_init(&confirmation, &ports) == 0);
  assert(vg_config_confirmation_sync(&confirmation, false) == 0);
  assert(vg_config_confirmation_submit(
           &confirmation, VG_CONFIG_CONFIRMATION_CONFIRM, false) == -ENOMEM);
  assert(vg_config_confirmation_is_visible(&confirmation));
  g_submit_ret = 0;
  assert(vg_config_confirmation_submit(
           &confirmation, VG_CONFIG_CONFIRMATION_CONFIRM, false) == 0);
  vg_config_confirmation_deinit(&confirmation);
}

int main(void)
{
  _Static_assert(sizeof(struct vg_config_confirmation) < 16384,
                 "confirmation must not duplicate frozen target storage");
  test_alarm_and_reopen();
  test_confirm_is_claimed_once();
  test_alarm_revokes_queued_click();
  test_new_revision_invalidates_old_action();
  test_cancel_and_missing_candidate();
  test_submit_failure_keeps_candidate_retryable();
  puts("test_config_confirmation: PASS");
  return 0;
}
