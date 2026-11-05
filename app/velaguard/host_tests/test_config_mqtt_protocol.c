#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "cJSON.h"

#include "../vg_config_mqtt_protocol.h"

static const char *topic = "vg/vg-test/config/request";
static char candidate_buffer[VG_CONFIG_MQTT_REQUEST_MAX + 1];

static void fill_hex(char *out, size_t length, char value)
{
  memset(out, value, length - 1);
  out[length - 1] = '\0';
}

static void expect_valid(const char *json,
                         enum vg_config_mqtt_op_e op)
{
  struct vg_config_mqtt_request request;
  char error[64];

  assert(vg_config_mqtt_request_parse(
           topic, json, strlen(json), "vg-test", true, 0, 1,
           &request, candidate_buffer, sizeof(candidate_buffer),
           error, sizeof(error)) == 0);
  assert(request.op == op);
  assert(strlen(request.request_hash) == 64);
}

static void test_requests(void)
{
  const char *device =
    "{\"schema_version\":1,\"device_id\":\"vg-test\","
    "\"req_id\":\"req-device-1\",\"op\":\"query\",\"scope\":\"device\"}";
  const char *task_query =
    "{\"schema_version\":1,\"device_id\":\"vg-test\","
    "\"req_id\":\"req-query-1\",\"task_id\":\"task-1\","
    "\"op\":\"query\",\"scope\":\"task\"}";
  const char *open =
    "{\"schema_version\":1,\"device_id\":\"vg-test\","
    "\"req_id\":\"req-open-1\",\"task_id\":\"task-1\","
    "\"op\":\"open\","
    "\"boot_id\":\"11111111111111111111111111111111\","
    "\"link_id\":\"22222222222222222222222222222222\","
    "\"expected_open_seq\":1,\"base_hash\":null}";
  const char *put =
    "{\"schema_version\":1,\"device_id\":\"vg-test\","
    "\"req_id\":\"req-put-1\",\"task_id\":\"task-1\",\"op\":\"put\","
    "\"boot_id\":\"11111111111111111111111111111111\","
    "\"link_id\":\"22222222222222222222222222222222\","
    "\"session_id\":\"33333333333333333333333333333333\","
    "\"expected_revision\":0,\"revision\":1,\"base_hash\":null,"
    "\"payload_sha256\":"
    "\"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\","
    "\"candidate_json\":"
    "\"{\\\"candidate_schema_version\\\":1,"
    "\\\"mode\\\":\\\"add_sensor\\\"}\"}";
  const char *cancel =
    "{\"schema_version\":1,\"device_id\":\"vg-test\","
    "\"req_id\":\"req-cancel-1\",\"task_id\":\"task-1\","
    "\"op\":\"cancel\","
    "\"boot_id\":\"11111111111111111111111111111111\","
    "\"link_id\":\"22222222222222222222222222222222\","
    "\"session_id\":\"33333333333333333333333333333333\"}";
  struct vg_config_mqtt_request parsed;
  char error[64];

  expect_valid(device, VG_CONFIG_MQTT_QUERY_DEVICE);
  expect_valid(task_query, VG_CONFIG_MQTT_QUERY_TASK);
  expect_valid(open, VG_CONFIG_MQTT_OPEN);
  expect_valid(put, VG_CONFIG_MQTT_PUT);
  expect_valid(cancel, VG_CONFIG_MQTT_CANCEL);

  assert(vg_config_mqtt_request_parse(
           topic, put, strlen(put), "vg-test", true, 0, 1,
           &parsed, candidate_buffer, sizeof(candidate_buffer),
           error, sizeof(error)) == 0);
  assert(parsed.expected_revision == 0);
  assert(parsed.revision == 1);
  assert(strstr(parsed.candidate_json, "add_sensor") != NULL);

  assert(vg_config_mqtt_request_parse(
           "vg/vg-other/config/request", device, strlen(device),
           "vg-test", true, 0, 1, &parsed,
           candidate_buffer, sizeof(candidate_buffer),
           error, sizeof(error)) == -EINVAL);
  assert(vg_config_mqtt_request_parse(
           topic, device, strlen(device), "vg-test", false, 0, 1,
           &parsed, candidate_buffer, sizeof(candidate_buffer),
           error, sizeof(error)) == -EINVAL);
  assert(vg_config_mqtt_request_parse(
           topic, device, strlen(device), "vg-test", true, 1, 1,
           &parsed, candidate_buffer, sizeof(candidate_buffer),
           error, sizeof(error)) == -EINVAL);
  assert(vg_config_mqtt_request_parse(
           topic, device, strlen(device), "vg-test", true, 0, 0,
           &parsed, candidate_buffer, sizeof(candidate_buffer),
           error, sizeof(error)) == -EINVAL);
}

