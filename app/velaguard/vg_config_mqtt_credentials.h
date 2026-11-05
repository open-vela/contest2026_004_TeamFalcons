/****************************************************************************
 * app/velaguard/vg_config_mqtt_credentials.h
 *
 * Device-bound credential loading for the dedicated candidate MQTT session.
 ****************************************************************************/

#ifndef __APP_VELAGUARD_VG_CONFIG_MQTT_CREDENTIALS_H
#define __APP_VELAGUARD_VG_CONFIG_MQTT_CREDENTIALS_H

#include <stddef.h>

#include "vg_config_mqtt_transport.h"

#define VG_CONFIG_MQTT_CREDENTIALS_JSON_MAX 512
#define VG_CONFIG_MQTT_CREDENTIALS_BLOB_MAX \
  (VG_CONFIG_MQTT_CREDENTIALS_JSON_MAX + 60)
#define VG_CONFIG_MQTT_CREDENTIALS_FILE \
  "/data/velaguard/provision/config_mqtt_secrets.v1"
#define VG_CONFIG_MQTT_CREDENTIALS_STAGING \
  "/data/velaguard/provision/.config_mqtt_staging"

int vg_config_mqtt_credentials_parse(
  const void *json,
  size_t length,
  struct vg_config_mqtt_config *out);
int vg_config_mqtt_credentials_load(
  struct vg_config_mqtt_config *out);
int vg_config_mqtt_credentials_import(
  const void *json,
  size_t length);
int vg_config_mqtt_credentials_install_blob(
  const void *blob,
  size_t length);
int vg_config_mqtt_credentials_wipe(void);

#endif
