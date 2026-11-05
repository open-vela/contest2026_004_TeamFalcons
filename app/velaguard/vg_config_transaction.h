/****************************************************************************
 * app/velaguard/vg_config_transaction.h
 *
 * Durable prepare, commit and recovery support for remote point tables.
 ****************************************************************************/

#ifndef __APP_VELAGUARD_VG_CONFIG_TRANSACTION_H
#define __APP_VELAGUARD_VG_CONFIG_TRANSACTION_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "vg_config_service.h"

#define VG_CONFIG_TRANSACTION_PATH_MAX 192
#define VG_CONFIG_TRANSACTION_ERROR_MAX 32

enum vg_config_receipt_outcome_e
{
  VG_CONFIG_RECEIPT_NONE = 0,
  VG_CONFIG_RECEIPT_APPLIED,
  VG_CONFIG_RECEIPT_FAILED,
  VG_CONFIG_RECEIPT_RECOVERY_UNKNOWN
};

struct vg_config_transaction_record
{
  char origin_boot_id[VG_CONFIG_HEX32_LEN];
  char task_id[VG_CONFIG_ID_MAX];
  char session_id[VG_CONFIG_HEX32_LEN];
  uint32_t revision;
  bool has_base_hash;
  char base_hash[VG_CONFIG_HASH_LEN];
  char payload_hash[VG_CONFIG_HASH_LEN];
  char source_hash[VG_CONFIG_HASH_LEN];
  char candidate_hash[VG_CONFIG_HASH_LEN];
  char confirmation_id[VG_CONFIG_HEX32_LEN];
};

struct vg_config_receipt
{
  struct vg_config_transaction_record record;
  enum vg_config_receipt_outcome_e outcome;
  bool has_applied_hash;
  char applied_hash[VG_CONFIG_HASH_LEN];
  char error_code[VG_CONFIG_TRANSACTION_ERROR_MAX];
};

struct vg_config_transaction_ports
{
  int (*write_file)(void *ctx, const char *path,
                    const void *data, size_t length);
  int (*read_file)(void *ctx, const char *path,
                   void *data, size_t capacity, size_t *length);
  int (*current_table_hash)(void *ctx, bool *present, char hash[65]);
  int (*commit_table)(void *ctx, const void *json, size_t length);
  void *ctx;
};

struct vg_config_transaction
{
  pthread_mutex_t lock;
  struct vg_config_transaction_ports ports;
  char root[VG_CONFIG_TRANSACTION_PATH_MAX];
  char points_path[VG_CONFIG_TRANSACTION_PATH_MAX];
  char candidate_path[VG_CONFIG_TRANSACTION_PATH_MAX];
  char source_path[VG_CONFIG_TRANSACTION_PATH_MAX];
  char target_path[VG_CONFIG_TRANSACTION_PATH_MAX];
  char journal_path[VG_CONFIG_TRANSACTION_PATH_MAX];
  char receipt_path[VG_CONFIG_TRANSACTION_PATH_MAX];
  struct vg_config_transaction_record record;
  bool prepared;
  bool recovery_blocked;
  bool initialized;
};

int vg_config_transaction_init(
  struct vg_config_transaction *transaction,
  const char *root, const char *points_path,
  const struct vg_config_transaction_ports *ports);
int vg_config_transaction_prepare(
  struct vg_config_transaction *transaction,
  const void *candidate_json, size_t candidate_length,
  const void *source_json, size_t source_length,
  const void *target_json, size_t target_length,
  const struct vg_config_transaction_record *record);
int vg_config_transaction_commit(
  struct vg_config_transaction *transaction,
  struct vg_config_receipt *receipt);
int vg_config_transaction_recover(
  struct vg_config_transaction *transaction,
  struct vg_config_receipt *receipt);
int vg_config_transaction_read_receipt(
  struct vg_config_transaction *transaction,
  struct vg_config_receipt *receipt);
bool vg_config_transaction_recovery_blocked(
  struct vg_config_transaction *transaction);

#endif
