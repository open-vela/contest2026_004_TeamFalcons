/****************************************************************************
 * app/velaguard/vg_config_mqtt_protocol.h
 *
 * Strict request parsing and bounded response encoding for the dedicated
 * candidate MQTT topic pair.
 ****************************************************************************/

#ifndef __APP_VELAGUARD_VG_CONFIG_MQTT_PROTOCOL_H
#define __APP_VELAGUARD_VG_CONFIG_MQTT_PROTOCOL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "vg_config_service.h"
#include "vg_device_id.h"

#define VG_CONFIG_MQTT_TOPIC_MAX 96
#define VG_CONFIG_MQTT_REQUEST_MAX VG_CONFIG_REQUEST_MAX

enum vg_config_mqtt_op_e
{
  VG_CONFIG_MQTT_QUERY_DEVICE = 0,
  VG_CONFIG_MQTT_QUERY_TASK,
  VG_CONFIG_MQTT_OPEN,
  VG_CONFIG_MQTT_PUT,
  VG_CONFIG_MQTT_CANCEL
};

struct vg_config_mqtt_request
{
  enum vg_config_mqtt_op_e op;
  char req_id[VG_CONFIG_ID_MAX];
  char request_hash[VG_CONFIG_HASH_LEN];
  char device_id[VG_DEVICE_ID_MAX];
  char task_id[VG_CONFIG_ID_MAX];
  char boot_id[VG_CONFIG_HEX32_LEN];
  char link_id[VG_CONFIG_HEX32_LEN];
  char session_id[VG_CONFIG_HEX32_LEN];
  uint32_t expected_open_seq;
  uint32_t expected_revision;
  uint32_t revision;
  bool has_base_hash;
  char base_hash[VG_CONFIG_HASH_LEN];
  char payload_hash[VG_CONFIG_HASH_LEN];
  size_t candidate_json_len;
  char *candidate_json;
};

int vg_config_mqtt_request_parse(
  const char *topic,
  const void *payload,
  size_t length,
  const char *device_id,
  bool secure_transport,
  int retain,
  int qos,
  struct vg_config_mqtt_request *request,
  char *candidate_json,
  size_t candidate_capacity,
  char *error,
  size_t error_size);

int vg_config_mqtt_device_snapshot_json(
  char *out,
  size_t capacity,
  const char *device_id,
  const char *req_id,
  const struct vg_config_device_snapshot *snapshot);

int vg_config_mqtt_state_json(
  char *out,
  size_t capacity,
  const char *device_id,
  const char *req_id,
  const char *boot_id,
  const char *link_id,
  const struct vg_config_task_status *status,
  uint64_t uptime_ms,
  const char *time_quality);

int vg_config_mqtt_request_error_json(
  char *out,
  size_t capacity,
  const char *device_id,
  const char *req_id,
  const char *boot_id,
  const char *link_id,
  const char *task_id,
  const char *session_id,
  const char *code,
  bool retryable,
  const char *path,
  const char *reason);

const char *vg_config_mqtt_state_name(enum vg_config_session_state_e state);

#endif
