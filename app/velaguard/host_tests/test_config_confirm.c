#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <string.h>

#include "../vg_config_confirmation.h"
#include "../vg_config_confirmation_worker.h"
#include "../vg_config_service.h"
#include "../../../boot/ota_core/vg_ota_sha256.h"

struct fixture
{
  struct vg_config_table_snapshot table;
  uint64_t now;
  uint8_t random_value;
  int commit_result;
  int notify_result;
  int commit_count;
  int notify_count;
  size_t committed_len;
  char committed_json[VG_POINT_TABLE_JSON_MAX];
  struct vg_discover_summary committed_table;
  pthread_mutex_t lock;
  pthread_cond_t changed;
  bool block_commit;
  bool commit_entered;
  bool release_commit;
};

struct confirm_thread_arg
{
  struct vg_config_service *service;
  struct vg_config_confirm_request request;
  struct vg_config_task_status status;
  int result;
};

static const char *good_candidate =
  "{"
  "\"candidate_schema_version\":1,"
  "\"mode\":\"add_sensor\","
  "\"bus\":{\"baud\":9600},"
  "\"sensor\":{\"slave_address\":1,\"model\":\"TH-01\"},"
  "\"manual\":{\"manual_id\":\"manual-01\","
  "\"sha256\":\"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\"},"
  "\"evidence\":[{\"id\":\"ev1\",\"page\":12,"
  "\"quote\":\"Temp register 0x0000, int16, scale 0.1, unit C.\"}],"
  "\"points\":[{\"id\":\"temp\",\"name\":\"Temperature\",\"addr\":1,"
  "\"fc\":3,\"reg\":0,\"qty\":1,\"dtype\":\"int16\",\"scale\":0.1,"
  "\"offset\":0,\"unit\":\"C\","
  "\"source\":{\"evidence_ids\":[\"ev1\"],"
  "\"original_address\":\"0x0000\",\"address_base\":0}}]"
  "}";

static void fill_hash(char out[VG_CONFIG_HASH_LEN], char value)
{
  memset(out, value, VG_CONFIG_HASH_LEN - 1);
  out[VG_CONFIG_HASH_LEN - 1] = '\0';
}

static void digest_to_hex(const uint8_t digest[32],
                          char out[VG_CONFIG_HASH_LEN])
{
  static const char digits[] = "0123456789abcdef";
  size_t i;

  for (i = 0; i < 32; i++)
    {
      out[i * 2] = digits[digest[i] >> 4];
      out[i * 2 + 1] = digits[digest[i] & 0x0f];
    }
  out[64] = '\0';
}

static void sha256_text(const char *text, char out[VG_CONFIG_HASH_LEN])
{
  struct vg_ota_sha256 sha;
  uint8_t digest[32];

  vg_ota_sha256_init(&sha);
  vg_ota_sha256_update(&sha, (const uint8_t *)text, strlen(text));
  vg_ota_sha256_final(&sha, digest);
  digest_to_hex(digest, out);
}

static int load_table(void *ctx, struct vg_config_table_snapshot *out)
{
  struct fixture *fixture = ctx;

  *out = fixture->table;
  return 0;
}

static int random_bytes(void *ctx, uint8_t *out, size_t length)
{
  struct fixture *fixture = ctx;
  size_t i;

  for (i = 0; i < length; i++)
    {
      out[i] = fixture->random_value++;
    }
  return 0;
}

static uint64_t now_ms(void *ctx)
{
  return ((struct fixture *)ctx)->now;
}

static int commit_table(void *ctx, const void *target_json,
                        size_t target_len,
                        const struct vg_discover_summary *target_table)
{
  struct fixture *fixture = ctx;

  if (fixture->block_commit)
    {
      pthread_mutex_lock(&fixture->lock);
      fixture->commit_entered = true;
      pthread_cond_broadcast(&fixture->changed);
      while (!fixture->release_commit)
        {
          pthread_cond_wait(&fixture->changed, &fixture->lock);
        }
      pthread_mutex_unlock(&fixture->lock);
    }

  if (fixture->commit_result != 0)
    {
      return fixture->commit_result;
    }
  assert(target_json != NULL);
  assert(target_table != NULL);
  assert(target_len < sizeof(fixture->committed_json));
  memcpy(fixture->committed_json, target_json, target_len);
  fixture->committed_json[target_len] = '\0';
  fixture->committed_len = target_len;
  fixture->committed_table = *target_table;
  fixture->commit_count++;
  return 0;
}

