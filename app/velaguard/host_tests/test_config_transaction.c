#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../vg_config_transaction.h"
#include "../vg_file_store.h"
#include "../../../boot/ota_core/vg_ota_sha256.h"

struct io_fixture
{
  char points_path[128];
  int write_count;
  int write_fail_at;
  int read_count;
  int read_fail_at;
  int commit_count;
  int commit_result;
  bool hash_failure;
};

struct test_files
{
  char root[128];
  char points[160];
  char candidate[160];
  char source[160];
  char target[160];
  char journal[160];
  char receipt[160];
};

static const char *candidate_json =
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

static const char *target_json =
  "{\"schema_version\":1,\"bus\":{\"baud\":9600},"
  "\"points\":[{\"id\":\"temp\",\"name\":\"Temperature\",\"addr\":1,"
  "\"fc\":3,\"reg\":0,\"qty\":1,\"dtype\":\"int16\",\"scale\":0.1,"
  "\"unit\":\"C\",\"fail_n\":3}]}";

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

static void hash_text(const char *text, char out[VG_CONFIG_HASH_LEN])
{
  struct vg_ota_sha256 sha;
  uint8_t digest[32];

  vg_ota_sha256_init(&sha);
  vg_ota_sha256_update(&sha, (const uint8_t *)text, strlen(text));
  vg_ota_sha256_final(&sha, digest);
  digest_to_hex(digest, out);
}

static int read_file(const char *path, void *data,
                     size_t capacity, size_t *length)
{
  uint8_t *out = data;
  size_t used = 0;
  int fd;

  fd = open(path, O_RDONLY);
  if (fd < 0)
    {
      return -errno;
    }
  while (used < capacity)
    {
      ssize_t got = read(fd, out + used, capacity - used);
      if (got < 0 && errno == EINTR)
        {
          continue;
        }
      if (got < 0)
        {
          int ret = -errno;
          close(fd);
          return ret;
        }
      if (got == 0)
        {
          break;
        }
      used += (size_t)got;
    }
  if (used == capacity)
    {
      uint8_t extra;
      ssize_t got;
      do
        {
          got = read(fd, &extra, 1);
        }
      while (got < 0 && errno == EINTR);
      if (got != 0)
        {
          int ret = got < 0 ? -errno : -EFBIG;
          close(fd);
          return ret;
        }
    }
  if (close(fd) != 0)
    {
      return -errno;
    }
  *length = used;
  return 0;
}

static int write_file(void *ctx, const char *path,
                      const void *data, size_t length)
{
  struct io_fixture *fixture = ctx;

  fixture->write_count++;
  if (fixture->write_fail_at == fixture->write_count)
    {
      return -EIO;
    }
  return vg_file_replace(path, data, length, 0644);
}

static int read_file_port(void *ctx, const char *path,
                          void *data, size_t capacity, size_t *length)
{
  struct io_fixture *fixture = ctx;

  fixture->read_count++;
  if (fixture->read_fail_at == fixture->read_count)
    {
      return -EIO;
    }
  return read_file(path, data, capacity, length);
}

static int table_hash(void *ctx, bool *present, char hash[65])
{
  struct io_fixture *fixture = ctx;
  struct vg_discover_summary table;
  struct vg_ota_sha256 sha;
  uint8_t buffer[256];
  uint8_t digest[32];
  size_t total = 0;
  int ret;
  int fd;

  if (fixture->hash_failure)
    {
      return -EIO;
    }
  memset(&table, 0, sizeof(table));
  ret = vg_point_table_read(&table, fixture->points_path);
  if (ret == -ENOENT || (ret == 0 && table.n_points == 0))
    {
      *present = false;
      hash[0] = '\0';
      return 0;
    }
  if (ret != 0)
    {
      return ret;
    }

  fd = open(fixture->points_path, O_RDONLY);
  if (fd < 0)
    {
      *present = false;
      hash[0] = '\0';
      return errno == ENOENT ? 0 : -errno;
    }
  *present = true;
  vg_ota_sha256_init(&sha);
  for (;;)
    {
      ssize_t got = read(fd, buffer, sizeof(buffer));
      if (got < 0 && errno == EINTR)
        {
          continue;
        }
      if (got < 0)
        {
          int ret = -errno;
          close(fd);
          return ret;
        }
      if (got == 0)
        {
          break;
        }
      total += (size_t)got;
      vg_ota_sha256_update(&sha, buffer, (uint32_t)got);
    }
  if (total >= VG_POINT_TABLE_JSON_MAX)
    {
      close(fd);
      return -EFBIG;
    }
  if (close(fd) != 0)
    {
      return -errno;
    }
  vg_ota_sha256_final(&sha, digest);
  digest_to_hex(digest, hash);
  return 0;
}

