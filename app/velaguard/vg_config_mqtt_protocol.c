/****************************************************************************
 * app/velaguard/vg_config_mqtt_protocol.c
 ****************************************************************************/

#ifdef __NuttX__
#  include <nuttx/config.h>
#endif

#include "vg_config_mqtt_protocol.h"
#include "vg_ota_json.h"
#include "vg_ota_sha256.h"

#ifdef __NuttX__
#  include <netutils/cJSON.h>
#else
#  include "cJSON.h"
#endif

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define VG_CONFIG_MQTT_FIELDS_MAX 13

static int reject(char *error, size_t size, const char *reason)
{
  if (error != NULL && size != 0)
    {
      snprintf(error, size, "%s", reason);
    }

  return -EINVAL;
}

static bool valid_utf8(const uint8_t *text, size_t length)
{
  size_t i = 0;

  if (text == NULL)
    {
      return false;
    }

  while (i < length)
    {
      uint8_t c = text[i++];
      uint32_t value;
      size_t remaining;

      if (c == 0)
        {
          return false;
        }
      if (c < 0x80)
        {
          continue;
        }
      if ((c & 0xe0) == 0xc0)
        {
          value = c & 0x1f;
          remaining = 1;
          if (value < 2)
            {
              return false;
            }
        }
      else if ((c & 0xf0) == 0xe0)
        {
          value = c & 0x0f;
          remaining = 2;
        }
      else if ((c & 0xf8) == 0xf0)
        {
          value = c & 0x07;
          remaining = 3;
        }
      else
        {
          return false;
        }

      if (i + remaining > length)
        {
          return false;
        }

      while (remaining-- != 0)
        {
          uint8_t next = text[i++];
          if ((next & 0xc0) != 0x80)
            {
              return false;
            }
          value = (value << 6) | (next & 0x3f);
        }

      if ((c & 0xe0) == 0xc0 && value < 0x80)
        {
          return false;
        }
      if ((c & 0xf0) == 0xe0 &&
          (value < 0x800 || (value >= 0xd800 && value <= 0xdfff)))
        {
          return false;
        }
      if ((c & 0xf8) == 0xf0 &&
          (value < 0x10000 || value > 0x10ffff))
        {
          return false;
        }
    }

  return true;
}

static bool valid_id(const char *text)
{
  size_t length;
  size_t i;

  if (text == NULL)
    {
      return false;
    }
  length = strlen(text);
  if (length < 1 || length >= VG_CONFIG_ID_MAX)
    {
      return false;
    }
  for (i = 0; i < length; i++)
    {
      unsigned char c = (unsigned char)text[i];
      if (!(isalnum(c) || c == '_' || c == '-'))
        {
          return false;
        }
    }
  return true;
}

static bool valid_identifier(const char *text, size_t limit)
{
  size_t length;
  size_t i;

  if (text == NULL)
    {
      return false;
    }
  length = strlen(text);
  if (length < 1 || length >= limit)
    {
      return false;
    }
  for (i = 0; i < length; i++)
    {
      unsigned char c = (unsigned char)text[i];
      if (!(isalnum(c) || c == '_' || c == '-' || c == '.' || c == ':'))
        {
          return false;
        }
    }
  return true;
}

static bool valid_hex(const char *text, size_t length)
{
  size_t i;

  if (text == NULL || strlen(text) != length)
    {
      return false;
    }
  for (i = 0; i < length; i++)
    {
      if (!((text[i] >= '0' && text[i] <= '9') ||
            (text[i] >= 'a' && text[i] <= 'f')))
        {
          return false;
        }
    }
  return true;
}

static bool number_u32(const cJSON *node, uint32_t *out)
{
  double value;

  if (!cJSON_IsNumber(node))
    {
      return false;
    }
  value = node->valuedouble;
  if (!isfinite(value) || value < 0.0 || value > UINT32_MAX ||
      floor(value) != value)
    {
      return false;
    }
  *out = (uint32_t)value;
  return true;
}