static int notify_table(void *ctx)
{
  struct fixture *fixture = ctx;

  fixture->notify_count++;
  return fixture->notify_result;
}

static struct vg_config_service_ports make_ports(struct fixture *fixture)
{
  struct vg_config_service_ports ports;

  memset(&ports, 0, sizeof(ports));
  ports.load_table = load_table;
  ports.random_bytes = random_bytes;
  ports.now_ms = now_ms;
  ports.ctx = fixture;
  ports.commit_table = commit_table;
  ports.notify_table = notify_table;
  return ports;
}

static void init_fixture(struct fixture *fixture)
{
  memset(fixture, 0, sizeof(*fixture));
  fixture->now = 1000;
  fixture->table.state = VG_CONFIG_TABLE_VALID;
  fixture->table.point_count = 1;
  strcpy(fixture->table.point_ids[0], "external");
  fixture->table.slave_addresses[0] = 2;
  fixture->table.slave_count = 1;
  fixture->table.has_base_hash = true;
  fill_hash(fixture->table.base_hash, 'a');
  fixture->table.current.n_points = 1;
  strcpy(fixture->table.current.devpath, "/dev/rs485");
  fixture->table.current.baud = 9600;
  strcpy(fixture->table.current.points[0].id, "external");
  strcpy(fixture->table.current.points[0].name, "External");
  fixture->table.current.points[0].addr = 2;
  fixture->table.current.points[0].fc = 3;
  fixture->table.current.points[0].reg = 5;
  fixture->table.current.points[0].qty = 1;
  strcpy(fixture->table.current.points[0].dtype, "uint16");
  fixture->table.current.points[0].scale = 1;
  fixture->table.current.points[0].fail_n = 3;
  assert(pthread_mutex_init(&fixture->lock, NULL) == 0);
  assert(pthread_cond_init(&fixture->changed, NULL) == 0);
}

static struct vg_config_confirm_request make_confirmation(
  const struct vg_config_pending_candidate *pending)
{
  struct vg_config_confirm_request request;

  memset(&request, 0, sizeof(request));
  strcpy(request.task_id, pending->task_id);
  strcpy(request.session_id, pending->session_id);
  request.revision = pending->revision;
  request.has_base_hash = pending->has_base_hash;
  strcpy(request.base_hash, pending->base_hash);
  strcpy(request.payload_hash, pending->payload_hash);
  strcpy(request.candidate_hash, pending->candidate_hash);
  strcpy(request.confirmation_id, pending->confirmation_id);
  return request;
}

static void setup_pending(struct fixture *fixture,
                          struct vg_config_service *service,
                          struct vg_config_task_status *status,
                          struct vg_config_pending_candidate *pending,
                          struct vg_config_confirm_request *confirmation)
{
  struct vg_config_service_ports ports;
  struct vg_config_open_request open_request;
  struct vg_config_put_request put;

  init_fixture(fixture);
  ports = make_ports(fixture);
  assert(vg_config_gate_is_idle());
  assert(vg_config_service_init(service, &ports) == 0);
  assert(vg_config_service_new_link(service) == 0);

  memset(&open_request, 0, sizeof(open_request));
  strcpy(open_request.req_id, "open-confirm");
  fill_hash(open_request.request_hash, 'd');
  strcpy(open_request.task_id, "confirm-task");
  strcpy(open_request.boot_id, service->boot_id);
  strcpy(open_request.link_id, service->link_id);
  open_request.expected_open_seq = 1;
  open_request.has_base_hash = true;
  strcpy(open_request.base_hash, fixture->table.base_hash);
  assert(vg_config_service_open(service, &open_request, status) == 0);

