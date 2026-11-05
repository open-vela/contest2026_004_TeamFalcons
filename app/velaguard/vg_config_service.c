/****************************************************************************
 * app/velaguard/vg_config_service.c
 ****************************************************************************/

#include <errno.h>
#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "vg_config_candidate.h"
#include "vg_config_service.h"
#include "../../boot/ota_core/vg_ota_sha256.h"

static void digest_to_hex(const uint8_t digest[16], char out[33])
{
  static const char digits[] = "0123456789abcdef";
  size_t i;

  for (i = 0; i < 16; i++)
    {
      out[i * 2] = digits[digest[i] >> 4];
      out[i * 2 + 1] = digits[digest[i] & 0x0f];
    }

  out[32] = '\0';
}

static void digest32_to_hex(const uint8_t digest[32], char out[65])
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

static int payload_hash(const void *data, size_t length, char out[65])
{
  struct vg_ota_sha256 sha;
  uint8_t digest[32];

  if (data == NULL || length == 0 || out == NULL)
    {
      return -EINVAL;
    }

  vg_ota_sha256_init(&sha);
  vg_ota_sha256_update(&sha, data, (uint32_t)length);
  vg_ota_sha256_final(&sha, digest);
  digest32_to_hex(digest, out);
  memset(digest, 0, sizeof(digest));
  memset(&sha, 0, sizeof(sha));
  return 0;
}

static int make_identity(struct vg_config_service *service, char out[33])
{
  uint8_t digest[16];
  int ret;

  ret = service->ports.random_bytes(service->ports.ctx, digest,
                                    sizeof(digest));
  if (ret != 0)
    {
      memset(digest, 0, sizeof(digest));
      return ret;
    }

  digest_to_hex(digest, out);
  memset(digest, 0, sizeof(digest));
  return 0;
}

static bool valid_text_id(const char *value)
{
  size_t i;
  size_t length;

  if (value == NULL)
    {
      return false;
    }

  length = strlen(value);
  if (length < 1 || length >= VG_CONFIG_ID_MAX)
    {
      return false;
    }

  for (i = 0; i < length; i++)
    {
      unsigned char c = (unsigned char)value[i];
      if (!(isalnum(c) || c == '_' || c == '-'))
        {
          return false;
        }
    }

  return true;
}

static bool valid_hex32(const char *value)
{
  size_t i;

  if (value == NULL || strlen(value) != 32)
    {
      return false;
    }

  for (i = 0; i < 32; i++)
    {
      if (!isxdigit((unsigned char)value[i]) ||
          isupper((unsigned char)value[i]))
        {
          return false;
        }
    }

  return true;
}

static bool valid_hash(const char *value)
{
  size_t i;

  if (value == NULL || strlen(value) != 64)
    {
      return false;
    }

  for (i = 0; i < 64; i++)
    {
      if (!isxdigit((unsigned char)value[i]) ||
          isupper((unsigned char)value[i]))
        {
          return false;
        }
    }

  return true;
}

static void set_error(struct vg_config_task_status *status,
                      const char *code, bool retryable)
{
  snprintf(status->error_code, sizeof(status->error_code), "%s", code);
  status->retryable = retryable;
}

static void expire_if_due(struct vg_config_service *service, bool *release)
{
  uint64_t now;

  *release = false;
  if (!service->session_active)
    {
      return;
    }

  now = service->ports.now_ms(service->ports.ctx);
  if (now < service->lease_deadline_ms)
    {
      return;
    }

  service->session.state = VG_CONFIG_SESSION_EXPIRED;
  service->session.lease_remaining_ms = 0;
  service->session.state_seq++;
  set_error(&service->session, "lease_expired", false);
  service->session_active = false;
  *release = service->remote_gate_token != 0;
}

static void fill_status(struct vg_config_service *service,
                        const struct vg_config_task_status *source,
                        struct vg_config_task_status *out)
{
  uint64_t now = service->ports.now_ms(service->ports.ctx);

  *out = *source;
  if (service->session_active &&
      strcmp(out->task_id, service->session.task_id) == 0)
    {
      out->lease_remaining_ms =
        now < service->lease_deadline_ms ?
        service->lease_deadline_ms - now : 0;
    }
  else
    {
      out->lease_remaining_ms = 0;
    }
}