static int commit_table(void *ctx, const void *json, size_t length)
{
  struct io_fixture *fixture = ctx;

  fixture->commit_count++;
  if (fixture->commit_result != 0)
    {
      return fixture->commit_result;
    }
  return vg_file_replace(fixture->points_path, json, length, 0644);
}

static void remove_path(const char *path)
{
  char derived[192];

  unlink(path);
  snprintf(derived, sizeof(derived), "%s.bak", path);
  unlink(derived);
  snprintf(derived, sizeof(derived), "%s.tmp", path);
  unlink(derived);
  snprintf(derived, sizeof(derived), "%s.btmp", path);
  unlink(derived);
}

static void fill_paths(struct test_files *files, int index)
{
  snprintf(files->root, sizeof(files->root),
           "config-transaction-%d", index);
  snprintf(files->points, sizeof(files->points),
           "%s/points.json", files->root);
  snprintf(files->candidate, sizeof(files->candidate),
           "%s/candidate.json", files->root);
  snprintf(files->source, sizeof(files->source),
           "%s/source.json", files->root);
  snprintf(files->target, sizeof(files->target),
           "%s/target.json", files->root);
  snprintf(files->journal, sizeof(files->journal),
           "%s/journal.json", files->root);
  snprintf(files->receipt, sizeof(files->receipt),
           "%s/receipt.json", files->root);
}

static void cleanup(struct test_files *files)
{
  remove_path(files->points);
  remove_path(files->candidate);
  remove_path(files->source);
  remove_path(files->target);
  remove_path(files->journal);
  remove_path(files->receipt);
  rmdir(files->root);
}

static struct vg_config_transaction_record make_record(void)
{
  struct vg_config_transaction_record record;

  memset(&record, 0, sizeof(record));
  strcpy(record.origin_boot_id,
         "11111111111111111111111111111111");
  strcpy(record.task_id, "transaction-task");
  strcpy(record.session_id,
         "22222222222222222222222222222222");
  record.revision = 1;
  hash_text(candidate_json, record.payload_hash);
  hash_text(candidate_json, record.source_hash);
  hash_text(target_json, record.candidate_hash);
  strcpy(record.confirmation_id,
         "33333333333333333333333333333333");
  return record;
}

static struct vg_config_transaction_ports make_ports(
  struct io_fixture *fixture)
{
  struct vg_config_transaction_ports ports;

  memset(&ports, 0, sizeof(ports));
  ports.write_file = write_file;
  ports.read_file = read_file_port;
  ports.current_table_hash = table_hash;
  ports.commit_table = commit_table;
  ports.ctx = fixture;
  return ports;
}

static void init_case(struct test_files *files, struct io_fixture *fixture,
                      struct vg_config_transaction *transaction, int index)
{
  struct vg_config_transaction_ports ports;

  fill_paths(files, index);
  cleanup(files);
  memset(fixture, 0, sizeof(*fixture));
  strcpy(fixture->points_path, files->points);
  ports = make_ports(fixture);
  assert(vg_config_transaction_init(transaction, files->root,
                                    files->points, &ports) == 0);
}

static int prepare(struct vg_config_transaction *transaction,
                   const struct vg_config_transaction_record *record)
{
  return vg_config_transaction_prepare(
    transaction, candidate_json, strlen(candidate_json),
    candidate_json, strlen(candidate_json),
    target_json, strlen(target_json), record);
}