  memset(&put, 0, sizeof(put));
  strcpy(put.req_id, "put-confirm");
  fill_hash(put.request_hash, 'e');
  strcpy(put.task_id, "confirm-task");
  strcpy(put.boot_id, service->boot_id);
  strcpy(put.link_id, service->link_id);
  strcpy(put.session_id, status->session_id);
  put.expected_revision = 0;
  put.revision = 1;
  put.has_base_hash = true;
  strcpy(put.base_hash, fixture->table.base_hash);
  sha256_text(good_candidate, put.payload_hash);
  put.candidate_json = good_candidate;
  put.candidate_json_len = strlen(good_candidate);
  assert(vg_config_service_put(service, &put, status) == 0);
  assert(status->state == VG_CONFIG_SESSION_AWAITING_CONFIRM);
  assert(vg_config_service_pending_candidate(service, pending) == 0);
  *confirmation = make_confirmation(pending);
}

static void test_applied_and_old_event(void)
{
  struct fixture fixture;
  struct vg_config_service service;
  struct vg_config_task_status status;
  struct vg_config_pending_candidate pending;
  struct vg_config_confirm_request confirmation;
  struct vg_config_confirm_request old_confirmation;

  setup_pending(&fixture, &service, &status, &pending, &confirmation);
  old_confirmation = confirmation;
  old_confirmation.candidate_hash[0] =
    old_confirmation.candidate_hash[0] == '0' ? '1' : '0';
  assert(vg_config_service_confirm(&service, &old_confirmation, &status)
         == -ESTALE);
  assert(status.state == VG_CONFIG_SESSION_AWAITING_CONFIRM);
  assert(fixture.commit_count == 0);
  assert(fixture.notify_count == 0);

  assert(vg_config_service_confirm(&service, &confirmation, &status) == 0);
  assert(status.state == VG_CONFIG_SESSION_APPLIED);
  assert(fixture.commit_count == 1);
  assert(fixture.notify_count == 1);
  assert(fixture.committed_len == pending.target_len);
  assert(strcmp(fixture.committed_json, pending.target_json) == 0);
  assert(fixture.committed_table.n_points == 2);
  assert(strcmp(fixture.committed_table.points[1].id, "temp") == 0);
  assert(vg_config_gate_is_idle());

  assert(vg_config_service_confirm(&service, &confirmation, &status)
         == -EALREADY);
  assert(status.state == VG_CONFIG_SESSION_APPLIED);
  assert(fixture.commit_count == 1);
  assert(fixture.notify_count == 1);
}

static void test_base_change(void)
{
  struct fixture fixture;
  struct vg_config_service service;
  struct vg_config_task_status status;
  struct vg_config_pending_candidate pending;
  struct vg_config_confirm_request confirmation;

  setup_pending(&fixture, &service, &status, &pending, &confirmation);
  fill_hash(fixture.table.base_hash, 'b');
  assert(vg_config_service_confirm(&service, &confirmation, &status)
         == -ESTALE);
  assert(status.state == VG_CONFIG_SESSION_FAILED);
  assert(strcmp(status.error_code, "base_conflict") == 0);
  assert(fixture.commit_count == 0);
  assert(fixture.notify_count == 0);
  assert(vg_config_gate_is_idle());
}

static void test_commit_failure(void)
{
  struct fixture fixture;
  struct vg_config_service service;
  struct vg_config_task_status status;
  struct vg_config_pending_candidate pending;
  struct vg_config_confirm_request confirmation;

  setup_pending(&fixture, &service, &status, &pending, &confirmation);
  fixture.commit_result = -ENOSPC;
  assert(vg_config_service_confirm(&service, &confirmation, &status)
         == -ENOSPC);
  assert(status.state == VG_CONFIG_SESSION_FAILED);
  assert(strcmp(status.error_code, "storage_error") == 0);
  assert(fixture.commit_count == 0);
  assert(fixture.notify_count == 0);
  assert(vg_config_gate_is_idle());
}

static void test_ownership_lost(void)
{
  struct fixture fixture;
  struct vg_config_service service;
  struct vg_config_task_status status;
  struct vg_config_pending_candidate pending;
  struct vg_config_confirm_request confirmation;
  uint32_t token;

  setup_pending(&fixture, &service, &status, &pending, &confirmation);
  token = service.remote_gate_token;
  assert(vg_config_gate_release(VG_CONFIG_GATE_REMOTE, token) == 0);
  assert(vg_config_service_confirm(&service, &confirmation, &status)
         == -ESTALE);
  assert(status.state == VG_CONFIG_SESSION_FAILED);
  assert(strcmp(status.error_code, "ownership_lost") == 0);
  assert(fixture.commit_count == 0);
}

