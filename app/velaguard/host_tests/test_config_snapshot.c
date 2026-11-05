#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <string.h>

#include "../vg_config_service.h"

struct fixture
{
  struct vg_config_table_snapshot table;
  uint64_t now;
  uint8_t random_value;
  int load_result;
  int random_result;
};

static int load_table(void *ctx, struct vg_config_table_snapshot *out)
{
  struct fixture *fixture = ctx;

  if (fixture->load_result != 0)
    {
      out->state = VG_CONFIG_TABLE_UNAVAILABLE;
      return fixture->load_result;
    }

  *out = fixture->table;
  return 0;
}

static int random_bytes(void *ctx, uint8_t *out, size_t length)
{
  struct fixture *fixture = ctx;
  size_t i;

  if (fixture->random_result != 0)
    {
      return fixture->random_result;
    }

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

int main(void)
{
  struct fixture fixture;
  struct vg_config_service service;
  struct vg_config_service_ports ports;
  struct vg_config_device_snapshot snapshot;
  uint32_t gate_token = 0;
  bool created = false;

  memset(&fixture, 0, sizeof(fixture));
  fixture.now = 1000;
  fixture.table.state = VG_CONFIG_TABLE_VALID;
  fixture.table.point_count = 2;
  strcpy(fixture.table.point_ids[0], "temp");
  strcpy(fixture.table.point_ids[1], "humidity");
  fixture.table.slave_addresses[0] = 1;
  fixture.table.slave_addresses[1] = 2;
  fixture.table.slave_count = 2;
  fixture.table.has_base_hash = true;
  memset(fixture.table.base_hash, 'a', 64);
  fixture.table.base_hash[64] = '\0';

  ports.load_table = load_table;
  ports.random_bytes = random_bytes;
  ports.now_ms = now_ms;
  ports.ctx = &fixture;
  assert(vg_config_service_init(&service, &ports) == 0);
  assert(strcmp(service.boot_id, "000102030405060708090a0b0c0d0e0f") == 0);
  assert(service.link_id[0] == '\0');
  assert(vg_config_service_new_link(&service) == 0);
  assert(strcmp(service.link_id, "101112131415161718191a1b1c1d1e1f") == 0);

  fixture.now = 1255;
  assert(vg_config_service_snapshot(&service, &snapshot) == 0);
  assert(snapshot.config_state == VG_CONFIG_TABLE_VALID);
  assert(snapshot.point_count == 2);
  assert(strcmp(snapshot.point_ids[0], "temp") == 0);
  assert(strcmp(snapshot.point_ids[1], "humidity") == 0);
  assert(snapshot.slave_count == 2);
  assert(snapshot.slave_addresses[0] == 1);
  assert(snapshot.slave_addresses[1] == 2);
  assert(snapshot.has_base_hash);
  assert(snapshot.base_hash[0] == 'a');
  assert(snapshot.next_open_seq == 1);
  assert(snapshot.max_points == VG_DISCOVER_MAX_POINTS);
  assert(snapshot.max_request_bytes == VG_CONFIG_REQUEST_MAX);
  assert(snapshot.max_state_bytes == VG_CONFIG_STATE_MAX);
  assert(snapshot.lease_ms == VG_CONFIG_LEASE_MS);
  assert(snapshot.baud == 9600);
  assert(snapshot.remote_config);
  assert(snapshot.uptime_ms == 255);
  assert(!snapshot.time_synced);
  assert(snapshot.active_gate == VG_CONFIG_GATE_NONE);

  assert(vg_config_gate_acquire(VG_CONFIG_GATE_SCAN, &gate_token,
                                &created) == 0);
  assert(vg_config_service_snapshot(&service, &snapshot) == 0);
  assert(snapshot.active_gate == VG_CONFIG_GATE_SCAN);
  assert(vg_config_gate_release(VG_CONFIG_GATE_SCAN, gate_token) == 0);

  fixture.table.state = VG_CONFIG_TABLE_EMPTY;
  fixture.table.point_count = 99;
  fixture.table.has_base_hash = true;
  assert(vg_config_service_snapshot(&service, &snapshot) == 0);
  assert(snapshot.config_state == VG_CONFIG_TABLE_EMPTY);
  assert(snapshot.point_count == 0);
  assert(!snapshot.has_base_hash);

  fixture.table.state = VG_CONFIG_TABLE_VALID;
  fixture.table.point_count = VG_DISCOVER_MAX_POINTS + 1;
  assert(vg_config_service_snapshot(&service, &snapshot) == 0);
  assert(snapshot.config_state == VG_CONFIG_TABLE_UNAVAILABLE);
  assert(snapshot.point_count == 0);

  fixture.load_result = -EIO;
  fixture.table.state = VG_CONFIG_TABLE_VALID;
  fixture.table.point_count = 1;
  assert(vg_config_service_snapshot(&service, &snapshot) == 0);
  assert(snapshot.config_state == VG_CONFIG_TABLE_UNAVAILABLE);
  assert(snapshot.point_count == 0);

  fixture.load_result = 0;
  fixture.random_result = -EIO;
  assert(vg_config_service_init(&service, &ports) == -EIO);
  assert(vg_config_service_init(NULL, &ports) == -EINVAL);
  return 0;
}