static int copy_string(const cJSON *node, char *out, size_t capacity,
                       bool allow_empty)
{
  size_t length;

  if (!cJSON_IsString(node) || node->valuestring == NULL)
    {
      return -EINVAL;
    }
  length = strlen(node->valuestring);
  if ((!allow_empty && length == 0) || length >= capacity ||
      !valid_utf8((const uint8_t *)node->valuestring, length))
    {
      return -EINVAL;
    }
  memcpy(out, node->valuestring, length + 1);
  return 0;
}

static int copy_base_hash(const cJSON *node,
                          bool *has_hash,
                          char hash[VG_CONFIG_HASH_LEN])
{
  memset(hash, 0, VG_CONFIG_HASH_LEN);
  *has_hash = false;
  if (cJSON_IsNull(node))
    {
      return 0;
    }
  if (!cJSON_IsString(node) ||
      !valid_hex(node->valuestring, VG_CONFIG_HASH_LEN - 1))
    {
      return -EINVAL;
    }
  memcpy(hash, node->valuestring, VG_CONFIG_HASH_LEN);
  *has_hash = true;
  return 0;
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

static void hash_payload(const void *payload, size_t length,
                         char out[VG_CONFIG_HASH_LEN])
{
  struct vg_ota_sha256 sha;
  uint8_t digest[32];

  vg_ota_sha256_init(&sha);
  vg_ota_sha256_update(&sha, payload, (uint32_t)length);
  vg_ota_sha256_final(&sha, digest);
  digest_to_hex(digest, out);
  memset(digest, 0, sizeof(digest));
  memset(&sha, 0, sizeof(sha));
}

static bool field_name_seen(const cJSON *item,
                            const char *const *names,
                            size_t count,
                            unsigned *seen)
{
  size_t i;

  if (item->string == NULL)
    {
      return false;
    }
  for (i = 0; i < count; i++)
    {
      if (strcmp(item->string, names[i]) == 0)
        {
          if ((*seen & (1u << i)) != 0)
            {
              return false;
            }
          *seen |= 1u << i;
          return true;
        }
    }
  return false;
}

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
  size_t error_size)
{
  static const char *const names[VG_CONFIG_MQTT_FIELDS_MAX] =
  {
    "schema_version", "device_id", "req_id", "op", "scope",
    "task_id", "boot_id", "link_id", "expected_open_seq", "base_hash",
    "session_id", "expected_revision", "revision"
  };
  static const char *const put_names[VG_CONFIG_MQTT_FIELDS_MAX] =
  {
    "schema_version", "device_id", "req_id", "task_id", "op",
    "boot_id", "link_id", "session_id", "expected_revision",
    "revision", "base_hash", "payload_sha256", "candidate_json"
  };
  const cJSON *fields[VG_CONFIG_MQTT_FIELDS_MAX] = { 0 };
  char expected_topic[VG_CONFIG_MQTT_TOPIC_MAX];
  char *copy = NULL;
  cJSON *root = NULL;
  cJSON *item;
  const char *const *selected_names;
  unsigned seen = 0;
  unsigned expected_seen;
  enum vg_config_mqtt_op_e parsed_op;
  const char *op;
  const char *scope = NULL;
  char raw_hash[VG_CONFIG_HASH_LEN];
  int ret = -EINVAL;

  if (error != NULL && error_size != 0)
    {
      error[0] = '\0';
    }
  if (!secure_transport)
    {
      return reject(error, error_size, "secure_transport_required");
    }
  if (retain != 0 || qos != 1)
    {
      return reject(error, error_size, "request_delivery_invalid");
    }
  if (topic == NULL || payload == NULL || request == NULL ||
      !valid_identifier(device_id, VG_DEVICE_ID_MAX))
    {
      return reject(error, error_size, "request_invalid");
    }
  if (length == 0 || length > VG_CONFIG_MQTT_REQUEST_MAX)
    {
      return reject(error, error_size, "request_too_large");
    }
  if (candidate_json == NULL ||
      candidate_capacity < VG_CONFIG_MQTT_REQUEST_MAX + 1)
    {
      return reject(error, error_size, "candidate_buffer_invalid");
    }
  if (vg_ota_json_guard(payload, length) != 0)
    {
      return reject(error, error_size, "json_invalid");
    }

  snprintf(expected_topic, sizeof(expected_topic),
           "vg/%s/config/request", device_id);
  if (strcmp(topic, expected_topic) != 0)
    {
      return reject(error, error_size, "config_topic_forbidden");
    }
  hash_payload(payload, length, raw_hash);

  copy = malloc(length + 1);
  if (copy == NULL)
    {
      return -ENOMEM;
    }
  memcpy(copy, payload, length);
  copy[length] = '\0';
  root = cJSON_ParseWithOpts(copy, NULL, 1);
  if (!cJSON_IsObject(root))
    {
      ret = -EINVAL;
      goto done;
    }

  item = cJSON_GetObjectItemCaseSensitive(root, "schema_version");
  if (!cJSON_IsNumber(item) || item->valuedouble != 1.0)
    {
      goto done;
    }
  item = cJSON_GetObjectItemCaseSensitive(root, "op");
  if (!cJSON_IsString(item))
    {
      goto done;
    }
  op = item->valuestring;
  item = cJSON_GetObjectItemCaseSensitive(root, "scope");
  if (cJSON_IsString(item))
    {
      scope = item->valuestring;
    }

  if (strcmp(op, "query") == 0 && scope != NULL &&
      strcmp(scope, "device") == 0)
    {
      parsed_op = VG_CONFIG_MQTT_QUERY_DEVICE;
      selected_names = names;
      expected_seen = (1u << 0) | (1u << 1) | (1u << 2) |
                      (1u << 3) | (1u << 4);
    }
  else if (strcmp(op, "query") == 0 && scope != NULL &&
           strcmp(scope, "task") == 0)
    {
      parsed_op = VG_CONFIG_MQTT_QUERY_TASK;
      selected_names = names;
      expected_seen = (1u << 0) | (1u << 1) | (1u << 2) |
                      (1u << 3) | (1u << 4) | (1u << 5);
    }
  else if (strcmp(op, "open") == 0)
    {
      parsed_op = VG_CONFIG_MQTT_OPEN;
      selected_names = names;
      expected_seen = (1u << 0) | (1u << 1) | (1u << 2) |
                      (1u << 3) | (1u << 5) | (1u << 6) |
                      (1u << 7) | (1u << 8) | (1u << 9);
    }
  else if (strcmp(op, "put") == 0)
    {
      parsed_op = VG_CONFIG_MQTT_PUT;
      selected_names = put_names;
      expected_seen = (1u << VG_CONFIG_MQTT_FIELDS_MAX) - 1u;
    }
  else if (strcmp(op, "cancel") == 0)
    {
      parsed_op = VG_CONFIG_MQTT_CANCEL;
      selected_names = names;
      expected_seen = (1u << 0) | (1u << 1) | (1u << 2) |
                      (1u << 3) | (1u << 5) | (1u << 6) |
                      (1u << 7) | (1u << 10);
    }
  else
    {
      ret = reject(error, error_size, "operation_forbidden");
      goto done;
    }

  cJSON_ArrayForEach(item, root)
    {
      size_t i;
      if (!field_name_seen(item, selected_names,
                           VG_CONFIG_MQTT_FIELDS_MAX, &seen))
        {
          goto done;
        }
      for (i = 0; i < VG_CONFIG_MQTT_FIELDS_MAX; i++)
        {
          if (strcmp(item->string, selected_names[i]) == 0)
            {
              fields[i] = item;
              break;
            }
        }
    }
  if (seen != expected_seen)
    {
      goto done;
    }

  memset(request, 0, sizeof(*request));
  request->op = parsed_op;
  if (copy_string(fields[1], request->device_id,
                  sizeof(request->device_id), false) != 0 ||
      strcmp(request->device_id, device_id) != 0 ||
      copy_string(fields[2], request->req_id,
                  sizeof(request->req_id), false) != 0 ||
      !valid_id(request->req_id))
    {
      goto done;
    }

  if (request->op == VG_CONFIG_MQTT_QUERY_DEVICE)
    {
      memcpy(request->request_hash, raw_hash, sizeof(request->request_hash));
      ret = 0;
      goto done;
    }

  if (copy_string(fields[request->op == VG_CONFIG_MQTT_PUT ? 3 : 5],
                  request->task_id, sizeof(request->task_id), false) != 0 ||
      !valid_id(request->task_id))
    {
      goto done;
    }
  if (request->op == VG_CONFIG_MQTT_QUERY_TASK)
    {
      memcpy(request->request_hash, raw_hash, sizeof(request->request_hash));
      ret = 0;
      goto done;
    }

  if (request->op == VG_CONFIG_MQTT_PUT)
    {
      if (copy_string(fields[5], request->boot_id,
                      sizeof(request->boot_id), false) != 0 ||
          copy_string(fields[6], request->link_id,
                      sizeof(request->link_id), false) != 0 ||
          copy_string(fields[7], request->session_id,
                      sizeof(request->session_id), false) != 0 ||
          !number_u32(fields[8], &request->expected_revision) ||
          !number_u32(fields[9], &request->revision) ||
          copy_base_hash(fields[10], &request->has_base_hash,
                         request->base_hash) != 0 ||
          copy_string(fields[11], request->payload_hash,
                      sizeof(request->payload_hash), false) != 0 ||
          copy_string(fields[12], candidate_json,
                      candidate_capacity, false) != 0)
        {
          goto done;
        }
      request->candidate_json = candidate_json;
      request->candidate_json_len = strlen(candidate_json);
      if (request->candidate_json_len == 0 ||
          request->candidate_json_len > VG_CONFIG_REQUEST_MAX ||
          !valid_hex(request->payload_hash, VG_CONFIG_HASH_LEN - 1))
        {
          goto done;
        }
    }
  else
    {
      if (copy_string(fields[6], request->boot_id,
                      sizeof(request->boot_id), false) != 0 ||
          copy_string(fields[7], request->link_id,
                      sizeof(request->link_id), false) != 0)
        {
          goto done;
        }

      if (request->op == VG_CONFIG_MQTT_OPEN)
        {
          if (!number_u32(fields[8], &request->expected_open_seq) ||
              copy_base_hash(fields[9], &request->has_base_hash,
                             request->base_hash) != 0)
            {
              goto done;
            }
        }
      else
        {
          if (copy_string(fields[10], request->session_id,
                          sizeof(request->session_id), false) != 0)
            {
              goto done;
            }
        }
    }

  if (!valid_hex(request->boot_id, VG_CONFIG_HEX32_LEN - 1) ||
      !valid_hex(request->link_id, VG_CONFIG_HEX32_LEN - 1) ||
      (request->session_id[0] != '\0' &&
       !valid_hex(request->session_id, VG_CONFIG_HEX32_LEN - 1)))
    {
      goto done;
    }

  memcpy(request->request_hash, raw_hash, sizeof(request->request_hash));
  ret = 0;

done:
  cJSON_Delete(root);
  if (copy != NULL)
    {
      memset(copy, 0, length);
      free(copy);
    }
  if (ret != 0)
    {
      memset(request, 0, sizeof(*request));
      if (error != NULL && error_size != 0 && error[0] == '\0')
        {
          snprintf(error, error_size, "request_invalid");
        }
    }
  return ret;
}