static void *confirm_thread(void *ctx)
{
  struct confirm_thread_arg *arg = ctx;

  arg->result = vg_config_service_confirm(arg->service, &arg->request,
                                         &arg->status);
  return NULL;
}

static void test_cancel_competition(void)
{
  struct fixture fixture;
  struct vg_config_service service;
  struct vg_config_task_status status;
  struct vg_config_pending_candidate pending;
  struct vg_config_confirm_request confirmation;
  struct vg_config_cancel_request cancel;
  struct confirm_thread_arg arg;
  pthread_t thread;

  setup_pending(&fixture, &service, &status, &pending, &confirmation);
  fixture.block_commit = true;
  memset(&arg, 0, sizeof(arg));
  arg.service = &service;
  arg.request = confirmation;
  assert(pthread_create(&thread, NULL, confirm_thread, &arg) == 0);

  pthread_mutex_lock(&fixture.lock);
  while (!fixture.commit_entered)
    {
      pthread_cond_wait(&fixture.changed, &fixture.lock);
    }
  pthread_mutex_unlock(&fixture.lock);

  memset(&cancel, 0, sizeof(cancel));
  strcpy(cancel.req_id, "cancel-too-late");
  fill_hash(cancel.request_hash, 'f');
  strcpy(cancel.task_id, pending.task_id);
  strcpy(cancel.boot_id, service.boot_id);
  strcpy(cancel.link_id, service.link_id);
  strcpy(cancel.session_id, pending.session_id);
  assert(vg_config_service_cancel(&service, &cancel, &status) == -EALREADY);
  assert(status.state == VG_CONFIG_SESSION_COMMITTING);

  pthread_mutex_lock(&fixture.lock);
  fixture.release_commit = true;
  pthread_cond_broadcast(&fixture.changed);
  pthread_mutex_unlock(&fixture.lock);
  pthread_join(thread, NULL);
  assert(arg.result == 0);
  assert(arg.status.state == VG_CONFIG_SESSION_APPLIED);
  assert(fixture.commit_count == 1);
  assert(fixture.notify_count == 1);
  assert(vg_config_gate_is_idle());
}

static void test_cancel_before_confirm(void)
{
  struct fixture fixture;
  struct vg_config_service service;
  struct vg_config_task_status status;
  struct vg_config_pending_candidate pending;
  struct vg_config_confirm_request confirmation;
  struct vg_config_cancel_request cancel;

  setup_pending(&fixture, &service, &status, &pending, &confirmation);
  memset(&cancel, 0, sizeof(cancel));
  strcpy(cancel.req_id, "cancel-first");
  fill_hash(cancel.request_hash, 'c');
  strcpy(cancel.task_id, pending.task_id);
  strcpy(cancel.boot_id, service.boot_id);
  strcpy(cancel.link_id, service.link_id);
  strcpy(cancel.session_id, pending.session_id);
  assert(vg_config_service_cancel(&service, &cancel, &status) == 0);
  assert(status.state == VG_CONFIG_SESSION_CANCELLED);
  assert(vg_config_service_confirm(&service, &confirmation, &status)
         == -EALREADY);
  assert(fixture.commit_count == 0);
  assert(fixture.notify_count == 0);
  assert(vg_config_gate_is_idle());
}

