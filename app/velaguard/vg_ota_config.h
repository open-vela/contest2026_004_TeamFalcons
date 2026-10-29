/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __VG_OTA_CONFIG_H
#define __VG_OTA_CONFIG_H
#include <stddef.h>
#include <stdint.h>
#define VG_OTA_CONFIG_JSON_MAX 1024
#define VG_OTA_CONFIG_BLOB_MAX (VG_OTA_CONFIG_JSON_MAX+52)
#define VG_OTA_CONFIG_FILE "/data/velaguard/provision/ota_secrets.v2"
#define VG_OTA_CONFIG_IMPORT "/data/velaguard/provision/ota_import.v2"
struct vg_ota_config {
  char broker[96];
  char port[6];
  char username[64];
  char password[128];
  char allowed_hosts[2][102];
  unsigned allowed_count;
};
int vg_ota_config_parse(const void *json,size_t length,struct vg_ota_config *out);
int vg_ota_config_seal(const uint8_t uid[12],const uint8_t nonce[12],const void *json,size_t length,
                       uint8_t *blob,size_t capacity,size_t *written);
int vg_ota_config_open(const uint8_t uid[12],const uint8_t *blob,size_t length,struct vg_ota_config *out);
int vg_ota_config_load(struct vg_ota_config *out);
int vg_ota_config_import(const uint8_t *blob,size_t length);
#endif
