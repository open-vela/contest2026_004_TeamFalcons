#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../vg_config_service.h"
#include "../../../boot/ota_core/vg_ota_sha256.h"

struct fixture
{
  struct vg_config_table_snapshot table;
  uint64_t now;
  uint8_t random_value;
  int load_count;
};

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

static void fill_hash(char out[VG_CONFIG_HASH_LEN], char value)
{
  memset(out, value, VG_CONFIG_HASH_LEN - 1);
  out[VG_CONFIG_HASH_LEN - 1] = '\0';
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

static void sha256_text(const char *text, char out[VG_CONFIG_HASH_LEN])
{
  struct vg_ota_sha256 sha;
  uint8_t digest[32];

  vg_ota_sha256_init(&sha);
  vg_ota_sha256_update(&sha, (const uint8_t *)text, strlen(text));
  vg_ota_sha256_final(&sha, digest);
  digest_to_hex(digest, out);
}

static int load_table(void *ctx, struct vg_config_table_snapshot *out)
{
  struct fixture *fixture = ctx;

  fixture->load_count++;
  *out = fixture->table;
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

static struct vg_config_open_request make_open(
  const char *req_id, const char *task_id, const char *boot_id,
  const char *link_id, const char *base_hash)
{
  struct vg_config_open_request request;

  memset(&request, 0, sizeof(request));
  strcpy(request.req_id, req_id);
  fill_hash(request.request_hash, 'd');
  strcpy(request.task_id, task_id);
  strcpy(request.boot_id, boot_id);
  strcpy(request.link_id, link_id);
  request.expected_open_seq = 1;
  request.has_base_hash = true;
  strcpy(request.base_hash, base_hash);
  return request;
}

static struct vg_config_put_request make_put(
  const char *req_id, char request_hash_value, const char *task_id,
  const char *boot_id, const char *link_id, const char *session_id,
  const char *base_hash, uint32_t expected_revision,
  uint32_t revision, const char *candidate)
{
  struct vg_config_put_request request;

  memset(&request, 0, sizeof(request));
  strcpy(request.req_id, req_id);
  fill_hash(request.request_hash, request_hash_value);
  strcpy(request.task_id, task_id);
  strcpy(request.boot_id, boot_id);
  strcpy(request.link_id, link_id);
  strcpy(request.session_id, session_id);
  request.expected_revision = expected_revision;
  request.revision = revision;
  request.has_base_hash = true;
  strcpy(request.base_hash, base_hash);
  sha256_text(candidate, request.payload_hash);
  request.candidate_json = candidate;
  request.candidate_json_len = strlen(candidate);
  return request;
}

static void replace_once(const char *input, const char *needle,
                         const char *replacement,
                         char *out, size_t capacity)
{
  const char *found = strstr(input, needle);
  size_t prefix;
  size_t suffix_length;

  assert(found != NULL);
  prefix = (size_t)(found - input);
  suffix_length = strlen(found + strlen(needle));
  assert(prefix + strlen(replacement) + suffix_length + 1 <= capacity);
  memcpy(out, input, prefix);
  memcpy(out + prefix, replacement, strlen(replacement));
  memcpy(out + prefix + strlen(replacement), found + strlen(needle),
         suffix_length + 1);
}

int main(void)
{
  struct fixture fixture;
  struct vg_config_service_ports ports;
  struct vg_config_service service;
  struct vg_config_device_snapshot snapshot;
  struct vg_config_open_request open_request;
  struct vg_config_put_request put;
  struct vg_config_put_request duplicate;
  struct vg_config_task_status status;
  struct vg_config_task_status duplicate_status;
  struct vg_config_pending_candidate pending;
  struct vg_config_pending_candidate first_pending;
  char boot_id[VG_CONFIG_HEX32_LEN];
  char link_id[VG_CONFIG_HEX32_LEN];
  char session_id[VG_CONFIG_HEX32_LEN];
  char base_hash[VG_CONFIG_HASH_LEN];
  char invalid_candidate[4096];
  char corrected_candidate[4096];
  int loads_after_put;

  memset(&fixture, 0, sizeof(fixture));
  fixture.now = 1000;
  fixture.table.state = VG_CONFIG_TABLE_VALID;
  fixture.table.point_count = 1;
  strcpy(fixture.table.point_ids[0], "external");
  fixture.table.slave_addresses[0] = 2;
  fixture.table.slave_count = 1;
  fixture.table.has_base_hash = true;
  fill_hash(fixture.table.base_hash, 'a');
  fixture.table.current.n_points = 1;
  strcpy(fixture.table.current.devpath, "/dev/rs485");
  fixture.table.current.baud = 9600;
  strcpy(fixture.table.current.points[0].id, "external");
  strcpy(fixture.table.current.points[0].name, "External");
  fixture.table.current.points[0].addr = 2;
  fixture.table.current.points[0].fc = 3;
  fixture.table.current.points[0].reg = 5;
  fixture.table.current.points[0].qty = 1;
  strcpy(fixture.table.current.points[0].dtype, "uint16");
  fixture.table.current.points[0].scale = 1;
  fixture.table.current.points[0].fail_n = 3;
  fill_hash(base_hash, 'a');

  ports.load_table = load_table;
  ports.random_bytes = random_bytes;
  ports.now_ms = now_ms;
  ports.ctx = &fixture;
  assert(vg_config_service_init(&service, &ports) == 0);
  assert(vg_config_service_new_link(&service) == 0);
  strcpy(boot_id, service.boot_id);
  strcpy(link_id, service.link_id);

  open_request = make_open("open-put", "put-task", boot_id, link_id,
                           base_hash);
  assert(vg_config_service_open(&service, &open_request, &status) == 0);
  strcpy(session_id, status.session_id);

  strcpy(invalid_candidate, good_candidate);
  {
    char *field = strstr(invalid_candidate, "\"reg\":0");
    assert(field != NULL);
    field[6] = '1';
  }
  put = make_put("put-invalid", '1', "put-task", boot_id, link_id,
                 session_id, base_hash, 0, 1, invalid_candidate);
  assert(vg_config_service_put(&service, &put, &status) == -EINVAL);
  assert(status.state == VG_CONFIG_SESSION_OPENED);
  assert(status.revision == 0);
  assert(strcmp(status.error_code, "invalid_candidate") == 0);
  assert(vg_config_service_pending_candidate(&service, &pending) == -ENOENT);
  assert(fixture.table.current.n_points == 1);
  assert(strcmp(fixture.table.current.points[0].id, "external") == 0);

  put = make_put("put-valid", '2', "put-task", boot_id, link_id,
                 session_id, base_hash, 0, 1, good_candidate);
  assert(vg_config_service_put(&service, &put, &status) == 0);
  assert(status.state == VG_CONFIG_SESSION_AWAITING_CONFIRM);
  assert(status.revision == 1);
  assert(status.state_seq == 5);
  assert(status.has_payload_hash);
  assert(status.has_candidate_hash);
  assert(status.has_confirmation_id);
  assert(vg_config_service_pending_candidate(&service, &first_pending) == 0);
  assert(first_pending.valid);
  assert(first_pending.revision == 1);
  assert(first_pending.target_len > 0);
  assert(strstr(first_pending.target_json, "\"id\":\"external\"") != NULL);
  assert(strstr(first_pending.target_json, "\"id\":\"temp\"") != NULL);
  assert(fixture.table.current.n_points == 1);
  assert(vg_config_service_snapshot(&service, &snapshot) == 0);
  assert(snapshot.point_count == 1);
  loads_after_put = fixture.load_count;

  duplicate = put;
  assert(vg_config_service_put(&service, &duplicate,
                               &duplicate_status) == 0);
  assert(strcmp(duplicate_status.confirmation_id,
                first_pending.confirmation_id) == 0);
  assert(fixture.load_count == loads_after_put);
  duplicate.request_hash[0] = '3';
  assert(vg_config_service_put(&service, &duplicate,
                               &duplicate_status) == -EEXIST);
  assert(fixture.load_count == loads_after_put);

  duplicate = put;
  duplicate.payload_hash[0] = '0';
  assert(vg_config_service_put(&service, &duplicate,
                               &duplicate_status) == -EINVAL);
  assert(fixture.load_count == loads_after_put);
  assert(vg_config_service_pending_candidate(&service, &pending) == 0);
  assert(strcmp(pending.confirmation_id, first_pending.confirmation_id) == 0);

  duplicate = put;
  duplicate.expected_revision = 1;
  duplicate.revision = 3;
  assert(vg_config_service_put(&service, &duplicate,
                               &duplicate_status) == -EINVAL);
  assert(fixture.load_count == loads_after_put);

  replace_once(good_candidate, "\"unit\":\"C\"", "\"unit\":\"F\"",
               corrected_candidate, sizeof(corrected_candidate));
  put = make_put("put-replace", '4', "put-task", boot_id, link_id,
                 session_id, base_hash, 1, 2, corrected_candidate);
  assert(vg_config_service_put(&service, &put, &status) == 0);
  assert(status.state == VG_CONFIG_SESSION_AWAITING_CONFIRM);
  assert(status.revision == 2);
  assert(strcmp(status.confirmation_id,
                first_pending.confirmation_id) != 0);
  assert(vg_config_service_pending_candidate(&service, &pending) == 0);
  assert(strstr(pending.target_json, "\"unit\":\"F\"") != NULL);

  replace_once(good_candidate, "\"unit\":\"C\"", "\"unit\":\" \"",
               invalid_candidate, sizeof(invalid_candidate));
  put = make_put("put-reject-rev3", '5', "put-task", boot_id, link_id,
                 session_id, base_hash, 2, 3, invalid_candidate);
  assert(vg_config_service_put(&service, &put, &status) == -EINVAL);
  assert(status.state == VG_CONFIG_SESSION_OPENED);
  assert(status.revision == 2);
  assert(!status.has_confirmation_id);
  assert(vg_config_service_pending_candidate(&service, &pending) == -ENOENT);

  replace_once(good_candidate, "\"name\":\"Temperature\"",
               "\"name\":\"Temperature2\"", corrected_candidate,
               sizeof(corrected_candidate));
  put = make_put("put-corrected", '6', "put-task", boot_id, link_id,
                 session_id, base_hash, 2, 3, corrected_candidate);
  assert(vg_config_service_put(&service, &put, &status) == 0);
  assert(status.state == VG_CONFIG_SESSION_AWAITING_CONFIRM);
  assert(status.revision == 3);
  assert(vg_config_service_query_task(&service, "put-task", &duplicate_status)
         == 0);
  assert(duplicate_status.revision == 3);
  return 0;
}