static void test_local_cancel_and_stale_token(void)
{
  struct fixture fixture;
  struct vg_config_service service;
  struct vg_config_task_status status;
  struct vg_config_pending_candidate pending;
  struct vg_config_confirm_request confirmation;
  struct vg_config_local_cancel_request cancel;

  setup_pending(&fixture, &service, &status, &pending, &confirmation);
  memset(&cancel, 0, sizeof(cancel));
  strcpy(cancel.task_id, pending.task_id);
  strcpy(cancel.session_id, pending.session_id);
  cancel.revision = pending.revision;
  strcpy(cancel.confirmation_id, pending.confirmation_id);
  cancel.confirmation_id[0] =
    cancel.confirmation_id[0] == '0' ? '1' : '0';
  assert(vg_config_service_cancel_local(&service, &cancel, &status)
         == -ESTALE);
  assert(status.state == VG_CONFIG_SESSION_AWAITING_CONFIRM);
  assert(fixture.commit_count == 0);

  strcpy(cancel.confirmation_id, pending.confirmation_id);
  cancel.revision = pending.revision + 1;
  assert(vg_config_service_cancel_local(&service, &cancel, &status)
         == -ESTALE);
  cancel.revision = pending.revision;
  assert(vg_config_service_cancel_local(&service, &cancel, &status) == 0);
  assert(status.state == VG_CONFIG_SESSION_CANCELLED);
  assert(strcmp(status.error_code, "local_cancelled") == 0);
  assert(vg_config_service_pending_candidate(&service, &pending) == -ENOENT);
  assert(vg_config_service_confirm(&service, &confirmation, &status)
         == -EALREADY);
  assert(fixture.commit_count == 0);
  assert(fixture.notify_count == 0);
  assert(vg_config_gate_is_idle());
}

struct local_ui_capture
{
  struct vg_config_service *service;
  bool queued;
  struct vg_config_confirmation_action action;
};

static int local_ui_load(void *ctx,
                         struct vg_config_pending_preview *out)
{
  struct local_ui_capture *capture = ctx;

  return vg_config_service_pending_preview(capture->service, out);
}

static int local_ui_submit(
  void *ctx,
  const struct vg_config_confirmation_action *action)
{
  struct local_ui_capture *capture = ctx;

  capture->queued = true;
  capture->action = *action;
  return 0;
}

static void test_local_ui_event_commits_exact_candidate(void)
{
  struct fixture fixture;
  struct vg_config_service service;
  struct vg_config_task_status status;
  struct vg_config_pending_candidate pending;
  struct vg_config_confirm_request confirmation;
  struct vg_config_confirmation ui;
  struct local_ui_capture capture;
  struct vg_config_confirmation_ports ports = {
    local_ui_load, local_ui_submit, &capture
  };

  setup_pending(&fixture, &service, &status, &pending, &confirmation);
  memset(&capture, 0, sizeof(capture));
  capture.service = &service;
  assert(vg_config_confirmation_init(&ui, &ports) == 0);
  assert(vg_config_confirmation_sync(&ui, false) == 0);
  assert(vg_config_confirmation_is_visible(&ui));
  assert(vg_config_confirmation_submit(
           &ui, VG_CONFIG_CONFIRMATION_CONFIRM, false) == 0);
  assert(capture.queued);
  assert(capture.action.kind == VG_CONFIG_CONFIRMATION_CONFIRM);
  assert(fixture.commit_count == 0);
  assert(vg_config_confirmation_submit(
           &ui, VG_CONFIG_CONFIRMATION_CONFIRM, false) == -EALREADY);
  assert(vg_config_confirmation_claim(&ui, &capture.action));
  assert(vg_config_service_confirm(
           &service, &capture.action.request.confirm, &status) == 0);
  assert(status.state == VG_CONFIG_SESSION_APPLIED);
  assert(fixture.commit_count == 1);
  assert(fixture.notify_count == 1);
  assert(strcmp(fixture.committed_json, pending.target_json) == 0);
  assert(!vg_config_confirmation_claim(&ui, &capture.action));
  vg_config_confirmation_deinit(&ui);
}

