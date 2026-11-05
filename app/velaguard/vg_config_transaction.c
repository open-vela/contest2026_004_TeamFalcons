/****************************************************************************
 * app/velaguard/vg_config_transaction.c
 ****************************************************************************/

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "vg_config_transaction.h"
#include "vg_discover.h"
#include "vg_file_store.h"
#include "../../boot/ota_core/vg_ota_sha256.h"

#define TRANSACTION_BLOB_SIZE 512
#define TRANSACTION_BODY_SIZE 480
#define TRANSACTION_VERSION 1
#define TRANSACTION_KIND_JOURNAL 1
#define TRANSACTION_FLAG_HAS_BASE 0x01
#define TRANSACTION_FLAG_HAS_APPLIED 0x02

static const uint8_t journal_magic[8] =
  { 'V', 'G', 'C', 'F', 'G', 'J', '0', '1' };
static const uint8_t receipt_magic[8] =
  { 'V', 'G', 'C', 'F', 'G', 'R', '0', '1' };

static int copy_path(char *out, size_t capacity, const char *value)
{
  size_t length;

  if (out == NULL || capacity == 0 || value == NULL || value[0] == '\0')
    {
      return -EINVAL;
    }

  length = strlen(value);
  if (length >= capacity)
    {
      return -ENAMETOOLONG;
    }

  memcpy(out, value, length + 1);
  return 0;
}

static int join_path(char *out, size_t capacity,
                     const char *root, const char *name)
{
  int length;

  if (strlen(name) > VG_STORE_NAME_MAX)
    {
      return -ENAMETOOLONG;
    }

  length = snprintf(out, capacity, "%s/%s", root, name);
  return length < 0 || (size_t)length >= capacity ?
         -ENAMETOOLONG : 0;
}

static void digest_to_hex(const uint8_t digest[32], char out[65])
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

static void hash_hex(const void *data, size_t length, char out[65])
{
  struct vg_ota_sha256 sha;
  uint8_t digest[32];

  vg_ota_sha256_init(&sha);
  vg_ota_sha256_update(&sha, data, (uint32_t)length);
  vg_ota_sha256_final(&sha, digest);
  digest_to_hex(digest, out);
  memset(digest, 0, sizeof(digest));
  memset(&sha, 0, sizeof(sha));
}

static void put_u32(uint8_t *out, uint32_t value)
{
  out[0] = (uint8_t)(value >> 24);
  out[1] = (uint8_t)(value >> 16);
  out[2] = (uint8_t)(value >> 8);
  out[3] = (uint8_t)value;
}

static uint32_t get_u32(const uint8_t *in)
{
  return ((uint32_t)in[0] << 24) |
         ((uint32_t)in[1] << 16) |
         ((uint32_t)in[2] << 8) |
         (uint32_t)in[3];
}

static int copy_fixed(uint8_t *out, size_t capacity,
                      const char *value, bool allow_empty)
{
  size_t length;

  if (value == NULL)
    {
      return -EINVAL;
    }
  length = strlen(value);
  if (length > capacity || (!allow_empty && length == 0))
    {
      return -EINVAL;
    }
  memset(out, 0, capacity);
  memcpy(out, value, length);
  return 0;
}

static int load_fixed(const uint8_t *in, size_t length,
                      char *out, size_t capacity, bool allow_empty)
{
  size_t used = 0;
  bool ended = false;
  size_t i;

  if (length == 0 || out == NULL || capacity <= length)
    {
      return -EINVAL;
    }

  for (i = 0; i < length; i++)
    {
      if (!ended && in[i] != '\0')
        {
          used++;
        }
      else if (!ended && in[i] == '\0')
        {
          ended = true;
        }
      else if (ended && in[i] != '\0')
        {
          return -EINVAL;
        }
    }

  if (!allow_empty && used == 0)
    {
      return -EINVAL;
    }

  memcpy(out, in, used);
  out[used] = '\0';
  return 0;
}

