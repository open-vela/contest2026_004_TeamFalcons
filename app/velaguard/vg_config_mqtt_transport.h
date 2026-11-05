/****************************************************************************
 * app/velaguard/vg_config_mqtt_transport.h
 *
 * Dedicated MQTT 3.1.1 over verified TLS transport for candidate control.
 ****************************************************************************/

#ifndef __APP_VELAGUARD_VG_CONFIG_MQTT_TRANSPORT_H
#define __APP_VELAGUARD_VG_CONFIG_MQTT_TRANSPORT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <mqtt.h>

#include "vg_config_mqtt_protocol.h"
#include "vg_ota_https.h"

#define VG_CONFIG_MQTT_HANDLE 0x20000002
#define VG_CONFIG_MQTT_TX_MAX 4096
#define VG_CONFIG_MQTT_RX_MAX (VG_CONFIG_MQTT_REQUEST_MAX + 1024)

struct vg_config_mqtt_config
{
  char broker[96];
  char port[6];
  char username[64];
  char password[128];
};

struct vg_config_mqtt_message
{
  bool used;
  char topic[VG_CONFIG_MQTT_TOPIC_MAX];
  size_t length;
  int retain;
  int qos;
  char payload[VG_CONFIG_MQTT_REQUEST_MAX + 1];
};

struct vg_config_mqtt_transport
{
  struct mqtt_client client;
  struct vg_ota_https tls;
  uint8_t tx[VG_CONFIG_MQTT_TX_MAX];
  uint8_t rx[VG_CONFIG_MQTT_RX_MAX];
  char device_id[VG_DEVICE_ID_MAX];
  struct vg_config_mqtt_message message;
  bool initialized;
  bool online;
};

int vg_config_mqtt_transport_open(
  struct vg_config_mqtt_transport *transport,
  const struct vg_config_mqtt_config *config,
  const char *device_id,
  const char *ca_pem);
int vg_config_mqtt_transport_poll(
  struct vg_config_mqtt_transport *transport);
int vg_config_mqtt_transport_take(
  struct vg_config_mqtt_transport *transport,
  struct vg_config_mqtt_message *out);
const struct vg_config_mqtt_message *vg_config_mqtt_transport_peek(
  struct vg_config_mqtt_transport *transport);
void vg_config_mqtt_transport_release(
  struct vg_config_mqtt_transport *transport);
int vg_config_mqtt_transport_publish_state(
  struct vg_config_mqtt_transport *transport,
  const char *json,
  uint16_t *ticket);
int vg_config_mqtt_transport_acked(
  struct vg_config_mqtt_transport *transport,
  uint16_t ticket);
bool vg_config_mqtt_transport_online(
  const struct vg_config_mqtt_transport *transport);
void vg_config_mqtt_transport_close(
  struct vg_config_mqtt_transport *transport);

int vg_config_mqtt_pal_send(int fd, const void *bytes, size_t length,
                            ssize_t *out);
int vg_config_mqtt_pal_recv(int fd, void *bytes, size_t length,
                            ssize_t *out);

#endif
