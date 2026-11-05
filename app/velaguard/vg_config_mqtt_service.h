/****************************************************************************
 * app/velaguard/vg_config_mqtt_service.h
 *
 * Background controller for the dedicated candidate MQTT request channel.
 ****************************************************************************/

#ifndef __APP_VELAGUARD_VG_CONFIG_MQTT_SERVICE_H
#define __APP_VELAGUARD_VG_CONFIG_MQTT_SERVICE_H

#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>

#include "vg_config_mqtt_protocol.h"
#include "vg_config_mqtt_transport.h"

struct vg_config_mqtt_service_ports
{
  int (*open_connection)(void *ctx, const char *device_id);
  int (*poll_connection)(void *ctx);
  bool (*online)(void *ctx);
  void (*close_connection)(void *ctx);
  struct vg_config_mqtt_message *(*peek)(void *ctx);
  void (*release)(void *ctx);
  int (*publish)(void *ctx, const char *json, uint16_t *ticket);
  int (*acked)(void *ctx, uint16_t ticket);
  bool (*ready)(void *ctx);
  uint64_t (*now_ms)(void *ctx);
  void (*delay_ms)(void *ctx, uint32_t delay);
  void *ctx;
};

struct vg_config_mqtt_service_status
{
  bool running;
  bool online;
  char error[64];
};

struct vg_config_mqtt_service
{
  pthread_mutex_t lock;
  pthread_cond_t changed;
  pthread_t thread;
  struct vg_config_mqtt_service_ports ports;
  struct vg_config_service *config;
  struct vg_config_mqtt_request request;
  struct vg_config_task_status status;
  char device_id[VG_DEVICE_ID_MAX];
  char active_task_id[VG_CONFIG_ID_MAX];
  char error[64];
  uint64_t ticket_since;
  uint32_t last_state_seq;
  uint16_t pending_ticket;
  bool initialized;
  bool running;
  bool stopping;
  bool online;
  bool have_active_task;
  bool have_last_state;
};

int vg_config_mqtt_service_init(
  struct vg_config_mqtt_service *service,
  struct vg_config_service *config,
  const char *device_id,
  const struct vg_config_mqtt_service_ports *ports);
int vg_config_mqtt_service_pump(
  struct vg_config_mqtt_service *service);
int vg_config_mqtt_service_start(
  struct vg_config_mqtt_service *service);
int vg_config_mqtt_service_stop(
  struct vg_config_mqtt_service *service);
int vg_config_mqtt_service_status(
  struct vg_config_mqtt_service *service,
  struct vg_config_mqtt_service_status *out);

#endif