const char *vg_config_mqtt_state_name(enum vg_config_session_state_e state)
{
  switch (state)
    {
      case VG_CONFIG_SESSION_OPENED:
        return "opened";
      case VG_CONFIG_SESSION_VALIDATING:
        return "validating";
      case VG_CONFIG_SESSION_AWAITING_CONFIRM:
        return "awaiting_local_confirm";
      case VG_CONFIG_SESSION_COMMITTING:
        return "committing";
      case VG_CONFIG_SESSION_APPLIED:
        return "applied";
      case VG_CONFIG_SESSION_CANCELLED:
        return "cancelled";
      case VG_CONFIG_SESSION_EXPIRED:
        return "expired";
      case VG_CONFIG_SESSION_FAILED:
        return "failed";
      case VG_CONFIG_SESSION_RECOVERY_UNKNOWN:
        return "recovery_unknown";
      case VG_CONFIG_SESSION_NONE:
      default:
        return "none";
    }
}

static bool add_optional_string(cJSON *root, const char *name,
                                const char *value)
{
  return value != NULL && value[0] != '\0' ?
         cJSON_AddStringToObject(root, name, value) != NULL :
         cJSON_AddNullToObject(root, name) != NULL;
}

static bool print_json(cJSON *root, char *out, size_t capacity)
{
  return root != NULL && out != NULL && capacity <= INT_MAX &&
         cJSON_PrintPreallocated(root, out, (int)capacity, 0);
}