static void test_round_trip_and_receipt(void)
{
  struct test_files files;
  struct io_fixture fixture;
  struct vg_config_transaction transaction;
  struct vg_config_transaction restarted;
  struct vg_config_transaction_record record = make_record();
  struct vg_config_receipt receipt;
  struct vg_config_receipt loaded;

  init_case(&files, &fixture, &transaction, 1);
  assert(prepare(&transaction, &record) == 0);
  assert(fixture.write_count == 4);
  assert(vg_config_transaction_commit(&transaction, &receipt) == 0);
  assert(receipt.outcome == VG_CONFIG_RECEIPT_APPLIED);
  assert(receipt.has_applied_hash);
  assert(strcmp(receipt.applied_hash, record.candidate_hash) == 0);
  assert(fixture.commit_count == 1);
  assert(!vg_config_transaction_recovery_blocked(&transaction));
  assert(vg_config_transaction_read_receipt(&transaction, &loaded) == 0);
  assert(loaded.outcome == VG_CONFIG_RECEIPT_APPLIED);
  assert(strcmp(loaded.record.task_id, record.task_id) == 0);

  {
    struct vg_config_transaction_ports ports = make_ports(&fixture);
    assert(vg_config_transaction_init(&restarted, files.root,
                                      files.points, &ports) == 0);
  }
  assert(vg_config_transaction_recover(&restarted, &loaded) == 0);
  assert(loaded.outcome == VG_CONFIG_RECEIPT_APPLIED);
  assert(!vg_config_transaction_recovery_blocked(&restarted));
  cleanup(&files);
}

static void test_failed_recovery(void)
{
  struct test_files files;
  struct io_fixture fixture;
  struct vg_config_transaction transaction;
  struct vg_config_transaction_record record = make_record();
  struct vg_config_receipt receipt;

  init_case(&files, &fixture, &transaction, 2);
  assert(prepare(&transaction, &record) == 0);
  assert(vg_config_transaction_recover(&transaction, &receipt) == 0);
  assert(receipt.outcome == VG_CONFIG_RECEIPT_FAILED);
  assert(!vg_config_transaction_recovery_blocked(&transaction));
  cleanup(&files);
}

static void test_existing_empty_table_is_base(void)
{
  struct test_files files;
  struct io_fixture fixture;
  struct vg_config_transaction transaction;
  struct vg_config_transaction_record record = make_record();
  const char *empty =
    "{\"schema_version\":1,\"bus\":{\"baud\":9600},\"points\":[]}";

  init_case(&files, &fixture, &transaction, 7);
  assert(vg_file_replace(files.points, empty, strlen(empty), 0644) == 0);
  assert(prepare(&transaction, &record) == 0);
  cleanup(&files);
}

static void test_unknown_blocks_prepare(void)
{
  struct test_files files;
  struct io_fixture fixture;
  struct vg_config_transaction transaction;
  struct vg_config_transaction_record record = make_record();
  struct vg_config_receipt receipt;
  const char *other =
    "{\"schema_version\":1,\"bus\":{\"baud\":9600},"
    "\"points\":[{\"id\":\"other\",\"name\":\"Other\",\"addr\":1,"
    "\"fc\":3,\"reg\":9,\"qty\":1,\"dtype\":\"int16\","
    "\"scale\":1,\"unit\":\"C\",\"fail_n\":3}]}";

  init_case(&files, &fixture, &transaction, 3);
  assert(prepare(&transaction, &record) == 0);
  assert(vg_file_replace(files.points, other, strlen(other), 0644) == 0);
  assert(vg_config_transaction_recover(&transaction, &receipt) == -EIO);
  assert(receipt.outcome == VG_CONFIG_RECEIPT_RECOVERY_UNKNOWN);
  assert(vg_config_transaction_recovery_blocked(&transaction));
  assert(prepare(&transaction, &record) == -EACCES);
  cleanup(&files);
}

static void test_crash_after_table_before_receipt(void)
{
  struct test_files files;
  struct io_fixture fixture;
  struct vg_config_transaction transaction;
  struct vg_config_transaction restarted;
  struct vg_config_transaction_record record = make_record();
  struct vg_config_receipt receipt;

  init_case(&files, &fixture, &transaction, 4);
  assert(prepare(&transaction, &record) == 0);
  assert(commit_table(&fixture, target_json, strlen(target_json)) == 0);
  remove_path(files.receipt);
  {
    struct vg_config_transaction_ports ports = make_ports(&fixture);
    assert(vg_config_transaction_init(&restarted, files.root,
                                      files.points, &ports) == 0);
  }
  assert(vg_config_transaction_recover(&restarted, &receipt) == 0);
  assert(receipt.outcome == VG_CONFIG_RECEIPT_APPLIED);
  assert(!vg_config_transaction_recovery_blocked(&restarted));
  cleanup(&files);
}

