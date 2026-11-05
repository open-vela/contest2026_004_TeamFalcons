#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"

#include "../../../boot/ota_core/vg_ota_sha256.h"
#include "../vg_config_gate.h"
#include "../vg_config_mqtt_service.h"

struct fixture
{
  struct vg_config_table_snapshot table;
  uint64_t now;
  uint8_t random_value;
  int commit_count;
  int notify_count;
  char committed_json[VG_POINT_TABLE_JSON_MAX];
  struct vg_discover_summary committed_table;
};

struct fake_wire
{
  struct vg_config_mqtt_message message;
  char published[8][VG_CONFIG_STATE_MAX];
  uint16_t tickets[8];
  unsigned publish_count;
  uint16_t next_ticket;
  bool connected;
  bool ready;
  int poll_result;
};

static const char *device_id = "vg-test";

static const char *good_candidate =
  "{"
  "\"candidate_schema_version\":1,"
  "\"mode\":\"add_sensor\","
  "\"bus\":{\"baud\":9600},"
  "\"sensor\":{\"slave_address\":1,\"model\":\"TH-01\"},"
  "\"manual\":{\"manual_id\":\"manual-01\","
  "\"sha256\":\"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\"},"
  "\"evidence\":[{\"id\":\"ev1\",\"page\":12,"
  "\"quote\":\"Temp register 0x0000, int16, scale 0.1, unit C.\"}],"
  "\"points\":[{\"id\":\"temp\",\"name\":\"Temperature\",\"addr\":1,"
  "\"fc\":3,\"reg\":0,\"qty\":1,\"dtype\":\"int16\",\"scale\":0.1,"
  "\"offset\":0,\"unit\":\"C\","
  "\"source\":{\"evidence_ids\":[\"ev1\"],"
  "\"original_address\":\"0x0000\",\"address_base\":0}}]"
  "}";

static void sha256_text(const char *text, char out[VG_CONFIG_HASH_LEN])
{
  static const char digits[] = "0123456789abcdef";
  struct vg_ota_sha256 sha;
  uint8_t digest[32];
  size_t i;

  vg_ota_sha256_init(&sha);
  vg_ota_sha256_update(&sha, (const uint8_t *)text, strlen(text));
  vg_ota_sha256_final(&sha, digest);
  for (i = 0; i < 32; i++)
    {
      out[i * 2] = digits[digest[i] >> 4];
      out[i * 2 + 1] = digits[digest[i] & 0x0f];
    }
  out[64] = '\0';
}