int vg_config_mqtt_device_snapshot_json(
  char *out,
  size_t capacity,
  const char *device_id,
  const char *req_id,
  const struct vg_config_device_snapshot *snapshot)
{
  static const char *const states[] =
  {
    "unavailable", "empty", "valid"
  };
  cJSON *root;
  cJSON *ids;
  cJSON *slaves;
  cJSON *caps;
  cJSON *fcs;
  cJSON *types;
  cJSON *quantities;
  uint32_t i;
  int ret = -EINVAL;

  if (out == NULL || capacity == 0 || snapshot == NULL ||
      !valid_identifier(device_id, VG_DEVICE_ID_MAX) ||
      !valid_id(req_id) || snapshot->config_state >
      VG_CONFIG_TABLE_VALID || snapshot->point_count >
      VG_CONFIG_BOOTSTRAP_CAPACITY ||
      snapshot->slave_count > VG_DISCOVER_MAX_SLAVES)
    {
      return -EINVAL;
    }

  root = cJSON_CreateObject();
  ids = cJSON_AddArrayToObject(root, "point_ids");
  slaves = cJSON_AddArrayToObject(root, "slave_addresses");
  caps = cJSON_AddObjectToObject(root, "capabilities");
  fcs = caps != NULL ? cJSON_AddArrayToObject(caps, "function_codes") : NULL;
  types = caps != NULL ? cJSON_AddArrayToObject(caps, "data_types") : NULL;
  quantities = caps != NULL ?
               cJSON_AddArrayToObject(caps, "quantities") : NULL;
  if (root == NULL || ids == NULL || slaves == NULL || caps == NULL ||
      fcs == NULL || types == NULL || quantities == NULL ||
      !cJSON_AddNumberToObject(root, "schema_version", 1) ||
      !cJSON_AddStringToObject(root, "device_id", device_id) ||
      !cJSON_AddStringToObject(root, "kind", "device_snapshot") ||
      !cJSON_AddStringToObject(root, "req_id", req_id) ||
      !cJSON_AddStringToObject(root, "boot_id", snapshot->boot_id) ||
      !cJSON_AddStringToObject(root, "link_id", snapshot->link_id) ||
      !cJSON_AddNumberToObject(root, "next_open_seq",
                               snapshot->next_open_seq) ||
      !cJSON_AddStringToObject(root, "config_state",
                               states[snapshot->config_state]) ||
      !add_optional_string(root, "base_hash",
                           snapshot->has_base_hash ?
                           snapshot->base_hash : NULL) ||
      !cJSON_AddNumberToObject(root, "point_count",
                               snapshot->point_count))
    {
      goto done;
    }

  for (i = 0; i < snapshot->point_count; i++)
    {
      if (!cJSON_AddItemToArray(
            ids, cJSON_CreateString(snapshot->point_ids[i])))
        {
          goto done;
        }
    }
  for (i = 0; i < snapshot->slave_count; i++)
    {
      if (!cJSON_AddItemToArray(
            slaves, cJSON_CreateNumber(snapshot->slave_addresses[i])))
        {
          goto done;
        }
    }

  if (!add_optional_string(root, "active_task_id",
                           snapshot->has_active_task_id ?
                           snapshot->active_task_id : NULL) ||
      !cJSON_AddBoolToObject(caps, "remote_config",
                             snapshot->remote_config) ||
      !cJSON_AddNumberToObject(caps, "protocol_version", 1) ||
      !cJSON_AddNumberToObject(caps, "candidate_schema_version", 1) ||
      !cJSON_AddItemToArray(fcs, cJSON_CreateNumber(3)) ||
      !cJSON_AddItemToArray(fcs, cJSON_CreateNumber(4)) ||
      !cJSON_AddItemToArray(types, cJSON_CreateString("int16")) ||
      !cJSON_AddItemToArray(types, cJSON_CreateString("uint16")) ||
      !cJSON_AddItemToArray(quantities, cJSON_CreateNumber(1)) ||
      !cJSON_AddNumberToObject(caps, "baud", snapshot->baud) ||
      !cJSON_AddNumberToObject(caps, "max_points",
                               snapshot->max_points) ||
      !cJSON_AddNumberToObject(caps, "max_request_bytes",
                               snapshot->max_request_bytes) ||
      !cJSON_AddNumberToObject(caps, "max_state_bytes",
                               snapshot->max_state_bytes) ||
      !cJSON_AddNumberToObject(caps, "lease_ms", snapshot->lease_ms) ||
      !cJSON_AddNumberToObject(root, "uptime_ms",
                               (double)snapshot->uptime_ms) ||
      !cJSON_AddStringToObject(root, "time_quality",
                               snapshot->time_synced ? "synced" :
                               "unsynced") ||
      !print_json(root, out, capacity))
    {
      goto done;
    }
  ret = 0;

done:
  cJSON_Delete(root);
  if (ret != 0)
    {
      out[0] = '\0';
    }
  return ret;
}