static void test_strict_rejections(void)
{
  const char *unknown =
    "{\"schema_version\":1,\"device_id\":\"vg-test\","
    "\"req_id\":\"req-device-1\",\"op\":\"query\",\"scope\":\"device\","
    "\"extra\":1}";
  const char *duplicate =
    "{\"schema_version\":1,\"device_id\":\"vg-test\","
    "\"device_id\":\"vg-test\",\"req_id\":\"req-device-1\","
    "\"op\":\"query\",\"scope\":\"device\"}";
  const char *wrong_device =
    "{\"schema_version\":1,\"device_id\":\"vg-other\","
    "\"req_id\":\"req-device-1\",\"op\":\"query\",\"scope\":\"device\"}";
  const char *remote_confirm =
    "{\"schema_version\":1,\"device_id\":\"vg-test\","
    "\"req_id\":\"req-confirm-1\",\"task_id\":\"task-1\","
    "\"op\":\"confirm\",\"session_id\":"
    "\"33333333333333333333333333333333\"}";
  struct vg_config_mqtt_request request;
  char error[64];
  char oversized[VG_CONFIG_MQTT_REQUEST_MAX + 2];

  assert(vg_config_mqtt_request_parse(
           topic, unknown, strlen(unknown), "vg-test", true, 0, 1,
           &request, candidate_buffer, sizeof(candidate_buffer),
           error, sizeof(error)) == -EINVAL);
  assert(vg_config_mqtt_request_parse(
           topic, duplicate, strlen(duplicate), "vg-test", true, 0, 1,
           &request, candidate_buffer, sizeof(candidate_buffer),
           error, sizeof(error)) == -EINVAL);
  assert(vg_config_mqtt_request_parse(
           topic, wrong_device, strlen(wrong_device), "vg-test", true, 0, 1,
           &request, candidate_buffer, sizeof(candidate_buffer),
           error, sizeof(error)) == -EINVAL);
  assert(vg_config_mqtt_request_parse(
           topic, remote_confirm, strlen(remote_confirm),
           "vg-test", true, 0, 1, &request,
           candidate_buffer, sizeof(candidate_buffer),
           error, sizeof(error)) == -EINVAL);
  memset(oversized, 'x', sizeof(oversized));
  assert(vg_config_mqtt_request_parse(
           topic, oversized, sizeof(oversized), "vg-test", true, 0, 1,
           &request, candidate_buffer, sizeof(candidate_buffer),
           error, sizeof(error)) == -EINVAL);
  assert(strcmp(error, "request_too_large") == 0);
}

static void test_responses(void)
{
  struct vg_config_device_snapshot snapshot;
  struct vg_config_task_status status;
  char json[VG_CONFIG_STATE_MAX];
  cJSON *root;

  memset(&snapshot, 0, sizeof(snapshot));
  fill_hex(snapshot.boot_id, sizeof(snapshot.boot_id), '1');
  fill_hex(snapshot.link_id, sizeof(snapshot.link_id), '2');
  snapshot.next_open_seq = 1;
  snapshot.config_state = VG_CONFIG_TABLE_EMPTY;
  snapshot.remote_config = true;
  snapshot.max_points = VG_DISCOVER_MAX_POINTS;
  snapshot.max_request_bytes = VG_CONFIG_REQUEST_MAX;
  snapshot.max_state_bytes = VG_CONFIG_STATE_MAX;
  snapshot.lease_ms = VG_CONFIG_LEASE_MS;
  snapshot.baud = 9600;
  snapshot.uptime_ms = 1000;
  assert(vg_config_mqtt_device_snapshot_json(
           json, sizeof(json), "vg-test", "req-device-1",
           &snapshot) == 0);
  root = cJSON_Parse(json);
  assert(cJSON_IsObject(root));
  assert(strcmp(cJSON_GetObjectItem(root, "kind")->valuestring,
                "device_snapshot") == 0);
  assert(cJSON_IsNull(cJSON_GetObjectItem(root, "active_task_id")));
  assert(cJSON_GetObjectItem(root, "capabilities") != NULL);
  cJSON_Delete(root);

  memset(&status, 0, sizeof(status));
  strcpy(status.task_id, "task-1");
  fill_hex(status.session_id, sizeof(status.session_id), '3');
  status.state = VG_CONFIG_SESSION_AWAITING_CONFIRM;
  status.revision = 1;
  status.state_seq = 2;
  status.has_payload_hash = true;
  fill_hex(status.payload_hash, sizeof(status.payload_hash), 'a');
  status.has_candidate_hash = true;
  fill_hex(status.candidate_hash, sizeof(status.candidate_hash), 'b');
  status.has_confirmation_id = true;
  fill_hex(status.confirmation_id, sizeof(status.confirmation_id), 'c');
  assert(vg_config_mqtt_state_json(
           json, sizeof(json), "vg-test", "req-put-1",
           "11111111111111111111111111111111",
           "22222222222222222222222222222222",
           &status, 1000, "unsynced") == 0);
  root = cJSON_Parse(json);
  assert(cJSON_IsObject(root));
  assert(strcmp(cJSON_GetObjectItem(root, "state")->valuestring,
                "awaiting_local_confirm") == 0);
  assert(cJSON_IsNull(cJSON_GetObjectItem(root, "base_hash")));
  assert(cJSON_IsNull(cJSON_GetObjectItem(root, "error")));
  cJSON_Delete(root);

  assert(vg_config_mqtt_request_error_json(
           json, sizeof(json), "vg-test", "req-bad-1",
           "11111111111111111111111111111111",
           "22222222222222222222222222222222",
           NULL, NULL, "revision_conflict", false,
           "/revision", "revision does not match") == 0);
  root = cJSON_Parse(json);
  assert(cJSON_IsObject(root));
  assert(strcmp(cJSON_GetObjectItem(root, "kind")->valuestring,
                "request_error") == 0);
  assert(cJSON_GetObjectItem(root, "error") != NULL);
  cJSON_Delete(root);
}

int main(void)
{
  test_requests();
  test_strict_rejections();
  test_responses();
  puts("test_config_mqtt_protocol: PASS");
  return 0;
}
