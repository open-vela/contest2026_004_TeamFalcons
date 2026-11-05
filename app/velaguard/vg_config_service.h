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

#include "vg_config_candidate.h"
#include "vg_config_gate.h"
#include "vg_config_contract.h"

enum vg_config_session_state_e
{
  VG_CONFIG_SESSION_NONE = 0,
  VG_CONFIG_SESSION_OPENED,
  VG_CONFIG_SESSION_VALIDATING,
  VG_CONFIG_SESSION_AWAITING_CONFIRM,
  VG_CONFIG_SESSION_COMMITTING,
  VG_CONFIG_SESSION_APPLIED,
  VG_CONFIG_SESSION_CANCELLED,
  VG_CONFIG_SESSION_EXPIRED,
  VG_CONFIG_SESSION_FAILED,
  VG_CONFIG_SESSION_RECOVERY_UNKNOWN
};

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
  struct vg_discover_summary current;
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
  bool recovery_blocked;
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
  int (*commit_table)(void *ctx, const void *target_json,
                      size_t target_len,
                      const struct vg_discover_summary *target_table);
  int (*notify_table)(void *ctx);
  int (*recovery_blocked)(void *ctx, bool *blocked);
};

struct vg_config_open_request
{
  char req_id[VG_CONFIG_ID_MAX];
  char request_hash[VG_CONFIG_HASH_LEN];
  char task_id[VG_CONFIG_ID_MAX];
  char boot_id[VG_CONFIG_HEX32_LEN];
  char link_id[VG_CONFIG_HEX32_LEN];
  uint32_t expected_open_seq;
  bool has_base_hash;
  char base_hash[VG_CONFIG_HASH_LEN];
};

struct vg_config_cancel_request
{
  char req_id[VG_CONFIG_ID_MAX];
  char request_hash[VG_CONFIG_HASH_LEN];
  char task_id[VG_CONFIG_ID_MAX];
  char boot_id[VG_CONFIG_HEX32_LEN];
  char link_id[VG_CONFIG_HEX32_LEN];
  char session_id[VG_CONFIG_HEX32_LEN];
};

struct vg_config_local_cancel_request
{
  char task_id[VG_CONFIG_ID_MAX];
  char session_id[VG_CONFIG_HEX32_LEN];
  uint32_t revision;
  char confirmation_id[VG_CONFIG_HEX32_LEN];
};

struct vg_config_put_request
{
  char req_id[VG_CONFIG_ID_MAX];
  char request_hash[VG_CONFIG_HASH_LEN];
  char task_id[VG_CONFIG_ID_MAX];
  char boot_id[VG_CONFIG_HEX32_LEN];
  char link_id[VG_CONFIG_HEX32_LEN];
  char session_id[VG_CONFIG_HEX32_LEN];
  uint32_t expected_revision;
  uint32_t revision;
  bool has_base_hash;
  char base_hash[VG_CONFIG_HASH_LEN];
  char payload_hash[VG_CONFIG_HASH_LEN];
  const void *candidate_json;
  size_t candidate_json_len;
};

struct vg_config_confirm_request
{
  char task_id[VG_CONFIG_ID_MAX];
  char session_id[VG_CONFIG_HEX32_LEN];
  uint32_t revision;
  bool has_base_hash;
  char base_hash[VG_CONFIG_HASH_LEN];
  char payload_hash[VG_CONFIG_HASH_LEN];
  char candidate_hash[VG_CONFIG_HASH_LEN];
  char confirmation_id[VG_CONFIG_HEX32_LEN];
};

struct vg_config_task_status
{
  char task_id[VG_CONFIG_ID_MAX];
  char session_id[VG_CONFIG_HEX32_LEN];
  enum vg_config_session_state_e state;
  uint32_t revision;
  uint32_t state_seq;
  bool has_base_hash;
  char base_hash[VG_CONFIG_HASH_LEN];
  bool has_payload_hash;
  char payload_hash[VG_CONFIG_HASH_LEN];
  bool has_candidate_hash;
  char candidate_hash[VG_CONFIG_HASH_LEN];
  bool has_confirmation_id;
  char confirmation_id[VG_CONFIG_HEX32_LEN];
  uint64_t lease_remaining_ms;
  char error_code[32];
  bool retryable;
};

