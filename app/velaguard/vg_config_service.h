/****************************************************************************
 * app/velaguard/vg_config_service.h
 *
 * Host-testable state and reporting for remote point-table configuration.
 ****************************************************************************/

#ifndef __APP_VELAGUARD_VG_CONFIG_SERVICE_H
#define __APP_VELAGUARD_VG_CONFIG_SERVICE_H

#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "vg_config_gate.h"
#include "vg_discover.h"

#define VG_CONFIG_HEX32_LEN 33
#define VG_CONFIG_HASH_LEN  65
#define VG_CONFIG_BOOTSTRAP_CAPACITY VG_DISCOVER_MAX_POINTS
#define VG_CONFIG_REQUEST_MAX 16384
#define VG_CONFIG_STATE_MAX 4096
#define VG_CONFIG_LEASE_MS 600000u

enum vg_config_table_state_e
{
  VG_CONFIG_TABLE_UNAVAILABLE = 0,
  VG_CONFIG_TABLE_EMPTY,
  VG_CONFIG_TABLE_VALID
};

struct vg_config_table_snapshot
{
  enum vg_config_table_state_e state;
  uint32_t point_count;
  char point_ids[VG_CONFIG_BOOTSTRAP_CAPACITY][VG_POINT_ID_MAX];
  uint8_t slave_addresses[VG_DISCOVER_MAX_SLAVES];
  uint32_t slave_count;
  bool has_base_hash;
  char base_hash[VG_CONFIG_HASH_LEN];
};

struct vg_config_device_snapshot
{
  char boot_id[VG_CONFIG_HEX32_LEN];
  char link_id[VG_CONFIG_HEX32_LEN];
  uint32_t next_open_seq;
  enum vg_config_table_state_e config_state;
  bool has_base_hash;
  char base_hash[VG_CONFIG_HASH_LEN];
  uint32_t point_count;
  char point_ids[VG_CONFIG_BOOTSTRAP_CAPACITY][VG_POINT_ID_MAX];
  uint8_t slave_addresses[VG_DISCOVER_MAX_SLAVES];
  uint32_t slave_count;
  uint32_t max_points;
  uint32_t max_request_bytes;
  uint32_t max_state_bytes;
  uint32_t lease_ms;
  uint16_t baud;
  bool remote_config;
  enum vg_config_gate_owner_e active_gate;
  uint64_t uptime_ms;
  bool time_synced;
};

struct vg_config_service_ports
{
  int (*load_table)(void *ctx, struct vg_config_table_snapshot *out);
  int (*random_bytes)(void *ctx, uint8_t *out, size_t length);
  uint64_t (*now_ms)(void *ctx);
  void *ctx;
};

struct vg_config_service
{
  pthread_mutex_t lock;
  struct vg_config_service_ports ports;
  char boot_id[VG_CONFIG_HEX32_LEN];
  char link_id[VG_CONFIG_HEX32_LEN];
  uint32_t next_open_seq;
  uint64_t started_ms;
  bool initialized;
};

int vg_config_service_init(struct vg_config_service *service,
                           const struct vg_config_service_ports *ports);
int vg_config_service_new_link(struct vg_config_service *service);
int vg_config_service_snapshot(struct vg_config_service *service,
                               struct vg_config_device_snapshot *out);

#endif