static struct vg_config_cached_request *
find_request(struct vg_config_service *service, const char *req_id)
{
  size_t i;

  for (i = 0; i < VG_CONFIG_REQUEST_CACHE; i++)
    {
      if (service->requests[i].used &&
          strcmp(service->requests[i].req_id, req_id) == 0)
        {
          return &service->requests[i];
        }
    }

  return NULL;
}

static void cache_request(struct vg_config_service *service,
                          const char *req_id, const char *request_hash,
                          int result,
                          const struct vg_config_task_status *status)
{
  struct vg_config_cached_request *entry =
    &service->requests[service->request_cursor];

  memset(entry, 0, sizeof(*entry));
  entry->used = true;
  memcpy(entry->req_id, req_id, sizeof(entry->req_id));
  memcpy(entry->request_hash, request_hash, sizeof(entry->request_hash));
  entry->result = result;
  entry->status = *status;
  service->request_cursor =
    (service->request_cursor + 1) % VG_CONFIG_REQUEST_CACHE;
}

static bool task_was_used(struct vg_config_service *service,
                          const char *task_id)
{
  uint32_t i;

  for (i = 0; i < service->used_task_count; i++)
    {
      if (strcmp(service->used_tasks[i], task_id) == 0)
        {
          return true;
        }
    }

  return false;
}

static bool remember_task(struct vg_config_service *service,
                          const char *task_id)
{
  if (service->used_task_count >= VG_CONFIG_USED_TASKS)
    {
      return false;
    }

  memcpy(service->used_tasks[service->used_task_count], task_id,
         sizeof(service->used_tasks[0]));
  service->used_task_count++;
  return true;
}

int vg_config_service_init(struct vg_config_service *service,
                           const struct vg_config_service_ports *ports)
{
  int ret;

  if (service == NULL || ports == NULL || ports->load_table == NULL ||
      ports->random_bytes == NULL || ports->now_ms == NULL)
    {
      return -EINVAL;
    }

  memset(service, 0, sizeof(*service));
  service->ports = *ports;
  ret = pthread_mutex_init(&service->lock, NULL);
  if (ret != 0)
    {
      return -ret;
    }

  ret = make_identity(service, service->boot_id);
  if (ret != 0)
    {
      pthread_mutex_destroy(&service->lock);
      memset(service, 0, sizeof(*service));
      return ret;
    }

  service->next_open_seq = 1;
  service->started_ms = service->ports.now_ms(service->ports.ctx);
  service->initialized = true;
  return 0;
}

int vg_config_service_new_link(struct vg_config_service *service)
{
  char link_id[VG_CONFIG_HEX32_LEN];
  int ret;

  if (service == NULL || !service->initialized)
    {
      return -EINVAL;
    }

  ret = vg_config_service_disconnect(service, "link_replaced");
  if (ret != 0)
    {
      return ret;
    }

  ret = make_identity(service, link_id);
  if (ret != 0)
    {
      return ret;
    }

  pthread_mutex_lock(&service->lock);
  memcpy(service->link_id, link_id, sizeof(service->link_id));
  pthread_mutex_unlock(&service->lock);
  memset(link_id, 0, sizeof(link_id));
  return 0;
}

int vg_config_service_disconnect(struct vg_config_service *service,
                                 const char *reason)
{
  uint32_t gate_token = 0;
  bool release = false;

  if (service == NULL || !service->initialized || reason == NULL)
    {
      return -EINVAL;
    }

  pthread_mutex_lock(&service->lock);
  if (service->session_active)
    {
      service->session.state = VG_CONFIG_SESSION_CANCELLED;
      service->session.lease_remaining_ms = 0;
      service->session.state_seq++;
      set_error(&service->session, reason, false);
      service->session_active = false;
      gate_token = service->remote_gate_token;
      service->remote_gate_token = 0;
      release = gate_token != 0;
    }

  pthread_mutex_unlock(&service->lock);
  if (release)
    {
      (void)vg_config_gate_release(VG_CONFIG_GATE_REMOTE, gate_token);
    }

  return 0;
}

