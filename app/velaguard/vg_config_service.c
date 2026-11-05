/****************************************************************************
 * app/velaguard/vg_config_service.c
 ****************************************************************************/

#include <errno.h>
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
  out->next_open_seq = service->next_open_seq;
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