static void test_corrupt_target_is_unknown(void)
{
  struct test_files files;
  struct io_fixture fixture;
  struct vg_config_transaction transaction;
  struct vg_config_transaction_record record = make_record();
  struct vg_config_receipt receipt;
  const char *corrupt = "{\"broken\":true}";

  init_case(&files, &fixture, &transaction, 5);
  assert(prepare(&transaction, &record) == 0);
  assert(vg_file_replace(files.target, corrupt, strlen(corrupt), 0644) == 0);
  assert(vg_config_transaction_recover(&transaction, &receipt) == -EIO);
  assert(receipt.outcome == VG_CONFIG_RECEIPT_RECOVERY_UNKNOWN);
  assert(vg_config_transaction_recovery_blocked(&transaction));
  cleanup(&files);
}

static void test_missing_journal_blocks_new_config(void)
{
  struct test_files files;
  struct io_fixture fixture;
  struct vg_config_transaction transaction;
  struct vg_config_transaction_record record = make_record();
  struct vg_config_receipt receipt;

  init_case(&files, &fixture, &transaction, 6);
  assert(strlen(strrchr(files.candidate, '/') + 1) <= VG_STORE_NAME_MAX);
  assert(strlen(strrchr(files.source, '/') + 1) <= VG_STORE_NAME_MAX);
  assert(strlen(strrchr(files.target, '/') + 1) <= VG_STORE_NAME_MAX);
  assert(strlen(strrchr(files.journal, '/') + 1) <= VG_STORE_NAME_MAX);
  assert(strlen(strrchr(files.receipt, '/') + 1) <= VG_STORE_NAME_MAX);
  assert(prepare(&transaction, &record) == 0);
  remove_path(files.journal);
  assert(vg_config_transaction_recover(&transaction, &receipt) == -EIO);
  assert(vg_config_transaction_recovery_blocked(&transaction));
  assert(prepare(&transaction, &record) == -EACCES);
  cleanup(&files);
}

static void test_fault_stages(void)
{
  int index;

  for (index = 1; index <= 4; index++)
    {
      struct test_files files;
      struct io_fixture fixture;
      struct vg_config_transaction transaction;
      struct vg_config_transaction_record record = make_record();

      init_case(&files, &fixture, &transaction, 10 + index);
      fixture.write_fail_at = index;
      assert(prepare(&transaction, &record) == -EIO);
      assert(fixture.commit_count == 0);
      cleanup(&files);
    }

  {
    struct test_files files;
    struct io_fixture fixture;
    struct vg_config_transaction transaction;
    struct vg_config_transaction_record record = make_record();

    init_case(&files, &fixture, &transaction, 20);
    fixture.read_fail_at = 1;
    assert(prepare(&transaction, &record) == -EIO);
    cleanup(&files);
  }

  {
    struct test_files files;
    struct io_fixture fixture;
    struct vg_config_transaction transaction;
    struct vg_config_transaction_record record = make_record();
    struct vg_config_receipt receipt;

    init_case(&files, &fixture, &transaction, 21);
    fixture.commit_result = -ENOSPC;
    assert(prepare(&transaction, &record) == 0);
    assert(vg_config_transaction_commit(&transaction, &receipt)
           == -ENOSPC);
    assert(receipt.outcome == VG_CONFIG_RECEIPT_FAILED);
    assert(!vg_config_transaction_recovery_blocked(&transaction));
    cleanup(&files);
  }

  {
    struct test_files files;
    struct io_fixture fixture;
    struct vg_config_transaction transaction;
    struct vg_config_transaction restarted;
    struct vg_config_transaction_record record = make_record();
    struct vg_config_receipt receipt;

    init_case(&files, &fixture, &transaction, 22);
    assert(prepare(&transaction, &record) == 0);
    fixture.write_fail_at = 5;
    assert(vg_config_transaction_commit(&transaction, &receipt) == -EIO);
    assert(receipt.outcome == VG_CONFIG_RECEIPT_APPLIED);
    assert(vg_config_transaction_recovery_blocked(&transaction));
    fixture.write_fail_at = 0;
    {
      struct vg_config_transaction_ports ports = make_ports(&fixture);
      assert(vg_config_transaction_init(&restarted, files.root,
                                        files.points, &ports) == 0);
    }
    assert(vg_config_transaction_recover(&restarted, &receipt) == 0);
    assert(receipt.outcome == VG_CONFIG_RECEIPT_APPLIED);
    cleanup(&files);
  }
}

int main(void)
{
  test_round_trip_and_receipt();
  test_failed_recovery();
  test_existing_empty_table_is_base();
  test_unknown_blocks_prepare();
  test_crash_after_table_before_receipt();
  test_corrupt_target_is_unknown();
  test_missing_journal_blocks_new_config();
  test_fault_stages();
  return 0;
}