int vg_config_service_tick(struct vg_config_service *service)
{
  uint32_t gate_token = 0;
  bool release;

  if (service == NULL || !service->initialized)
    {
      return -EINVAL;
    }

  pthread_mutex_lock(&service->lock);
  expire_if_due(service, &release);
  if (release)
    {
      gate_token = service->remote_gate_token;
      service->remote_gate_token = 0;
    }

  pthread_mutex_unlock(&service->lock);
  if (release)
    {
      (void)vg_config_gate_release(VG_CONFIG_GATE_REMOTE, gate_token);
    }

  return 0;
}

int vg_config_service_snapshot(struct vg_config_service *service,
                               struct vg_config_device_snapshot *out)
{
  struct vg_config_table_snapshot table;
  uint64_t now;
  int ret;

  if (service == NULL || out == NULL || !service->initialized)
    {
      return -EINVAL;
    }

  ret = vg_config_service_tick(service);
  if (ret != 0)
    {
      return ret;
    }

  memset(out, 0, sizeof(*out));
  memset(&table, 0, sizeof(table));
  ret = service->ports.load_table(service->ports.ctx, &table);
  if (ret != 0)
    {
      table.state = VG_CONFIG_TABLE_UNAVAILABLE;
    }

  now = service->ports.now_ms(service->ports.ctx);
  pthread_mutex_lock(&service->lock);
  memcpy(out->boot_id, service->boot_id, sizeof(out->boot_id));
  memcpy(out->link_id, service->link_id, sizeof(out->link_id));
  out->next_open_seq = service->open_seq_exhausted ?
                       0 : service->next_open_seq;
  out->remote_config = service->link_id[0] != '\0';
  out->uptime_ms = now >= service->started_ms ?
                   now - service->started_ms : 0;
  pthread_mutex_unlock(&service->lock);

  out->config_state = table.state;
  out->max_points = VG_DISCOVER_MAX_POINTS;
  out->max_request_bytes = VG_CONFIG_REQUEST_MAX;
  out->max_state_bytes = VG_CONFIG_STATE_MAX;
  out->lease_ms = VG_CONFIG_LEASE_MS;
  out->baud = 9600;
  out->time_synced = false;
  out->active_gate = vg_config_gate_current();

  if (table.state != VG_CONFIG_TABLE_VALID)
    {
      return 0;
    }

  out->point_count = table.point_count;
  if (out->point_count > VG_CONFIG_BOOTSTRAP_CAPACITY)
    {
      out->point_count = 0;
      out->config_state = VG_CONFIG_TABLE_UNAVAILABLE;
      return 0;
    }

  memcpy(out->point_ids, table.point_ids, sizeof(out->point_ids));
  out->slave_count = table.slave_count;
  if (out->slave_count > VG_DISCOVER_MAX_SLAVES)
    {
      out->slave_count = 0;
      out->point_count = 0;
      out->config_state = VG_CONFIG_TABLE_UNAVAILABLE;
      return 0;
    }

  memcpy(out->slave_addresses, table.slave_addresses,
         sizeof(out->slave_addresses));
  out->has_base_hash = table.has_base_hash;
  memcpy(out->base_hash, table.base_hash, sizeof(out->base_hash));
  return 0;
}

int vg_config_service_open(struct vg_config_service *service,
                           const struct vg_config_open_request *request,
                           struct vg_config_task_status *out)
{
  struct vg_config_table_snapshot table;
  struct vg_config_cached_request *cached;
  struct vg_config_task_status status;
  char session_id[VG_CONFIG_HEX32_LEN];
  uint32_t gate_token = 0;
  uint64_t now;
  bool created = false;
  int ret;

  if (service == NULL || request == NULL || out == NULL ||
      !service->initialized || !valid_text_id(request->req_id) ||
      !valid_hash(request->request_hash) ||
      !valid_text_id(request->task_id) ||
      !valid_hex32(request->boot_id) || !valid_hex32(request->link_id) ||
      request->expected_open_seq == 0 ||
      (request->has_base_hash && !valid_hash(request->base_hash)))
    {
      return -EINVAL;
    }

  ret = vg_config_service_tick(service);
  if (ret != 0)
    {
      return ret;
    }