static int load_table(void *ctx, struct vg_config_table_snapshot *out)
{
  *out = ((struct fixture *)ctx)->table;
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

static int commit_table(void *ctx, const void *json, size_t length,
                        const struct vg_discover_summary *table)
{
  struct fixture *fixture = ctx;

  assert(length < sizeof(fixture->committed_json));
  memcpy(fixture->committed_json, json, length);
  fixture->committed_json[length] = '\0';
  fixture->committed_table = *table;
  fixture->commit_count++;
  return 0;
}

static int notify_table(void *ctx)
{
  ((struct fixture *)ctx)->notify_count++;
  return 0;
}

static int wire_open(void *ctx, const char *id)
{
  struct fake_wire *wire = ctx;

  assert(strcmp(id, device_id) == 0);
  wire->connected = true;
  return 0;
}

static int wire_poll(void *ctx)
{
  struct fake_wire *wire = ctx;
  return wire->connected ? wire->poll_result : -ENOTCONN;
}

static bool wire_online(void *ctx)
{
  return ((struct fake_wire *)ctx)->connected;
}

static void wire_close(void *ctx)
{
  struct fake_wire *wire = ctx;

  wire->connected = false;
  memset(&wire->message, 0, sizeof(wire->message));
}

static struct vg_config_mqtt_message *wire_peek(void *ctx)
{
  struct fake_wire *wire = ctx;
  return wire->message.used ? &wire->message : NULL;
}

static void wire_release(void *ctx)
{
  memset(&((struct fake_wire *)ctx)->message, 0,
         sizeof(((struct fake_wire *)ctx)->message));
}

static int wire_publish(void *ctx, const char *json, uint16_t *ticket)
{
  struct fake_wire *wire = ctx;
  unsigned index = wire->publish_count++;

  assert(index < 8);
  assert(strlen(json) < sizeof(wire->published[index]));
  strcpy(wire->published[index], json);
  wire->tickets[index] = ++wire->next_ticket;
  *ticket = wire->tickets[index];
  return 0;
}

static int wire_acked(void *ctx, uint16_t ticket)
{
  struct fake_wire *wire = ctx;
  unsigned i;

  for (i = 0; i < wire->publish_count; i++)
    {
      if (wire->tickets[i] == ticket)
        {
          return 1;
        }
    }
  return 0;
}

static bool wire_ready(void *ctx)
{
  return ((struct fake_wire *)ctx)->ready;
}

static uint64_t wire_now(void *ctx)
{
  struct fake_wire *wire = ctx;
  wire->next_ticket += 0;
  return (uint64_t)wire->publish_count * 1000u;
}

static void wire_delay(void *ctx, uint32_t delay)
{
  (void)ctx;
  (void)delay;
}

static void put_request(struct fake_wire *wire, const char *json)
{
  assert(!wire->message.used);
  strcpy(wire->message.topic, "vg/vg-test/config/request");
  assert(strlen(json) <= VG_CONFIG_MQTT_REQUEST_MAX);
  strcpy(wire->message.payload, json);
  wire->message.length = strlen(json);
  wire->message.qos = 1;
  wire->message.retain = 0;
  wire->message.used = true;
}

static cJSON *last_json(struct fake_wire *wire)
{
  assert(wire->publish_count > 0);
  return cJSON_Parse(wire->published[wire->publish_count - 1]);
}

static void make_request(
  char *out,
  size_t capacity,
  const char *req_id,
  const char *task_id,
  const char *op,
  const char *boot_id,
  const char *link_id,
  const char *session_id,
  uint32_t expected_open_seq,
  uint32_t expected_revision,
  uint32_t revision)
{
  char payload_hash[VG_CONFIG_HASH_LEN];

  if (strcmp(op, "open") == 0)
    {
      snprintf(out, capacity,
               "{\"schema_version\":1,\"device_id\":\"%s\","
               "\"req_id\":\"%s\",\"task_id\":\"%s\",\"op\":\"open\","
               "\"boot_id\":\"%s\",\"link_id\":\"%s\","
               "\"expected_open_seq\":%u,\"base_hash\":null}",
               device_id, req_id, task_id, boot_id, link_id,
               (unsigned)expected_open_seq);
    }
  else if (strcmp(op, "put") == 0)
    {
      char escaped[VG_CONFIG_REQUEST_MAX];
      size_t in = 0;
      size_t at = 0;

      while (good_candidate[in] != '\0')
        {
          if (good_candidate[in] == '"' || good_candidate[in] == '\\')
            {
              escaped[at++] = '\\';
            }
          escaped[at++] = good_candidate[in++];
        }
      escaped[at] = '\0';
      sha256_text(good_candidate, payload_hash);
      snprintf(out, capacity,
               "{\"schema_version\":1,\"device_id\":\"%s\","
               "\"req_id\":\"%s\",\"task_id\":\"%s\",\"op\":\"put\","
               "\"boot_id\":\"%s\",\"link_id\":\"%s\","
               "\"session_id\":\"%s\",\"expected_revision\":%u,"
               "\"revision\":%u,\"base_hash\":null,"
               "\"payload_sha256\":\"%s\","
               "\"candidate_json\":\"%s\"}",
               device_id, req_id, task_id, boot_id, link_id, session_id,
               (unsigned)expected_revision, (unsigned)revision,
               payload_hash, escaped);
    }
  else
    {
      snprintf(out, capacity,
               "{\"schema_version\":1,\"device_id\":\"%s\","
               "\"req_id\":\"%s\",\"task_id\":\"%s\",\"op\":\"cancel\","
               "\"boot_id\":\"%s\",\"link_id\":\"%s\","
               "\"session_id\":\"%s\"}",
               device_id, req_id, task_id, boot_id, link_id, session_id);
    }
}

static int confirm_pending(struct vg_config_service *service)
{
  struct vg_config_pending_candidate pending;
  struct vg_config_confirm_request request;
  struct vg_config_task_status status;

  assert(vg_config_service_pending_candidate(service, &pending) == 0);
  memset(&request, 0, sizeof(request));
  strcpy(request.task_id, pending.task_id);
  strcpy(request.session_id, pending.session_id);
  request.revision = pending.revision;
  request.has_base_hash = pending.has_base_hash;
  strcpy(request.base_hash, pending.base_hash);
  strcpy(request.payload_hash, pending.payload_hash);
  strcpy(request.candidate_hash, pending.candidate_hash);
  strcpy(request.confirmation_id, pending.confirmation_id);
  return vg_config_service_confirm(service, &request, &status);
}

static void test_request_flow_and_disconnect(void)
{
  struct fixture fixture;
  struct fake_wire wire;
  struct vg_config_service config;
  struct vg_config_service_ports config_ports;
  struct vg_config_mqtt_service service;
  struct vg_config_mqtt_service_ports ports;
  struct vg_config_mqtt_service_status service_status;
  char json[VG_CONFIG_REQUEST_MAX];
  char session[VG_CONFIG_HEX32_LEN];
  char task[VG_CONFIG_ID_MAX];
  cJSON *root;
  cJSON *session_json;
  char *session_string;

  memset(&fixture, 0, sizeof(fixture));
  fixture.now = 1000;
  fixture.table.state = VG_CONFIG_TABLE_EMPTY;
  fixture.table.current.baud = 9600;
  strcpy(fixture.table.current.devpath, "/dev/rs485");
  memset(&config_ports, 0, sizeof(config_ports));
  config_ports.load_table = load_table;
  config_ports.random_bytes = random_bytes;
  config_ports.now_ms = now_ms;
  config_ports.ctx = &fixture;
  config_ports.commit_table = commit_table;
  config_ports.notify_table = notify_table;
  assert(vg_config_gate_is_idle());
  assert(vg_config_service_init(&config, &config_ports) == 0);

  memset(&wire, 0, sizeof(wire));
  wire.ready = true;
  memset(&ports, 0, sizeof(ports));
  ports.open_connection = wire_open;
  ports.poll_connection = wire_poll;
  ports.online = wire_online;
  ports.close_connection = wire_close;
  ports.peek = wire_peek;
  ports.release = wire_release;
  ports.publish = wire_publish;
  ports.acked = wire_acked;
  ports.ready = wire_ready;
  ports.now_ms = wire_now;
  ports.delay_ms = wire_delay;
  ports.ctx = &wire;
  assert(vg_config_mqtt_service_init(
           &service, &config, device_id, &ports) == 0);

  put_request(
    &wire,
    "{\"schema_version\":1,\"device_id\":\"vg-test\","
    "\"req_id\":\"req-device-1\",\"op\":\"query\",\"scope\":\"device\"}");
  assert(vg_config_mqtt_service_pump(&service) == 0);
  assert(wire.publish_count == 1);
  root = last_json(&wire);
  assert(strcmp(cJSON_GetObjectItem(root, "kind")->valuestring,
                "device_snapshot") == 0);
  cJSON_Delete(root);

  make_request(json, sizeof(json), "req-open-1", "task-1", "open",
               config.boot_id, config.link_id, NULL,
               config.next_open_seq, 0, 0);
  put_request(&wire, json);
  assert(vg_config_mqtt_service_pump(&service) == 0);
  assert(wire.publish_count == 2);
  root = last_json(&wire);
  assert(strcmp(cJSON_GetObjectItem(root, "state")->valuestring,
                "opened") == 0);
  session_json = cJSON_GetObjectItem(root, "session_id");
  assert(cJSON_IsString(session_json));
  session_string = strdup(session_json->valuestring);
  assert(session_string != NULL);
  strcpy(session, session_string);
  free(session_string);
  cJSON_Delete(root);

  make_request(json, sizeof(json), "req-put-1", "task-1", "put",
               config.boot_id, config.link_id, session, 0, 0, 1);
  put_request(&wire, json);
  assert(vg_config_mqtt_service_pump(&service) == 0);
  assert(wire.publish_count == 3);
  root = last_json(&wire);
  assert(strcmp(cJSON_GetObjectItem(root, "state")->valuestring,
                "awaiting_local_confirm") == 0);
  cJSON_Delete(root);

  make_request(json, sizeof(json), "req-cancel-1", "task-1", "cancel",
               config.boot_id, config.link_id, session, 0, 0, 0);
  put_request(&wire, json);
  assert(vg_config_mqtt_service_pump(&service) == 0);
  assert(wire.publish_count == 4);
  root = last_json(&wire);
  assert(strcmp(cJSON_GetObjectItem(root, "state")->valuestring,
                "cancelled") == 0);
  cJSON_Delete(root);
  assert(fixture.commit_count == 0);

  make_request(json, sizeof(json), "req-open-2", "task-2", "open",
               config.boot_id, config.link_id, NULL,
               config.next_open_seq, 0, 0);
  put_request(&wire, json);
  assert(vg_config_mqtt_service_pump(&service) == 0);
  root = last_json(&wire);
  session_json = cJSON_GetObjectItem(root, "session_id");
  assert(cJSON_IsString(session_json));
  strcpy(session, session_json->valuestring);
  cJSON_Delete(root);

  make_request(json, sizeof(json), "req-put-2", "task-2", "put",
               config.boot_id, config.link_id, session, 0, 0, 1);
  put_request(&wire, json);
  assert(vg_config_mqtt_service_pump(&service) == 0);
  assert(fixture.commit_count == 0);
  assert(confirm_pending(&config) == 0);
  assert(fixture.commit_count == 1);
  assert(fixture.notify_count == 1);
  assert(vg_config_mqtt_service_pump(&service) == 0);
  root = last_json(&wire);
  assert(strcmp(cJSON_GetObjectItem(root, "state")->valuestring,
                "applied") == 0);
  assert(cJSON_IsNull(cJSON_GetObjectItem(root, "req_id")));
  cJSON_Delete(root);

  strcpy(task, "task-2");
  wire.poll_result = -EIO;
  assert(vg_config_mqtt_service_pump(&service) == -ENOTCONN);
  assert(!wire.connected);
  assert(vg_config_mqtt_service_status(&service, &service_status) == 0);
  assert(strcmp(service_status.error, "link_lost") == 0);
  assert(vg_config_service_pending_candidate(&config,
                                             &(struct vg_config_pending_candidate){0})
         == -ENOENT);

  wire.poll_result = 0;
  assert(vg_config_mqtt_service_pump(&service) == 0);
  assert(wire.connected);
  assert(config.session_active == 0);
  assert(strcmp(config.session.task_id, task) == 0);
  assert(vg_config_gate_is_idle());
}

int main(void)
{
  test_request_flow_and_disconnect();
  puts("test_config_mqtt_service: PASS");
  return 0;
}