int vg_config_mqtt_state_json(
  char *out,
  size_t capacity,
  const char *device_id,
  const char *req_id,
  const char *boot_id,
  const char *link_id,
  const struct vg_config_task_status *status,
  uint64_t uptime_ms,
  const char *time_quality)
{
  cJSON *root;
  cJSON *error = NULL;
  int ret = -EINVAL;

  if (out == NULL || capacity == 0 || status == NULL ||
      !valid_identifier(device_id, VG_DEVICE_ID_MAX) ||
      !valid_hex(boot_id, VG_CONFIG_HEX32_LEN - 1) ||
      !valid_hex(link_id, VG_CONFIG_HEX32_LEN - 1))
    {
      return -EINVAL;
    }

  root = cJSON_CreateObject();
  if (root == NULL)
    {
      return -ENOMEM;
    }
  if (!cJSON_AddNumberToObject(root, "schema_version", 1) ||
      !cJSON_AddStringToObject(root, "device_id", device_id) ||
      !cJSON_AddStringToObject(root, "kind", "state") ||
      !add_optional_string(root, "req_id", req_id) ||
      !cJSON_AddStringToObject(root, "boot_id", boot_id) ||
      !cJSON_AddStringToObject(root, "link_id", link_id) ||
      !cJSON_AddStringToObject(root, "task_id", status->task_id) ||
      !cJSON_AddStringToObject(root, "session_id", status->session_id) ||
      !cJSON_AddNumberToObject(root, "state_seq", status->state_seq) ||
      !cJSON_AddStringToObject(root, "state",
                               vg_config_mqtt_state_name(status->state)) ||
      !cJSON_AddNumberToObject(root, "revision", status->revision) ||
      !cJSON_AddNullToObject(root, "pending_revision") ||
      !add_optional_string(root, "base_hash",
                           status->has_base_hash ?
                           status->base_hash : NULL) ||
      !add_optional_string(root, "payload_sha256",
                           status->has_payload_hash ?
                           status->payload_hash : NULL) ||
      !add_optional_string(root, "candidate_hash",
                           status->has_candidate_hash ?
                           status->candidate_hash : NULL) ||
      !add_optional_string(root, "confirmation_id",
                           status->has_confirmation_id ?
                           status->confirmation_id : NULL) ||
      !cJSON_AddNumberToObject(
        root, "lease_remaining_ms",
        (double)status->lease_remaining_ms))
    {
      goto done;
    }

  if (status->error_code[0] != '\0')
    {
      error = cJSON_AddObjectToObject(root, "error");
      if (error == NULL ||
          !cJSON_AddStringToObject(error, "code", status->error_code) ||
          !cJSON_AddBoolToObject(error, "retryable", status->retryable) ||
          !cJSON_AddArrayToObject(error, "field_errors"))
        {
          goto done;
        }
    }
  else if (!cJSON_AddNullToObject(root, "error"))
    {
      goto done;
    }

  if (!cJSON_AddNumberToObject(root, "uptime_ms", (double)uptime_ms) ||
      !cJSON_AddStringToObject(root, "time_quality",
                               time_quality != NULL ? time_quality :
                               "unsynced") ||
      !print_json(root, out, capacity))
    {
      goto done;
    }
  ret = 0;

done:
  cJSON_Delete(root);
  if (ret != 0)
    {
      out[0] = '\0';
    }
  return ret;
}

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
  const char *reason)
{
  cJSON *root;
  cJSON *error;
  cJSON *fields;
  cJSON *first;
  int ret = -EINVAL;

  if (out == NULL || capacity == 0 ||
      !valid_identifier(device_id, VG_DEVICE_ID_MAX) ||
      !valid_id(req_id) || code == NULL || !valid_id(code))
    {
      return -EINVAL;
    }

  root = cJSON_CreateObject();
  error = root != NULL ? cJSON_AddObjectToObject(root, "error") : NULL;
  fields = error != NULL ?
           cJSON_AddArrayToObject(error, "field_errors") : NULL;
  first = fields != NULL ? cJSON_CreateObject() : NULL;
  if (root == NULL || error == NULL || fields == NULL || first == NULL ||
      !cJSON_AddNumberToObject(root, "schema_version", 1) ||
      !cJSON_AddStringToObject(root, "device_id", device_id) ||
      !cJSON_AddStringToObject(root, "kind", "request_error") ||
      !cJSON_AddStringToObject(root, "req_id", req_id) ||
      !add_optional_string(root, "boot_id", boot_id) ||
      !add_optional_string(root, "link_id", link_id) ||
      !add_optional_string(root, "task_id", task_id) ||
      !add_optional_string(root, "session_id", session_id) ||
      !cJSON_AddStringToObject(error, "code", code) ||
      !cJSON_AddBoolToObject(error, "retryable", retryable) ||
      !cJSON_AddStringToObject(first, "path",
                               path != NULL ? path : "") ||
      !cJSON_AddStringToObject(first, "reason",
                               reason != NULL ? reason :
                               "request rejected"))
    {
      cJSON_Delete(first);
      goto done;
    }
  if (!cJSON_AddItemToArray(fields, first))
    {
      cJSON_Delete(first);
      goto done;
    }
  first = NULL;
  if (!print_json(root, out, capacity))
    {
      goto done;
    }
  ret = 0;

done:
  if (first != NULL)
    {
      cJSON_Delete(first);
    }
  cJSON_Delete(root);
  if (ret != 0)
    {
      out[0] = '\0';
    }
  return ret;
}