static bool valid_text_id(const char *value)
{
  size_t length;
  size_t i;

  if (value == NULL)
    {
      return false;
    }
  length = strlen(value);
  if (length < 1 || length >= VG_CONFIG_ID_MAX)
    {
      return false;
    }
  for (i = 0; i < length; i++)
    {
      char c = value[i];
      if (!((c >= 'A' && c <= 'Z') ||
            (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') || c == '_' || c == '-'))
        {
          return false;
        }
    }
  return true;
}

static bool valid_hex(const char *value, size_t length)
{
  size_t i;

  if (value == NULL || strlen(value) != length)
    {
      return false;
    }
  for (i = 0; i < length; i++)
    {
      if (!((value[i] >= '0' && value[i] <= '9') ||
            (value[i] >= 'a' && value[i] <= 'f')))
        {
          return false;
        }
    }
  return true;
}

static int validate_record(
  const struct vg_config_transaction_record *record)
{
  if (record == NULL || !valid_hex(record->origin_boot_id, 32) ||
      !valid_text_id(record->task_id) ||
      !valid_hex(record->session_id, 32) || record->revision == 0 ||
      (record->has_base_hash && !valid_hex(record->base_hash, 64)) ||
      (!record->has_base_hash && record->base_hash[0] != '\0') ||
      !valid_hex(record->payload_hash, 64) ||
      !valid_hex(record->source_hash, 64) ||
      !valid_hex(record->candidate_hash, 64) ||
      !valid_hex(record->confirmation_id, 32))
    {
      return -EINVAL;
    }

  return 0;
}

static int file_read_default(void *ctx, const char *path,
                             void *data, size_t capacity, size_t *length)
{
  uint8_t *out = data;
  size_t used = 0;
  int fd;

  (void)ctx;
  if (path == NULL || (data == NULL && capacity != 0) || length == NULL)
    {
      return -EINVAL;
    }

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

static int file_write_default(void *ctx, const char *path,
                              const void *data, size_t length)
{
  (void)ctx;
  return vg_file_replace(path, data, length, 0644);
}

static int commit_table_default(void *ctx, const void *json, size_t length)
{
  struct vg_config_transaction *transaction = ctx;

  return vg_point_table_commit_json(json, length,
                                    transaction->points_path);
}

static int table_hash_default(void *ctx, bool *present, char hash[65])
{
  struct vg_config_transaction *transaction = ctx;
  struct vg_discover_summary *table;
  struct vg_ota_sha256 sha;
  uint8_t buffer[256];
  uint8_t digest[32];
  size_t total = 0;
  int ret;
  int fd;

  if (transaction == NULL || present == NULL || hash == NULL)
    {
      return -EINVAL;
    }
  table = malloc(sizeof(*table));
  if (table == NULL)
    {
      return -ENOMEM;
    }
  ret = vg_point_table_read(table, transaction->points_path);
  if (ret == -ENOENT || (ret == 0 && table->n_points == 0))
    {
      free(table);
      *present = false;
      hash[0] = '\0';
      return 0;
    }
  if (ret != 0)
    {
      free(table);
      return ret;
    }
  free(table);

  fd = open(transaction->points_path, O_RDONLY);
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
      if (total >= VG_POINT_TABLE_JSON_MAX)
        {
          close(fd);
          return -EFBIG;
        }
      vg_ota_sha256_update(&sha, buffer, (uint32_t)got);
    }
  if (close(fd) != 0)
    {
      return -errno;
    }
  vg_ota_sha256_final(&sha, digest);
  digest_to_hex(digest, hash);
  memset(digest, 0, sizeof(digest));
  memset(&sha, 0, sizeof(sha));
  return 0;
}

static int encode_blob(const uint8_t magic[8], uint8_t kind,
                       uint8_t flags,
                       const struct vg_config_transaction_record *record,
                       uint32_t candidate_len, uint32_t target_len,
                       const char *error_code, uint8_t out[512])
{
  struct vg_ota_sha256 sha;
  uint8_t digest[32];

  memset(out, 0, TRANSACTION_BLOB_SIZE);
  memcpy(out, magic, 8);
  out[8] = TRANSACTION_VERSION;
  out[9] = kind;
  out[10] = flags;
  put_u32(out + 12, record->revision);
  if (copy_fixed(out + 16, 32, record->origin_boot_id, false) != 0 ||
      copy_fixed(out + 48, 64, record->task_id, false) != 0 ||
      copy_fixed(out + 112, 32, record->session_id, false) != 0 ||
      copy_fixed(out + 144, 64,
                 record->has_base_hash ? record->base_hash : "", true) != 0 ||
      copy_fixed(out + 208, 64, record->payload_hash, false) != 0 ||
      copy_fixed(out + 272, 64, record->source_hash, false) != 0 ||
      copy_fixed(out + 336, 64, record->candidate_hash, false) != 0 ||
      copy_fixed(out + 400, 32, record->confirmation_id, false) != 0)
    {
      return -EINVAL;
    }
  put_u32(out + 432, candidate_len);
  put_u32(out + 436, target_len);
  if (error_code != NULL &&
      copy_fixed(out + 440, 31, error_code, true) != 0)
    {
      return -EINVAL;
    }

  vg_ota_sha256_init(&sha);
  vg_ota_sha256_update(&sha, out, TRANSACTION_BODY_SIZE);
  vg_ota_sha256_final(&sha, digest);
  memcpy(out + TRANSACTION_BODY_SIZE, digest, sizeof(digest));
  memset(digest, 0, sizeof(digest));
  memset(&sha, 0, sizeof(sha));
  return 0;
}

static int decode_record(const uint8_t in[512],
                         struct vg_config_transaction_record *record,
                         uint32_t *candidate_len, uint32_t *target_len,
                         char error_code[VG_CONFIG_TRANSACTION_ERROR_MAX])
{
  struct vg_ota_sha256 sha;
  uint8_t digest[32];
  int ret = -EINVAL;

  if (in == NULL || record == NULL || candidate_len == NULL ||
      target_len == NULL || error_code == NULL || in[8] != TRANSACTION_VERSION)
    {
      return -EINVAL;
    }
  vg_ota_sha256_init(&sha);
  vg_ota_sha256_update(&sha, in, TRANSACTION_BODY_SIZE);
  vg_ota_sha256_final(&sha, digest);
  if (memcmp(digest, in + TRANSACTION_BODY_SIZE, sizeof(digest)) != 0)
    {
      goto done;
    }
  if ((in[10] & ~(TRANSACTION_FLAG_HAS_BASE |
                  TRANSACTION_FLAG_HAS_APPLIED)) != 0)
    {
      goto done;
    }

  memset(record, 0, sizeof(*record));
  record->revision = get_u32(in + 12);
  record->has_base_hash = (in[10] & TRANSACTION_FLAG_HAS_BASE) != 0;
  if (load_fixed(in + 16, 32, record->origin_boot_id,
                 sizeof(record->origin_boot_id), false) != 0 ||
      load_fixed(in + 48, 64, record->task_id,
                 sizeof(record->task_id), false) != 0 ||
      load_fixed(in + 112, 32, record->session_id,
                 sizeof(record->session_id), false) != 0 ||
      load_fixed(in + 144, 64, record->base_hash,
                 sizeof(record->base_hash), true) != 0 ||
      load_fixed(in + 208, 64, record->payload_hash,
                 sizeof(record->payload_hash), false) != 0 ||
      load_fixed(in + 272, 64, record->source_hash,
                 sizeof(record->source_hash), false) != 0 ||
      load_fixed(in + 336, 64, record->candidate_hash,
                 sizeof(record->candidate_hash), false) != 0 ||
      load_fixed(in + 400, 32, record->confirmation_id,
                 sizeof(record->confirmation_id), false) != 0 ||
      load_fixed(in + 440, 31, error_code,
                 VG_CONFIG_TRANSACTION_ERROR_MAX, true) != 0)
    {
      goto done;
    }
  if (record->has_base_hash != (record->base_hash[0] != '\0') ||
      validate_record(record) != 0)
    {
      goto done;
    }

  *candidate_len = get_u32(in + 432);
  *target_len = get_u32(in + 436);
  if (*candidate_len == 0 || *candidate_len > VG_CONFIG_REQUEST_MAX ||
      *target_len == 0 || *target_len >= VG_POINT_TABLE_JSON_MAX)
    {
      goto done;
    }
  ret = 0;

 done:
  memset(digest, 0, sizeof(digest));
  memset(&sha, 0, sizeof(sha));
  return ret;
}

static int write_and_verify(struct vg_config_transaction *transaction,
                            const char *path, const void *data, size_t length)
{
  uint8_t *copy;
  size_t got = 0;
  int ret;

  ret = transaction->ports.write_file(transaction->ports.ctx, path,
                                      data, length);
  if (ret != 0)
    {
      return ret;
    }

  copy = malloc(length == 0 ? 1 : length + 1);
  if (copy == NULL)
    {
      return -ENOMEM;
    }
  ret = transaction->ports.read_file(transaction->ports.ctx, path,
                                     copy, length + 1, &got);
  if (ret != 0 || got != length || memcmp(copy, data, length) != 0)
    {
      ret = ret != 0 ? ret : -EIO;
    }
  free(copy);
  return ret;
}

static int read_file_limited(struct vg_config_transaction *transaction,
                             const char *path, void *data, size_t capacity,
                             size_t *length)
{
  return transaction->ports.read_file(transaction->ports.ctx, path,
                                      data, capacity, length);
}

static int path_exists(struct vg_config_transaction *transaction,
                       const char *path, bool *exists)
{
  uint8_t byte;
  size_t length;
  int ret;

  ret = read_file_limited(transaction, path, &byte, sizeof(byte), &length);
  if (ret == -ENOENT)
    {
      *exists = false;
      return 0;
    }
  if (ret == 0 || ret == -EFBIG)
    {
      *exists = true;
      return 0;
    }
  return ret;
}

static int verify_prepared_files(
  struct vg_config_transaction *transaction,
  uint32_t candidate_len, uint32_t target_len)
{
  uint8_t *buffer;
  char digest[65];
  size_t length;
  int ret;

  buffer = malloc(VG_CONFIG_REQUEST_MAX + 1);
  if (buffer == NULL)
    {
      return -ENOMEM;
    }

  ret = read_file_limited(transaction, transaction->candidate_path,
                          buffer, VG_CONFIG_REQUEST_MAX + 1, &length);
  if (ret != 0 || length != candidate_len)
    {
      ret = ret != 0 ? ret : -EIO;
      goto done;
    }
  hash_hex(buffer, length, digest);
  if (strcmp(digest, transaction->record.payload_hash) != 0)
    {
      ret = -EIO;
      goto done;
    }

  ret = read_file_limited(transaction, transaction->source_path,
                          buffer, VG_CONFIG_REQUEST_MAX + 1, &length);
  if (ret != 0)
    {
      goto done;
    }
  hash_hex(buffer, length, digest);
  if (strcmp(digest, transaction->record.source_hash) != 0)
    {
      ret = -EIO;
      goto done;
    }

  ret = read_file_limited(transaction, transaction->target_path,
                          buffer, VG_POINT_TABLE_JSON_MAX, &length);
  if (ret != 0 || length != target_len)
    {
      ret = ret != 0 ? ret : -EIO;
      goto done;
    }
  hash_hex(buffer, length, digest);
  if (strcmp(digest, transaction->record.candidate_hash) != 0)
    {
      ret = -EIO;
      goto done;
    }

  ret = 0;

 done:
  memset(digest, 0, sizeof(digest));
  free(buffer);
  return ret;
}

static bool table_matches(bool present, const char *hash,
                          bool expected_present, const char *expected)
{
  return present == expected_present &&
         (!present || strcmp(hash, expected) == 0);
}

static bool records_equal(
  const struct vg_config_transaction_record *left,
  const struct vg_config_transaction_record *right)
{
  return strcmp(left->origin_boot_id, right->origin_boot_id) == 0 &&
         strcmp(left->task_id, right->task_id) == 0 &&
         strcmp(left->session_id, right->session_id) == 0 &&
         left->revision == right->revision &&
         left->has_base_hash == right->has_base_hash &&
         (!left->has_base_hash ||
          strcmp(left->base_hash, right->base_hash) == 0) &&
         strcmp(left->payload_hash, right->payload_hash) == 0 &&
         strcmp(left->source_hash, right->source_hash) == 0 &&
         strcmp(left->candidate_hash, right->candidate_hash) == 0 &&
         strcmp(left->confirmation_id, right->confirmation_id) == 0;
}

static int persist_receipt(struct vg_config_transaction *transaction,
                           struct vg_config_receipt *receipt)
{
  uint8_t blob[512];
  uint8_t flags = 0;
  int ret;

  if (receipt->record.has_base_hash)
    {
      flags |= TRANSACTION_FLAG_HAS_BASE;
    }
  if (receipt->has_applied_hash)
    {
      flags |= TRANSACTION_FLAG_HAS_APPLIED;
    }
  ret = encode_blob(receipt_magic, (uint8_t)receipt->outcome, flags,
                    &receipt->record, 1, 1, receipt->error_code, blob);
  if (ret == 0)
    {
      ret = write_and_verify(transaction, transaction->receipt_path,
                             blob, sizeof(blob));
    }
  memset(blob, 0, sizeof(blob));
  return ret;
}

int vg_config_transaction_init(
  struct vg_config_transaction *transaction,
  const char *root, const char *points_path,
  const struct vg_config_transaction_ports *ports)
{
  struct vg_config_transaction_ports defaults;
  int ret;

  if (transaction == NULL || root == NULL || points_path == NULL)
    {
      return -EINVAL;
    }

  memset(transaction, 0, sizeof(*transaction));
  ret = copy_path(transaction->root, sizeof(transaction->root), root);
  if (ret == 0)
    {
      ret = copy_path(transaction->points_path,
                      sizeof(transaction->points_path), points_path);
    }
  if (ret == 0)
    {
      ret = join_path(transaction->candidate_path,
                      sizeof(transaction->candidate_path), root,
                      "candidate.json");
    }
  if (ret == 0)
    {
      ret = join_path(transaction->source_path,
                      sizeof(transaction->source_path), root,
                      "source.json");
    }
  if (ret == 0)
    {
      ret = join_path(transaction->target_path,
                      sizeof(transaction->target_path), root,
                      "target.json");
    }
  if (ret == 0)
    {
      ret = join_path(transaction->journal_path,
                      sizeof(transaction->journal_path), root,
                      "journal.json");
    }
  if (ret == 0)
    {
      ret = join_path(transaction->receipt_path,
                      sizeof(transaction->receipt_path), root,
                      "receipt.json");
    }
  if (ret != 0)
    {
      return ret;
    }

  if (ports != NULL)
    {
      if (ports->write_file == NULL || ports->read_file == NULL ||
          ports->current_table_hash == NULL || ports->commit_table == NULL)
        {
          return -EINVAL;
        }
      transaction->ports = *ports;
    }
  else
    {
      defaults.write_file = file_write_default;
      defaults.read_file = file_read_default;
      defaults.current_table_hash = table_hash_default;
      defaults.commit_table = commit_table_default;
      defaults.ctx = transaction;
      transaction->ports = defaults;
    }

  if (mkdir(root, 0755) != 0 && errno != EEXIST)
    {
      return -errno;
    }
  if (pthread_mutex_init(&transaction->lock, NULL) != 0)
    {
      return -EIO;
    }
  transaction->initialized = true;
  return 0;
}

int vg_config_transaction_prepare(
  struct vg_config_transaction *transaction,
  const void *candidate_json, size_t candidate_length,
  const void *source_json, size_t source_length,
  const void *target_json, size_t target_length,
  const struct vg_config_transaction_record *record)
{
  uint8_t journal[512];
  bool present;
  char current_hash[65];
  char digest[65];
  int ret;

  if (transaction == NULL || !transaction->initialized ||
      candidate_json == NULL || candidate_length == 0 ||
      candidate_length > VG_CONFIG_REQUEST_MAX ||
      source_json == NULL || source_length == 0 ||
      source_length > VG_CONFIG_REQUEST_MAX ||
      target_json == NULL || target_length == 0 ||
      target_length >= VG_POINT_TABLE_JSON_MAX ||
      validate_record(record) != 0)
    {
      return -EINVAL;
    }

  hash_hex(candidate_json, candidate_length, digest);
  if (strcmp(digest, record->payload_hash) != 0)
    {
      return -EINVAL;
    }
  hash_hex(source_json, source_length, digest);
  if (strcmp(digest, record->source_hash) != 0)
    {
      return -EINVAL;
    }
  hash_hex(target_json, target_length, digest);
  if (strcmp(digest, record->candidate_hash) != 0)
    {
      return -EINVAL;
    }

  ret = encode_blob(journal_magic, TRANSACTION_KIND_JOURNAL,
                    record->has_base_hash ?
                    TRANSACTION_FLAG_HAS_BASE : 0,
                    record, (uint32_t)candidate_length,
                    (uint32_t)target_length, "", journal);
  if (ret != 0)
    {
      goto done;
    }

  pthread_mutex_lock(&transaction->lock);
  transaction->prepared = false;
  if (transaction->recovery_blocked)
    {
      ret = -EACCES;
      goto unlock;
    }

  ret = transaction->ports.current_table_hash(
    transaction->ports.ctx, &present, current_hash);
  if (ret != 0)
    {
      goto unlock;
    }
  if (!table_matches(present, current_hash, record->has_base_hash,
                     record->base_hash))
    {
      ret = -ESTALE;
      goto unlock;
    }

  ret = write_and_verify(transaction, transaction->candidate_path,
                         candidate_json, candidate_length);
  if (ret == 0)
    {
      ret = write_and_verify(transaction, transaction->source_path,
                             source_json, source_length);
    }
  if (ret == 0)
    {
      ret = write_and_verify(transaction, transaction->target_path,
                             target_json, target_length);
    }
  if (ret == 0)
    {
      ret = write_and_verify(transaction, transaction->journal_path,
                             journal, sizeof(journal));
    }
  if (ret == 0)
    {
      transaction->record = *record;
      transaction->prepared = true;
    }

 unlock:
  pthread_mutex_unlock(&transaction->lock);

 done:
  memset(journal, 0, sizeof(journal));
  memset(current_hash, 0, sizeof(current_hash));
  memset(digest, 0, sizeof(digest));
  return ret;
}

int vg_config_transaction_commit(
  struct vg_config_transaction *transaction,
  struct vg_config_receipt *receipt)
{
  uint8_t journal[512];
  uint8_t *target = NULL;
  char current_hash[65] = "";
  char error_code[VG_CONFIG_TRANSACTION_ERROR_MAX] = "";
  uint32_t candidate_len;
  uint32_t target_len;
  size_t length;
  bool present;
  int commit_ret;
  int ret;

  if (transaction == NULL || receipt == NULL ||
      !transaction->initialized)
    {
      return -EINVAL;
    }

  pthread_mutex_lock(&transaction->lock);
  if (!transaction->prepared)
    {
      ret = -EINVAL;
      goto unlock;
    }

  ret = read_file_limited(transaction, transaction->journal_path,
                          journal, sizeof(journal), &length);
  if (ret != 0 || length != sizeof(journal) ||
      memcmp(journal, journal_magic, sizeof(journal_magic)) != 0 ||
      journal[9] != TRANSACTION_KIND_JOURNAL)
    {
      ret = ret != 0 ? ret : -EIO;
      strcpy(error_code, "journal_invalid");
      goto unknown;
    }
  {
    struct vg_config_transaction_record decoded;
    if (decode_record(journal, &decoded, &candidate_len, &target_len,
                      error_code) != 0 ||
        !records_equal(&decoded, &transaction->record))
      {
        ret = -EIO;
        strcpy(error_code, "journal_invalid");
        goto unknown;
      }
  }

  ret = verify_prepared_files(transaction, candidate_len, target_len);
  if (ret != 0)
    {
      strcpy(error_code, "prepared_invalid");
      goto unknown;
    }

  target = malloc(target_len);
  if (target == NULL)
    {
      ret = -ENOMEM;
      strcpy(error_code, "validation_resource");
      goto unknown;
    }
  ret = read_file_limited(transaction, transaction->target_path,
                          target, target_len, &length);
  if (ret != 0 || length != target_len)
    {
      ret = ret != 0 ? ret : -EIO;
      strcpy(error_code, "target_unreadable");
      goto unknown;
    }

  commit_ret = transaction->ports.commit_table(
    transaction->ports.ctx, target, target_len);
  ret = transaction->ports.current_table_hash(
    transaction->ports.ctx, &present, current_hash);
  if (ret != 0)
    {
      error_code[0] = '\0';
      strcpy(error_code, "table_unreadable");
      goto unknown;
    }

  memset(receipt, 0, sizeof(*receipt));
  receipt->record = transaction->record;
  if (table_matches(present, current_hash, true,
                    transaction->record.candidate_hash))
    {
      receipt->outcome = VG_CONFIG_RECEIPT_APPLIED;
      receipt->has_applied_hash = true;
      strcpy(receipt->applied_hash, transaction->record.candidate_hash);
      ret = 0;
    }
  else if (commit_ret != 0 &&
           table_matches(present, current_hash,
                         transaction->record.has_base_hash,
                         transaction->record.base_hash))
    {
      receipt->outcome = VG_CONFIG_RECEIPT_FAILED;
      strcpy(receipt->error_code, "commit_failed");
      ret = commit_ret;
    }
  else
    {
      receipt->outcome = VG_CONFIG_RECEIPT_RECOVERY_UNKNOWN;
      strcpy(receipt->error_code, "outcome_unknown");
      ret = -EIO;
    }

  if (persist_receipt(transaction, receipt) != 0)
    {
      transaction->recovery_blocked = true;
      ret = -EIO;
    }
  transaction->prepared = false;
  transaction->recovery_blocked =
    receipt->outcome == VG_CONFIG_RECEIPT_RECOVERY_UNKNOWN ||
    transaction->recovery_blocked;
  goto unlock;

 unknown:
  memset(receipt, 0, sizeof(*receipt));
  receipt->record = transaction->record;
  receipt->outcome = VG_CONFIG_RECEIPT_RECOVERY_UNKNOWN;
  strcpy(receipt->error_code,
         error_code[0] != '\0' ? error_code : "record_invalid");
  if (persist_receipt(transaction, receipt) != 0)
    {
      ret = -EIO;
    }
  transaction->prepared = false;
  transaction->recovery_blocked = true;
  ret = -EIO;

 unlock:
  pthread_mutex_unlock(&transaction->lock);
  memset(journal, 0, sizeof(journal));
  memset(current_hash, 0, sizeof(current_hash));
  memset(error_code, 0, sizeof(error_code));
  if (target != NULL)
    {
      memset(target, 0, target_len);
      free(target);
    }
  return ret;
}

int vg_config_transaction_recover(
  struct vg_config_transaction *transaction,
  struct vg_config_receipt *receipt)
{
  uint8_t journal[512];
  char current_hash[65] = "";
  char error_code[VG_CONFIG_TRANSACTION_ERROR_MAX];
  uint32_t candidate_len;
  uint32_t target_len;
  size_t length;
  bool present;
  int ret;

  if (transaction == NULL || receipt == NULL ||
      !transaction->initialized)
    {
      return -EINVAL;
    }

  pthread_mutex_lock(&transaction->lock);
  ret = read_file_limited(transaction, transaction->journal_path,
                          journal, sizeof(journal), &length);
  if (ret == -ENOENT)
    {
      bool candidate_exists;
      bool source_exists;
      bool target_exists;

      ret = path_exists(transaction, transaction->candidate_path,
                        &candidate_exists);
      if (ret == 0)
        {
          ret = path_exists(transaction, transaction->source_path,
                            &source_exists);
        }
      if (ret == 0)
        {
          ret = path_exists(transaction, transaction->target_path,
                            &target_exists);
        }
      if (ret != 0)
        {
          transaction->recovery_blocked = true;
          pthread_mutex_unlock(&transaction->lock);
          return -EIO;
        }
      if (candidate_exists || source_exists || target_exists)
        {
          transaction->recovery_blocked = true;
          pthread_mutex_unlock(&transaction->lock);
          return -EIO;
        }
      pthread_mutex_unlock(&transaction->lock);
      return -ENOENT;
    }
  if (ret != 0 || length != sizeof(journal) ||
      memcmp(journal, journal_magic, sizeof(journal_magic)) != 0 ||
      journal[9] != TRANSACTION_KIND_JOURNAL)
    {
      transaction->recovery_blocked = true;
      pthread_mutex_unlock(&transaction->lock);
      return -EIO;
    }

  memset(&transaction->record, 0, sizeof(transaction->record));
  if (decode_record(journal, &transaction->record,
                    &candidate_len, &target_len, error_code) != 0)
    {
      transaction->recovery_blocked = true;
      pthread_mutex_unlock(&transaction->lock);
      return -EIO;
    }

  ret = verify_prepared_files(transaction, candidate_len, target_len);
  if (ret != 0)
    {
      goto unknown;
    }

  ret = transaction->ports.current_table_hash(
    transaction->ports.ctx, &present, current_hash);
  if (ret != 0)
    {
      goto unknown;
    }

  memset(receipt, 0, sizeof(*receipt));
  receipt->record = transaction->record;
  if (table_matches(present, current_hash, true,
                    transaction->record.candidate_hash))
    {
      receipt->outcome = VG_CONFIG_RECEIPT_APPLIED;
      receipt->has_applied_hash = true;
      strcpy(receipt->applied_hash, transaction->record.candidate_hash);
      ret = 0;
    }
  else if (table_matches(present, current_hash,
                         transaction->record.has_base_hash,
                         transaction->record.base_hash))
    {
      receipt->outcome = VG_CONFIG_RECEIPT_FAILED;
      strcpy(receipt->error_code, "not_applied");
      ret = 0;
    }
  else
    {
      receipt->outcome = VG_CONFIG_RECEIPT_RECOVERY_UNKNOWN;
      strcpy(receipt->error_code, "outcome_unknown");
      ret = -EIO;
    }

  if (persist_receipt(transaction, receipt) != 0)
    {
      transaction->recovery_blocked = true;
      ret = -EIO;
    }
  transaction->prepared = false;
  transaction->recovery_blocked =
    receipt->outcome == VG_CONFIG_RECEIPT_RECOVERY_UNKNOWN ||
    transaction->recovery_blocked;
  pthread_mutex_unlock(&transaction->lock);
  memset(journal, 0, sizeof(journal));
  memset(current_hash, 0, sizeof(current_hash));
  memset(error_code, 0, sizeof(error_code));
  return ret;

 unknown:
  memset(receipt, 0, sizeof(*receipt));
  receipt->record = transaction->record;
  receipt->outcome = VG_CONFIG_RECEIPT_RECOVERY_UNKNOWN;
  strcpy(receipt->error_code, "record_invalid");
  transaction->recovery_blocked = true;
  transaction->prepared = false;
  (void)persist_receipt(transaction, receipt);
  pthread_mutex_unlock(&transaction->lock);
  memset(journal, 0, sizeof(journal));
  memset(current_hash, 0, sizeof(current_hash));
  memset(error_code, 0, sizeof(error_code));
  return -EIO;
}

int vg_config_transaction_read_receipt(
  struct vg_config_transaction *transaction,
  struct vg_config_receipt *receipt)
{
  uint8_t blob[512];
  char error_code[VG_CONFIG_TRANSACTION_ERROR_MAX];
  uint32_t candidate_len;
  uint32_t target_len;
  size_t length;
  int ret;

  if (transaction == NULL || receipt == NULL ||
      !transaction->initialized)
    {
      return -EINVAL;
    }

  pthread_mutex_lock(&transaction->lock);
  ret = read_file_limited(transaction, transaction->receipt_path,
                          blob, sizeof(blob), &length);
  if (ret != 0 || length != sizeof(blob) ||
      memcmp(blob, receipt_magic, sizeof(receipt_magic)) != 0)
    {
      ret = ret != 0 ? ret : -EIO;
      goto done;
    }

  memset(receipt, 0, sizeof(*receipt));
  ret = decode_record(blob, &receipt->record, &candidate_len,
                      &target_len, error_code);
  if (ret != 0)
    {
      goto done;
    }
  receipt->outcome = (enum vg_config_receipt_outcome_e)blob[9];
  receipt->has_applied_hash =
    (blob[10] & TRANSACTION_FLAG_HAS_APPLIED) != 0;
  if (receipt->outcome < VG_CONFIG_RECEIPT_APPLIED ||
      receipt->outcome > VG_CONFIG_RECEIPT_RECOVERY_UNKNOWN ||
      (receipt->has_applied_hash &&
       receipt->outcome != VG_CONFIG_RECEIPT_APPLIED))
    {
      ret = -EIO;
      goto done;
    }
  if (receipt->has_applied_hash)
    {
      strcpy(receipt->applied_hash, receipt->record.candidate_hash);
    }
  strcpy(receipt->error_code, error_code);

 done:
  memset(blob, 0, sizeof(blob));
  memset(error_code, 0, sizeof(error_code));
  pthread_mutex_unlock(&transaction->lock);
  return ret;
}

bool vg_config_transaction_recovery_blocked(
  struct vg_config_transaction *transaction)
{
  bool blocked;

  if (transaction == NULL || !transaction->initialized)
    {
      return true;
    }
  pthread_mutex_lock(&transaction->lock);
  blocked = transaction->recovery_blocked;
  pthread_mutex_unlock(&transaction->lock);
  return blocked;
}