struct vg_config_pending_candidate
{
  bool valid;
  char task_id[VG_CONFIG_ID_MAX];
  char session_id[VG_CONFIG_HEX32_LEN];
  uint32_t revision;
  bool has_base_hash;
  char base_hash[VG_CONFIG_HASH_LEN];
  char payload_hash[VG_CONFIG_HASH_LEN];
  char candidate_hash[VG_CONFIG_HASH_LEN];
  char confirmation_id[VG_CONFIG_HEX32_LEN];
  struct vg_config_candidate candidate;
  size_t candidate_json_len;
  char candidate_json[VG_CONFIG_REQUEST_MAX + 1];
  size_t target_len;
  char target_json[VG_POINT_TABLE_JSON_MAX];
  struct vg_discover_summary target_table;
};

/* Small UI-facing copy of the current confirmation. Frozen JSON and the
 * parsed target table stay owned by vg_config_service. */
struct vg_config_pending_preview
{
  bool valid;
  char task_id[VG_CONFIG_ID_MAX];
  char session_id[VG_CONFIG_HEX32_LEN];
  uint32_t revision;
  bool has_base_hash;
  char base_hash[VG_CONFIG_HASH_LEN];
  char payload_hash[VG_CONFIG_HASH_LEN];
  char candidate_hash[VG_CONFIG_HASH_LEN];
  char confirmation_id[VG_CONFIG_HEX32_LEN];
  struct vg_config_candidate candidate;
};

struct vg_config_cached_request
{
  bool used;
  char req_id[VG_CONFIG_ID_MAX];
  char request_hash[VG_CONFIG_HASH_LEN];
  int result;
  struct vg_config_task_status status;
};

struct vg_config_service
{
  pthread_mutex_t lock;
  struct vg_config_service_ports ports;
  char boot_id[VG_CONFIG_HEX32_LEN];
  char link_id[VG_CONFIG_HEX32_LEN];
  uint32_t next_open_seq;
  uint64_t started_ms;
  uint64_t lease_deadline_ms;
  uint32_t remote_gate_token;
  char used_tasks[VG_CONFIG_USED_TASKS][VG_CONFIG_ID_MAX];
  uint32_t used_task_count;
  size_t request_cursor;
  struct vg_config_cached_request requests[VG_CONFIG_REQUEST_CACHE];
  struct vg_config_task_status session;
  struct vg_config_pending_candidate pending;
  bool session_active;
  bool open_seq_exhausted;
  bool initialized;
};

int vg_config_service_init(struct vg_config_service *service,
                           const struct vg_config_service_ports *ports);
int vg_config_service_new_link(struct vg_config_service *service);
int vg_config_service_disconnect(struct vg_config_service *service,
                                 const char *reason);
int vg_config_service_tick(struct vg_config_service *service);
int vg_config_service_snapshot(struct vg_config_service *service,
                               struct vg_config_device_snapshot *out);
int vg_config_service_open(struct vg_config_service *service,
                           const struct vg_config_open_request *request,
                           struct vg_config_task_status *out);
int vg_config_service_query_task(struct vg_config_service *service,
                                 const char *task_id,
                                 struct vg_config_task_status *out);
int vg_config_service_cancel(struct vg_config_service *service,
                             const struct vg_config_cancel_request *request,
                             struct vg_config_task_status *out);
int vg_config_service_cancel_local(
  struct vg_config_service *service,
  const struct vg_config_local_cancel_request *request,
  struct vg_config_task_status *out);
int vg_config_service_put(struct vg_config_service *service,
                          const struct vg_config_put_request *request,
                          struct vg_config_task_status *out);
int vg_config_service_pending_candidate(
  struct vg_config_service *service,
  struct vg_config_pending_candidate *out);
int vg_config_service_pending_preview(
  struct vg_config_service *service,
  struct vg_config_pending_preview *out);
int vg_config_service_confirm(struct vg_config_service *service,
                              const struct vg_config_confirm_request *request,
                              struct vg_config_task_status *out);

#endif