  pthread_mutex_lock(&service->lock);
  cached = find_request(service, request->req_id);
  if (cached != NULL)
    {
      if (strcmp(cached->request_hash, request->request_hash) != 0)
        {
          pthread_mutex_unlock(&service->lock);
          return -EEXIST;
        }

      fill_status(service, &cached->status, out);
      pthread_mutex_unlock(&service->lock);
      return cached->result;
    }

  if (strcmp(request->boot_id, service->boot_id) != 0 ||
      strcmp(request->link_id, service->link_id) != 0 ||
      service->open_seq_exhausted ||
      request->expected_open_seq != service->next_open_seq)
    {
      pthread_mutex_unlock(&service->lock);
      return -ESTALE;
    }

  if (service->session_active || task_was_used(service, request->task_id))
    {
      pthread_mutex_unlock(&service->lock);
      return -EBUSY;
    }

  pthread_mutex_unlock(&service->lock);
  ret = vg_config_gate_acquire(VG_CONFIG_GATE_REMOTE, &gate_token, &created);
  if (ret != 0)
    {
      return ret;
    }

  memset(&table, 0, sizeof(table));
  ret = service->ports.load_table(service->ports.ctx, &table);
  if (ret != 0)
    {
      (void)vg_config_gate_release(VG_CONFIG_GATE_REMOTE, gate_token);
      return ret;
    }

  ret = make_identity(service, session_id);
  if (ret != 0)
    {
      (void)vg_config_gate_release(VG_CONFIG_GATE_REMOTE, gate_token);
      return ret;
    }

  pthread_mutex_lock(&service->lock);
  expire_if_due(service, &created);
  if (created)
    {
      (void)vg_config_gate_release(VG_CONFIG_GATE_REMOTE, gate_token);
      pthread_mutex_unlock(&service->lock);
      return -EAGAIN;
    }

  if (strcmp(request->boot_id, service->boot_id) != 0 ||
      strcmp(request->link_id, service->link_id) != 0 ||
      service->open_seq_exhausted ||
      request->expected_open_seq != service->next_open_seq ||
      service->session_active ||
      task_was_used(service, request->task_id))
    {
      (void)vg_config_gate_release(VG_CONFIG_GATE_REMOTE, gate_token);
      pthread_mutex_unlock(&service->lock);
      return -ESTALE;
    }

  if (table.state == VG_CONFIG_TABLE_UNAVAILABLE)
    {
      (void)vg_config_gate_release(VG_CONFIG_GATE_REMOTE, gate_token);
      pthread_mutex_unlock(&service->lock);
      return -EIO;
    }

  if ((table.state == VG_CONFIG_TABLE_VALID) != request->has_base_hash ||
      (request->has_base_hash &&
       strcmp(request->base_hash, table.base_hash) != 0))
    {
      (void)vg_config_gate_release(VG_CONFIG_GATE_REMOTE, gate_token);
      pthread_mutex_unlock(&service->lock);
      return -ESTALE;
    }

  if (!remember_task(service, request->task_id))
    {
      (void)vg_config_gate_release(VG_CONFIG_GATE_REMOTE, gate_token);
      pthread_mutex_unlock(&service->lock);
      return -ENOSPC;
    }

  memset(&status, 0, sizeof(status));
  memcpy(status.task_id, request->task_id, sizeof(status.task_id));
  memcpy(status.session_id, session_id, sizeof(status.session_id));
  status.state = VG_CONFIG_SESSION_OPENED;
  status.revision = 0;
  status.state_seq = 1;
  status.has_base_hash = request->has_base_hash;
  if (request->has_base_hash)
    {
      memcpy(status.base_hash, request->base_hash, sizeof(status.base_hash));
    }

  now = service->ports.now_ms(service->ports.ctx);
  service->session = status;
  service->session_active = true;
  service->lease_deadline_ms = now + VG_CONFIG_LEASE_MS;
  service->remote_gate_token = gate_token;
  if (service->next_open_seq == UINT32_MAX)
    {
      service->open_seq_exhausted = true;
    }
  else
    {
      service->next_open_seq++;
    }

  cache_request(service, request->req_id, request->request_hash, 0,
                &status);
  fill_status(service, &status, out);
  pthread_mutex_unlock(&service->lock);
  memset(session_id, 0, sizeof(session_id));
  return 0;
}