static void test_worker_commits_exact_candidate(void)
{
  struct fixture fixture;
  struct vg_config_service service;
  struct vg_config_task_status status;
  struct vg_config_pending_candidate pending;
  struct vg_config_confirm_request confirmation;
  struct vg_config_confirmation ui;
  struct vg_config_confirmation_worker worker;
  struct vg_config_confirmation_ports ports;

  setup_pending(&fixture, &service, &status, &pending, &confirmation);
  assert(vg_config_confirmation_init(&ui, NULL) == 0);
  assert(vg_config_confirmation_worker_init(
           &worker, &ui, &service, NULL, NULL) == 0);
  assert(vg_config_confirmation_worker_ports(&worker, &ports) == 0);
  assert(vg_config_confirmation_bind(&ui, &ports) == 0);
  assert(vg_config_confirmation_worker_start(&worker) == 0);
  assert(vg_config_confirmation_sync(&ui, false) == 0);
  assert(vg_config_confirmation_is_visible(&ui));
  assert(vg_config_confirmation_submit(
           &ui, VG_CONFIG_CONFIRMATION_CONFIRM, false) == 0);
  assert(vg_config_confirmation_worker_stop(&worker) == 0);

  assert(vg_config_service_query_task(&service, confirmation.task_id,
                                      &status) == 0);
  assert(status.state == VG_CONFIG_SESSION_APPLIED);
  assert(fixture.commit_count == 1);
  assert(fixture.notify_count == 1);
  assert(strcmp(fixture.committed_json, pending.target_json) == 0);
  vg_config_confirmation_deinit(&ui);
}

static bool alarm_is_active(void *ctx)
{
  return *(bool *)ctx;
}

static void test_worker_alarm_revokes_queued_action(void)
{
  struct fixture fixture;
  struct vg_config_service service;
  struct vg_config_task_status status;
  struct vg_config_pending_candidate pending;
  struct vg_config_confirm_request confirmation;
  struct vg_config_confirmation ui;
  struct vg_config_confirmation_worker worker;
  struct vg_config_confirmation_ports ports;
  struct vg_config_local_cancel_request cancel;
  bool alarm = true;

  setup_pending(&fixture, &service, &status, &pending, &confirmation);
  assert(vg_config_confirmation_init(&ui, NULL) == 0);
  assert(vg_config_confirmation_worker_init(
           &worker, &ui, &service, alarm_is_active, &alarm) == 0);
  assert(vg_config_confirmation_worker_ports(&worker, &ports) == 0);
  assert(vg_config_confirmation_bind(&ui, &ports) == 0);
  assert(vg_config_confirmation_worker_start(&worker) == 0);
  assert(vg_config_confirmation_sync(&ui, false) == 0);
  assert(vg_config_confirmation_submit(
           &ui, VG_CONFIG_CONFIRMATION_CONFIRM, false) == 0);
  assert(vg_config_confirmation_worker_stop(&worker) == 0);

  assert(vg_config_service_query_task(&service, confirmation.task_id,
                                      &status) == 0);
  assert(status.state == VG_CONFIG_SESSION_AWAITING_CONFIRM);
  assert(fixture.commit_count == 0);
  assert(fixture.notify_count == 0);
  assert(!vg_config_confirmation_is_visible(&ui));

  memset(&cancel, 0, sizeof(cancel));
  strcpy(cancel.task_id, pending.task_id);
  strcpy(cancel.session_id, pending.session_id);
  cancel.revision = pending.revision;
  strcpy(cancel.confirmation_id, pending.confirmation_id);
  assert(vg_config_service_cancel_local(&service, &cancel, &status) == 0);
  assert(vg_config_gate_is_idle());
  vg_config_confirmation_deinit(&ui);
}

static void test_pending_getter_expires_candidate(void)
{
  struct fixture fixture;
  struct vg_config_service service;
  struct vg_config_task_status status;
  struct vg_config_pending_candidate pending;
  struct vg_config_confirm_request confirmation;

  setup_pending(&fixture, &service, &status, &pending, &confirmation);
  fixture.now += VG_CONFIG_LEASE_MS;
  assert(vg_config_service_pending_candidate(&service, &pending) == -ENOENT);
  assert(vg_config_service_query_task(&service, confirmation.task_id,
                                     &status) == 0);
  assert(status.state == VG_CONFIG_SESSION_EXPIRED);
  assert(vg_config_gate_is_idle());
}

int main(void)
{
  test_applied_and_old_event();
  test_base_change();
  test_commit_failure();
  test_ownership_lost();
  test_cancel_competition();
  test_cancel_before_confirm();
  test_local_cancel_and_stale_token();
  test_local_ui_event_commits_exact_candidate();
  test_worker_commits_exact_candidate();
  test_worker_alarm_revokes_queued_action();
  test_pending_getter_expires_candidate();
  return 0;
}
