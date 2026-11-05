/****************************************************************************
 * app/velaguard/vg_config_candidate.h
 *
 * Strict candidate JSON parsing, deterministic validation and table merge.
 ****************************************************************************/

#ifndef __APP_VELAGUARD_VG_CONFIG_CANDIDATE_H
#define __APP_VELAGUARD_VG_CONFIG_CANDIDATE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "vg_config_contract.h"

#define VG_CONFIG_MODEL_MAX 65
#define VG_CONFIG_QUOTE_MAX 161
#define VG_CONFIG_ORIGINAL_ADDR_MAX 33
#define VG_CONFIG_EVIDENCE_MAX 32
#define VG_CONFIG_EVIDENCE_ID_MAX 33
#define VG_CONFIG_CANDIDATE_POINT_MAX 32
#define VG_CONFIG_EVIDENCE_REF_MAX 2

struct vg_config_evidence
{
  char id[VG_CONFIG_EVIDENCE_ID_MAX];
  uint32_t page;
  char quote[VG_CONFIG_QUOTE_MAX];
};

struct vg_config_candidate_point
{
  char id[VG_POINT_ID_MAX];
  char name[VG_POINT_NAME_MAX];
  uint8_t addr;
  uint8_t fc;
  uint16_t reg;
  char dtype[16];
  float scale;
  char unit[8];
  char original_address[VG_CONFIG_ORIGINAL_ADDR_MAX];
  uint32_t address_base;
  char evidence_ids[VG_CONFIG_EVIDENCE_REF_MAX][VG_CONFIG_EVIDENCE_ID_MAX];
  uint8_t evidence_count;
};

struct vg_config_candidate
{
  uint32_t schema_version;
  char mode[16];
  uint16_t baud;
  uint8_t slave_address;
  char model[VG_CONFIG_MODEL_MAX];
  char manual_id[VG_CONFIG_ID_MAX];
  char manual_sha256[VG_CONFIG_HASH_LEN];
  struct vg_config_evidence evidence[VG_CONFIG_EVIDENCE_MAX];
  uint32_t evidence_count;
  struct vg_config_candidate_point points[VG_CONFIG_CANDIDATE_POINT_MAX];
  uint32_t point_count;
};

struct vg_config_candidate_build
{
  struct vg_config_candidate candidate;
  struct vg_discover_summary merged;
  char target_json[VG_CONFIG_REQUEST_MAX];
  size_t target_len;
  char candidate_hash[VG_CONFIG_HASH_LEN];
};

int vg_config_json_strict_validate(const void *json, size_t length);
int vg_config_candidate_parse_json(
  const void *json,
  size_t length,
  struct vg_config_candidate *out);
int vg_config_candidate_build_json(
  const void *json, size_t length,
  const struct vg_discover_summary *current,
  struct vg_config_candidate_build *out);

#endif