int vg_config_service_query_task(struct vg_config_service *service,
                                 const char *task_id,
                                 struct vg_config_task_status *out)
{
  int ret;

  if (service == NULL || out == NULL || !valid_text_id(task_id) ||
      !service->initialized)
    {
      return -EINVAL;
    }

  ret = vg_config_service_tick(service);
  if (ret != 0)
    {
      return ret;
    }

  pthread_mutex_lock(&service->lock);
  if (service->session.task_id[0] == '\0' ||
      strcmp(service->session.task_id, task_id) != 0)
    {
      pthread_mutex_unlock(&service->lock);
      return -ENOENT;
    }

  fill_status(service, &service->session, out);
  pthread_mutex_unlock(&service->lock);
  return 0;
}

int vg_config_service_cancel(struct vg_config_service *service,
                             const struct vg_config_cancel_request *request,
                             struct vg_config_task_status *out)
{
  struct vg_config_cached_request *cached;
  uint32_t gate_token = 0;
  bool release = false;
  int ret;

  if (service == NULL || request == NULL || out == NULL ||
      !service->initialized || !valid_text_id(request->req_id) ||
      !valid_hash(request->request_hash) ||
      !valid_text_id(request->task_id) ||
      !valid_hex32(request->boot_id) || !valid_hex32(request->link_id) ||
      !valid_hex32(request->session_id))
    {
      return -EINVAL;
    }

  ret = vg_config_service_tick(service);
  if (ret != 0)
    {
      return ret;
    }

  pthread_mutex_lock(&service->lock);
  cached = find_request(service, request->req_id);
  if (cached != NULL)
    {
      if (strcmp(cached->request_hash, request->request_hash) != 0)
        {
          pthread_mutex_unlock(&service->lock);
          return -EEXIST;
        }

      fill_status(service, &cached->status, out);
      pthread_mutex_unlock(&service->lock);
      return cached->result;
    }

  if (!service->session_active ||
      strcmp(request->task_id, service->session.task_id) != 0)
    {
      pthread_mutex_unlock(&service->lock);
      return -ENOENT;
    }

  if (strcmp(request->boot_id, service->boot_id) != 0 ||
      strcmp(request->link_id, service->link_id) != 0 ||
      strcmp(request->session_id, service->session.session_id) != 0)
    {
      pthread_mutex_unlock(&service->lock);
      return -ESTALE;
    }

  if (service->session.state == VG_CONFIG_SESSION_COMMITTING)
    {
      pthread_mutex_unlock(&service->lock);
      return -EALREADY;
    }

  service->session.state = VG_CONFIG_SESSION_CANCELLED;
  service->session.lease_remaining_ms = 0;
  service->session.state_seq++;
  set_error(&service->session, "cancelled", false);
  service->session_active = false;
  gate_token = service->remote_gate_token;
  service->remote_gate_token = 0;
  release = gate_token != 0;
  cache_request(service, request->req_id, request->request_hash, 0,
                &service->session);
  fill_status(service, &service->session, out);
  pthread_mutex_unlock(&service->lock);

  if (release)
    {
      (void)vg_config_gate_release(VG_CONFIG_GATE_REMOTE, gate_token);
    }

  return 0;
}

static const char *candidate_error_code(int ret)
{
  switch (ret)
    {
      case -EEXIST:
        return "candidate_conflict";
      case -E2BIG:
        return "capacity_exceeded";
      case -ENOMEM:
        return "validation_resource";
      default:
        return "invalid_candidate";
    }
}

int vg_config_service_put(struct vg_config_service *service,
                          const struct vg_config_put_request *request,
                          struct vg_config_task_status *out)
{
  struct vg_config_candidate_build build;
  struct vg_config_cached_request *cached;
  struct vg_config_table_snapshot table;
  char calculated_payload_hash[VG_CONFIG_HASH_LEN];
  char confirmation_id[VG_CONFIG_HEX32_LEN];
  uint32_t generation_before;
  int tick_ret;
  int ret;

