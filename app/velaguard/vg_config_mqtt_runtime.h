/****************************************************************************
 * app/velaguard/vg_config_mqtt_runtime.h
 *
 * Product runtime ownership for the candidate MQTT controller and local
 * confirmation worker.
 ****************************************************************************/

#ifndef __APP_VELAGUARD_VG_CONFIG_MQTT_RUNTIME_H
#define __APP_VELAGUARD_VG_CONFIG_MQTT_RUNTIME_H

#include <stdbool.h>
#include <stdio.h>

#include "vg_config_confirmation.h"
#include "vg_config_service.h"

struct vg_config_mqtt_runtime_status
{
  bool initialized;
  bool running;
  bool online;
  char error[64];
};

int vg_config_mqtt_runtime_start(void);
int vg_config_mqtt_runtime_status(
  struct vg_config_mqtt_runtime_status *out);
void vg_config_mqtt_runtime_print(FILE *out);
struct vg_config_service *vg_config_mqtt_runtime_service(void);
int vg_config_mqtt_runtime_bind_confirmation(
  struct vg_config_confirmation *confirmation,
  bool (*alarm_active)(void *ctx),
  void *alarm_ctx);

#endif
