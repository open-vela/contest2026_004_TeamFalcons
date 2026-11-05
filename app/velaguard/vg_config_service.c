/****************************************************************************
 * app/velaguard/vg_config_service.c
 ****************************************************************************/

#include <errno.h>
#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "vg_config_service.h"

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
                          const struct vg_config_task_status *status)
{
  struct vg_config_cached_request *entry =
    &service->requests[service->request_cursor];

  memset(entry, 0, sizeof(*entry));
  entry->used = true;
  memcpy(entry->req_id, req_id, sizeof(entry->req_id));
  memcpy(entry->request_hash, request_hash, sizeof(entry->request_hash));
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
      return 0;
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

  cache_request(service, request->req_id, request->request_hash, &status);
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
      return 0;
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
  cache_request(service, request->req_id, request->request_hash,
                &service->session);
  fill_status(service, &service->session, out);
  pthread_mutex_unlock(&service->lock);

  if (release)
    {
      (void)vg_config_gate_release(VG_CONFIG_GATE_REMOTE, gate_token);
    }

  return 0;
}