  if (service == NULL || request == NULL || out == NULL ||
      !service->initialized || !valid_text_id(request->req_id) ||
      !valid_hash(request->request_hash) ||
      !valid_text_id(request->task_id) ||
      !valid_hex32(request->boot_id) || !valid_hex32(request->link_id) ||
      !valid_hex32(request->session_id) ||
      (request->has_base_hash && !valid_hash(request->base_hash)) ||
      !valid_hash(request->payload_hash) ||
      request->candidate_json == NULL ||
      request->candidate_json_len == 0 ||
      request->candidate_json_len > VG_CONFIG_REQUEST_MAX)
    {
      return -EINVAL;
    }

  if (request->revision == 0 ||
      request->expected_revision == UINT32_MAX ||
      request->revision != request->expected_revision + 1)
    {
      return -EINVAL;
    }

  ret = payload_hash(request->candidate_json, request->candidate_json_len,
                     calculated_payload_hash);
  if (ret != 0)
    {
      return ret;
    }

  if (strcmp(calculated_payload_hash, request->payload_hash) != 0)
    {
      memset(calculated_payload_hash, 0, sizeof(calculated_payload_hash));
      return -EINVAL;
    }

  ret = vg_config_service_tick(service);
  if (ret != 0)
    {
      memset(calculated_payload_hash, 0, sizeof(calculated_payload_hash));
      return ret;
    }

  pthread_mutex_lock(&service->lock);
  cached = find_request(service, request->req_id);
  if (cached != NULL)
    {
      if (strcmp(cached->request_hash, request->request_hash) != 0)
        {
          pthread_mutex_unlock(&service->lock);
          return -EEXIST;
        }

      fill_status(service, &cached->status, out);
      ret = cached->result;
      pthread_mutex_unlock(&service->lock);
      return ret;
    }

  if (!service->session_active)
    {
      pthread_mutex_unlock(&service->lock);
      return -ENOENT;
    }

  if (strcmp(request->task_id, service->session.task_id) != 0 ||
      strcmp(request->session_id, service->session.session_id) != 0)
    {
      pthread_mutex_unlock(&service->lock);
      return -ENOENT;
    }

  if (strcmp(request->boot_id, service->boot_id) != 0 ||
      strcmp(request->link_id, service->link_id) != 0)
    {
      pthread_mutex_unlock(&service->lock);
      return -ESTALE;
    }

  if ((service->session.has_base_hash != request->has_base_hash) ||
      (request->has_base_hash &&
       strcmp(service->session.base_hash, request->base_hash) != 0))
    {
      pthread_mutex_unlock(&service->lock);
      return -ESTALE;
    }

  if (service->session.state == VG_CONFIG_SESSION_VALIDATING ||
      service->session.state == VG_CONFIG_SESSION_COMMITTING)
    {
      pthread_mutex_unlock(&service->lock);
      return -EBUSY;
    }

  if (service->session.state != VG_CONFIG_SESSION_OPENED &&
      service->session.state != VG_CONFIG_SESSION_AWAITING_CONFIRM)
    {
      pthread_mutex_unlock(&service->lock);
      return -EALREADY;
    }

  if (request->expected_revision != service->session.revision)
    {
      pthread_mutex_unlock(&service->lock);
      return -EINVAL;
    }

  service->session.state = VG_CONFIG_SESSION_VALIDATING;
  service->session.state_seq++;
  service->session.has_payload_hash = false;
  memset(service->session.payload_hash, 0,
         sizeof(service->session.payload_hash));
  service->session.has_candidate_hash = false;
  memset(service->session.candidate_hash, 0,
         sizeof(service->session.candidate_hash));
  service->session.has_confirmation_id = false;
  memset(service->session.confirmation_id, 0,
         sizeof(service->session.confirmation_id));
  service->session.error_code[0] = '\0';
  service->session.retryable = false;
  memset(&service->pending, 0, sizeof(service->pending));
  generation_before = service->session.state_seq;
  pthread_mutex_unlock(&service->lock);

  memset(&table, 0, sizeof(table));
  ret = service->ports.load_table(service->ports.ctx, &table);
  if (ret == 0 &&
      (table.state == VG_CONFIG_TABLE_UNAVAILABLE ||
       ((table.state == VG_CONFIG_TABLE_VALID) !=
        request->has_base_hash) ||
       (request->has_base_hash &&
        strcmp(table.base_hash, request->base_hash) != 0)))
    {
      ret = -ESTALE;
    }

