#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <string.h>

#include "../vg_config_service.h"

struct fixture
{
  uint64_t now;
  uint8_t random_value;
};

static int load_table(void *ctx, struct vg_config_table_snapshot *out)
{
  (void)ctx;
  memset(out, 0, sizeof(*out));
  out->state = VG_CONFIG_TABLE_VALID;
  out->point_count = 1;
  strcpy(out->point_ids[0], "temp");
  out->slave_addresses[0] = 1;
  out->slave_count = 1;
  out->has_base_hash = true;
  memset(out->base_hash, 'a', 64);
  out->base_hash[64] = '\0';
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

static void fill_hash(char out[65], char value)
{
  memset(out, value, 64);
  out[64] = '\0';
}

static struct vg_config_open_request open_request(
  const char *req, const char *task, const char *boot, const char *link,
  uint32_t seq, char hash)
{
  struct vg_config_open_request request;

  memset(&request, 0, sizeof(request));
  strcpy(request.req_id, req);
  fill_hash(request.request_hash, 'd');
  strcpy(request.task_id, task);
  strcpy(request.boot_id, boot);
  strcpy(request.link_id, link);
  request.expected_open_seq = seq;
  request.has_base_hash = true;
  fill_hash(request.base_hash, hash);
  return request;
}

static void fill_cancel(struct vg_config_cancel_request *request,
                        const char *req, const char *task,
                        const char *boot, const char *link,
                        const char *session)
{
  memset(request, 0, sizeof(*request));
  strcpy(request->req_id, req);
  fill_hash(request->request_hash, 'c');
  strcpy(request->task_id, task);
  strcpy(request->boot_id, boot);
  strcpy(request->link_id, link);
  strcpy(request->session_id, session);
}

struct open_thread_arg
{
  struct vg_config_service *service;
  struct vg_config_open_request request;
  pthread_barrier_t *barrier;
  int result;
};

static void *open_thread(void *ctx)
{
  struct open_thread_arg *arg = ctx;
  struct vg_config_task_status status;

  pthread_barrier_wait(arg->barrier);
  arg->result = vg_config_service_open(arg->service, &arg->request,
                                      &status);
  return NULL;
}

static void test_concurrent_open(void)
{
  struct fixture fixture = {1000, 0};
  struct vg_config_service_ports ports = {
    load_table, random_bytes, now_ms, &fixture
  };
  struct vg_config_service service;
  struct open_thread_arg args[2];
  pthread_barrier_t barrier;
  pthread_t threads[2];
  int successes = 0;

  assert(vg_config_service_init(&service, &ports) == 0);
  assert(vg_config_service_new_link(&service) == 0);
  assert(pthread_barrier_init(&barrier, NULL, 3) == 0);
  memset(args, 0, sizeof(args));
  args[0].service = &service;
  args[0].request = open_request("race-a", "race-task-a",
                                 service.boot_id, service.link_id, 1, 'a');
  args[0].barrier = &barrier;
  args[1].service = &service;
  args[1].request = open_request("race-b", "race-task-b",
                                 service.boot_id, service.link_id, 1, 'a');
  args[1].barrier = &barrier;
  assert(pthread_create(&threads[0], NULL, open_thread, &args[0]) == 0);
  assert(pthread_create(&threads[1], NULL, open_thread, &args[1]) == 0);
  pthread_barrier_wait(&barrier);
  pthread_join(threads[0], NULL);
  pthread_join(threads[1], NULL);
  successes += args[0].result == 0;
  successes += args[1].result == 0;
  assert(successes == 1);
  assert(args[0].result == -EBUSY || args[0].result == -ESTALE ||
         args[1].result == -EBUSY || args[1].result == -ESTALE);
  assert(vg_config_gate_current() == VG_CONFIG_GATE_REMOTE);
  assert(vg_config_service_disconnect(&service, "test_done") == 0);
  assert(vg_config_gate_is_idle());
  pthread_barrier_destroy(&barrier);
}

int main(void)
{
  struct fixture fixture = {1000, 0};
  struct vg_config_service_ports ports = {
    load_table, random_bytes, now_ms, &fixture
  };
  struct vg_config_service service;
  struct vg_config_device_snapshot snapshot;
  struct vg_config_task_status status;
  struct vg_config_task_status duplicate;
  struct vg_config_open_request request;
  struct vg_config_cancel_request cancel;
  char boot[33];
  char link[33];

  assert(vg_config_service_init(&service, &ports) == 0);
  assert(vg_config_service_new_link(&service) == 0);
  strcpy(boot, service.boot_id);
  strcpy(link, service.link_id);
  assert(vg_config_service_open(NULL, &request, &status) == -EINVAL);
  assert(vg_config_service_query_task(&service, "missing", &status) == -ENOENT);

  request = open_request("open-bad-boot", "task-bad-boot",
                         "ffffffffffffffffffffffffffffffff", link, 1, 'a');
  assert(vg_config_service_open(&service, &request, &status) == -ESTALE);
  request = open_request("open-bad-link", "task-bad-link",
                         boot, "ffffffffffffffffffffffffffffffff", 1, 'a');
  assert(vg_config_service_open(&service, &request, &status) == -ESTALE);
  request = open_request("open-bad-base", "task-bad-base",
                         boot, link, 1, 'b');
  assert(vg_config_service_open(&service, &request, &status) == -ESTALE);
  assert(vg_config_gate_is_idle());

  request = open_request("open-1", "task-1", boot, link, 1, 'a');
  assert(vg_config_service_open(&service, &request, &status) == 0);
  assert(status.state == VG_CONFIG_SESSION_OPENED);
  assert(status.revision == 0);
  assert(status.state_seq == 1);
  assert(status.has_base_hash);
  assert(status.lease_remaining_ms == VG_CONFIG_LEASE_MS);
  assert(vg_config_gate_current() == VG_CONFIG_GATE_REMOTE);
  assert(vg_config_service_snapshot(&service, &snapshot) == 0);
  assert(snapshot.next_open_seq == 2);
  assert(snapshot.active_gate == VG_CONFIG_GATE_REMOTE);

  assert(vg_config_service_open(&service, &request, &duplicate) == 0);
  assert(strcmp(duplicate.session_id, status.session_id) == 0);
  assert(duplicate.state_seq == 1);
  assert(vg_config_service_snapshot(&service, &snapshot) == 0);
  assert(snapshot.next_open_seq == 2);
  request.request_hash[0] = 'e';
  assert(vg_config_service_open(&service, &request, &duplicate) == -EEXIST);

  request = open_request("open-2", "task-2", boot, link, 2, 'a');
  assert(vg_config_service_open(&service, &request, &duplicate) == -EBUSY);
  assert(vg_config_service_query_task(&service, "task-1", &duplicate) == 0);
  assert(duplicate.state == VG_CONFIG_SESSION_OPENED);

  fixture.now += VG_CONFIG_LEASE_MS;
  assert(vg_config_service_tick(&service) == 0);
  assert(vg_config_gate_is_idle());
  assert(vg_config_service_query_task(&service, "task-1", &duplicate) == 0);
  assert(duplicate.state == VG_CONFIG_SESSION_EXPIRED);
  assert(strcmp(duplicate.error_code, "lease_expired") == 0);
  assert(duplicate.lease_remaining_ms == 0);

  request = open_request("open-3", "task-3", boot, link, 2, 'a');
  assert(vg_config_service_open(&service, &request, &status) == 0);
  fill_cancel(&cancel, "cancel-3", "task-3", boot, link,
              status.session_id);
  assert(vg_config_service_cancel(&service, &cancel, &duplicate) == 0);
  assert(duplicate.state == VG_CONFIG_SESSION_CANCELLED);
  assert(duplicate.state_seq == 2);
  assert(vg_config_gate_is_idle());
  assert(vg_config_service_cancel(&service, &cancel, &duplicate) == 0);
  assert(duplicate.state == VG_CONFIG_SESSION_CANCELLED);

  request = open_request("open-reuse", "task-3", boot, link, 3, 'a');
  assert(vg_config_service_open(&service, &request, &duplicate) == -EBUSY);

  request = open_request("open-4", "task-4", boot, link, 3, 'a');
  assert(vg_config_service_open(&service, &request, &status) == 0);
  assert(vg_config_service_new_link(&service) == 0);
  assert(vg_config_gate_is_idle());
  assert(vg_config_service_query_task(&service, "task-4", &duplicate) == 0);
  assert(duplicate.state == VG_CONFIG_SESSION_CANCELLED);
  assert(strcmp(duplicate.error_code, "link_replaced") == 0);
  assert(strcmp(service.link_id, link) != 0);
  strcpy(link, service.link_id);

  request = open_request("open-5", "task-5", boot, link, 4, 'a');
  assert(vg_config_service_open(&service, &request, &status) == 0);
  assert(vg_config_service_disconnect(&service, "link_lost") == 0);
  assert(vg_config_service_query_task(&service, "task-5", &duplicate) == 0);
  assert(duplicate.state == VG_CONFIG_SESSION_CANCELLED);
  assert(strcmp(duplicate.error_code, "link_lost") == 0);

  service.next_open_seq = UINT32_MAX;
  service.open_seq_exhausted = false;
  request = open_request("open-last", "task-last", boot, link,
                         UINT32_MAX, 'a');
  assert(vg_config_service_open(&service, &request, &status) == 0);
  assert(vg_config_service_snapshot(&service, &snapshot) == 0);
  assert(snapshot.next_open_seq == 0);
  request = open_request("open-after-last", "task-after-last", boot, link,
                         UINT32_MAX, 'a');
  assert(vg_config_service_open(&service, &request, &duplicate) == -ESTALE);
  assert(vg_config_service_disconnect(&service, "test_done") == 0);
  test_concurrent_open();
  return 0;
}