  memset(&build, 0, sizeof(build));
  if (ret == 0)
    {
      ret = vg_config_candidate_build_json(
        request->candidate_json, request->candidate_json_len,
        &table.current, &build);
    }

  memset(confirmation_id, 0, sizeof(confirmation_id));
  if (ret == 0)
    {
      ret = make_identity(service, confirmation_id);
    }

  tick_ret = vg_config_service_tick(service);
  if (ret == 0 && tick_ret != 0)
    {
      ret = tick_ret;
    }

  pthread_mutex_lock(&service->lock);
  if (!service->session_active ||
      service->session.state != VG_CONFIG_SESSION_VALIDATING ||
      service->session.state_seq != generation_before ||
      strcmp(service->session.task_id, request->task_id) != 0 ||
      strcmp(service->session.session_id, request->session_id) != 0)
    {
      pthread_mutex_unlock(&service->lock);
      ret = -EAGAIN;
      goto done;
    }

  if (ret == 0)
    {
      memset(&service->pending, 0, sizeof(service->pending));
      service->pending.valid = true;
      memcpy(service->pending.task_id, request->task_id,
             sizeof(service->pending.task_id));
      memcpy(service->pending.session_id, request->session_id,
             sizeof(service->pending.session_id));
      service->pending.revision = request->revision;
      service->pending.has_base_hash = request->has_base_hash;
      if (request->has_base_hash)
        {
          memcpy(service->pending.base_hash, request->base_hash,
                 sizeof(service->pending.base_hash));
        }
      memcpy(service->pending.payload_hash, request->payload_hash,
             sizeof(service->pending.payload_hash));
      memcpy(service->pending.candidate_hash, build.candidate_hash,
             sizeof(service->pending.candidate_hash));
      memcpy(service->pending.confirmation_id, confirmation_id,
             sizeof(service->pending.confirmation_id));
      service->pending.candidate_json_len =
        request->candidate_json_len;
      memcpy(service->pending.candidate_json, request->candidate_json,
             request->candidate_json_len);
      service->pending.candidate_json[request->candidate_json_len] = '\0';
      service->pending.target_len = build.target_len;
      memcpy(service->pending.target_json, build.target_json,
             build.target_len + 1);

      service->session.state = VG_CONFIG_SESSION_AWAITING_CONFIRM;
      service->session.revision = request->revision;
      service->session.has_payload_hash = true;
      memcpy(service->session.payload_hash, request->payload_hash,
             sizeof(service->session.payload_hash));
      service->session.has_candidate_hash = true;
      memcpy(service->session.candidate_hash, build.candidate_hash,
             sizeof(service->session.candidate_hash));
      service->session.has_confirmation_id = true;
      memcpy(service->session.confirmation_id, confirmation_id,
             sizeof(service->session.confirmation_id));
      service->session.error_code[0] = '\0';
      service->session.retryable = false;
    }
  else
    {
      memset(&service->pending, 0, sizeof(service->pending));
      service->session.state = VG_CONFIG_SESSION_OPENED;
      service->session.state_seq++;
      set_error(&service->session,
                ret == -ESTALE ? "base_conflict" :
                                 candidate_error_code(ret),
                false);
    }

  service->session.state_seq += ret == 0 ? 1 : 0;
  cache_request(service, request->req_id, request->request_hash, ret,
                &service->session);
  fill_status(service, &service->session, out);
  pthread_mutex_unlock(&service->lock);

 done:
  memset(calculated_payload_hash, 0, sizeof(calculated_payload_hash));
  memset(confirmation_id, 0, sizeof(confirmation_id));
  memset(&build, 0, sizeof(build));
  return ret;
}

int vg_config_service_pending_candidate(
  struct vg_config_service *service,
  struct vg_config_pending_candidate *out)
{
  if (service == NULL || out == NULL || !service->initialized)
    {
      return -EINVAL;
    }

  pthread_mutex_lock(&service->lock);
  if (!service->session_active || !service->pending.valid ||
      service->session.state != VG_CONFIG_SESSION_AWAITING_CONFIRM)
    {
      pthread_mutex_unlock(&service->lock);
      return -ENOENT;
    }

  *out = service->pending;
  pthread_mutex_unlock(&service->lock);
  return 0;
}
