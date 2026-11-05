/****************************************************************************
 * app/velaguard/vg_config_mqtt_transport.c
 ****************************************************************************/

#ifdef __NuttX__
#  include <nuttx/config.h>
#endif

#include "vg_config_mqtt_transport.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static struct vg_config_mqtt_transport *g_active;

int vg_config_mqtt_pal_send(int fd, const void *bytes, size_t length,
                            ssize_t *out)
{
  size_t sent = 0;

  if (fd != VG_CONFIG_MQTT_HANDLE || out == NULL ||
      g_active == NULL)
    {
      return -1;
    }

  while (sent < length)
    {
      ssize_t wrote = vg_ota_tls_send(
        &g_active->tls, (const uint8_t *)bytes + sent, length - sent);
      if (wrote <= 0)
        {
          *out = MQTT_ERROR_SOCKET_ERROR;
          return 0;
        }
      sent += (size_t)wrote;
    }

  *out = (ssize_t)sent;
  return 0;
}

int vg_config_mqtt_pal_recv(int fd, void *bytes, size_t length,
                            ssize_t *out)
{
  ssize_t received;

  if (fd != VG_CONFIG_MQTT_HANDLE || out == NULL ||
      g_active == NULL)
    {
      return -1;
    }

  received = vg_ota_tls_receive(&g_active->tls, bytes, length, 20);
  *out = received == -EAGAIN ? 0 :
         received < 0 ? MQTT_ERROR_SOCKET_ERROR : received;
  return 0;
}

static void inbound(void **state, struct mqtt_response_publish *publish)
{
  struct vg_config_mqtt_transport *transport =
    state != NULL ? *state : NULL;

  if (transport == NULL || publish == NULL ||
      publish->topic_name_size >= VG_CONFIG_MQTT_TOPIC_MAX ||
      publish->application_message_size > VG_CONFIG_MQTT_REQUEST_MAX ||
      transport->message.used)
    {
      if (transport != NULL)
        {
          transport->client.error = MQTT_ERROR_RECV_BUFFER_TOO_SMALL;
        }
      return;
    }

  memset(&transport->message, 0, sizeof(transport->message));
  memcpy(transport->message.topic, publish->topic_name,
         publish->topic_name_size);
  transport->message.topic[publish->topic_name_size] = '\0';
  memcpy(transport->message.payload, publish->application_message,
         publish->application_message_size);
  transport->message.payload[publish->application_message_size] = '\0';
  transport->message.length = publish->application_message_size;
  transport->message.retain = publish->retain_flag;
  transport->message.qos = publish->qos_level;
  transport->message.used = true;
}

static bool valid_config(const struct vg_config_mqtt_config *config)
{
  size_t broker_length;
  size_t port_length;

  if (config == NULL)
    {
      return false;
    }

  broker_length = strnlen(config->broker, sizeof(config->broker));
  port_length = strnlen(config->port, sizeof(config->port));
  return broker_length > 0 && broker_length < sizeof(config->broker) &&
         port_length > 0 && port_length < sizeof(config->port) &&
         config->username[0] != '\0' && config->password[0] != '\0' &&
         strnlen(config->username, sizeof(config->username)) <
           sizeof(config->username) &&
         strnlen(config->password, sizeof(config->password)) <
           sizeof(config->password);
}

int vg_config_mqtt_transport_open(
  struct vg_config_mqtt_transport *transport,
  const struct vg_config_mqtt_config *config,
  const char *device_id,
  const char *ca_pem)
{
  char client_id[VG_DEVICE_ID_MAX + 8];
  char topic[VG_CONFIG_MQTT_TOPIC_MAX];
  uint16_t subscribe_ticket;
  int subscribed = 0;
  int ret;
  unsigned i;

  if (transport == NULL || !valid_config(config) ||
      device_id == NULL || strlen(device_id) >= sizeof(transport->device_id) ||
      ca_pem == NULL || ca_pem[0] == '\0')
    {
      return -EINVAL;
    }
  if (!vg_ota_tls_entropy_ready())
    {
      return -EAGAIN;
    }

  vg_config_mqtt_transport_close(transport);
  memset(transport->device_id, 0, sizeof(transport->device_id));
  memcpy(transport->device_id, device_id, strlen(device_id) + 1);
  vg_ota_https_init(&transport->tls, ca_pem);
  ret = vg_ota_tls_connect(&transport->tls, config->broker, config->port);
  if (ret != 0)
    {
      vg_ota_https_close(&transport->tls);
      return ret;
    }

  if (!transport->initialized)
    {
      ret = mqtt_init(&transport->client, VG_CONFIG_MQTT_HANDLE,
                      transport->tx, sizeof(transport->tx),
                      transport->rx, sizeof(transport->rx), inbound);
      if (ret != MQTT_OK)
        {
          vg_ota_https_close(&transport->tls);
          return -EIO;
        }
      transport->initialized = true;
    }
  else
    {
      mqtt_reinit(&transport->client, VG_CONFIG_MQTT_HANDLE,
                  transport->tx, sizeof(transport->tx),
                  transport->rx, sizeof(transport->rx));
    }

  g_active = transport;
  transport->client.publish_response_callback_state = transport;
  transport->client.send_offset = 0;
  transport->client.response_timeout = 10;
  snprintf(client_id, sizeof(client_id), "%s-config", device_id);
  ret = mqtt_connect(&transport->client, client_id, NULL, NULL, 0,
                     config->username, config->password,
                     MQTT_CONNECT_CLEAN_SESSION, 30);
  if (ret != MQTT_OK)
    {
      goto failed;
    }

  for (i = 0; i < 150 && !transport->client.event_connect &&
       transport->client.error == MQTT_OK; i++)
    {
      (void)mqtt_sync(&transport->client);
      usleep(20000);
    }
  if (!transport->client.event_connect ||
      transport->client.error != MQTT_OK)
    {
      ret = -EACCES;
      goto failed;
    }

  snprintf(topic, sizeof(topic), "vg/%s/config/request", device_id);
  ret = mqtt_subscribe(&transport->client, topic, 1);
  if (ret != MQTT_OK)
    {
      goto failed;
    }
  subscribe_ticket = transport->client.pid_lfsr;
  for (i = 0; i < 150 && transport->client.error == MQTT_OK; i++)
    {
      struct mqtt_queued_message *queued;

      (void)mqtt_sync(&transport->client);
      queued = mqtt_mq_find(&transport->client.mq,
                            MQTT_CONTROL_SUBSCRIBE, &subscribe_ticket);
      if (queued == NULL || queued->state == MQTT_QUEUED_COMPLETE)
        {
          subscribed = 1;
          break;
        }
      usleep(20000);
    }
  if (transport->client.error != MQTT_OK || !subscribed)
    {
      ret = transport->client.error == MQTT_ERROR_SUBSCRIBE_FAILED ?
            -EACCES : -EIO;
      goto failed;
    }

  transport->online = true;
  return 0;

failed:
  g_active = NULL;
  vg_ota_https_close(&transport->tls);
  memset(transport->tx, 0, sizeof(transport->tx));
  memset(transport->rx, 0, sizeof(transport->rx));
  memset(&transport->message, 0, sizeof(transport->message));
  return ret < 0 ? ret : -EIO;
}

int vg_config_mqtt_transport_poll(
  struct vg_config_mqtt_transport *transport)
{
  if (transport == NULL || !transport->online)
    {
      return -ENOTCONN;
    }
  if (mqtt_sync(&transport->client) != MQTT_OK ||
      transport->client.error != MQTT_OK)
    {
      vg_config_mqtt_transport_close(transport);
      return -EIO;
    }
  return 0;
}

int vg_config_mqtt_transport_take(
  struct vg_config_mqtt_transport *transport,
  struct vg_config_mqtt_message *out)
{
  if (transport == NULL || out == NULL)
    {
      return -EINVAL;
    }
  if (!transport->message.used)
    {
      return -ENOENT;
    }

  *out = transport->message;
  memset(&transport->message, 0, sizeof(transport->message));
  return 0;
}

const struct vg_config_mqtt_message *vg_config_mqtt_transport_peek(
  struct vg_config_mqtt_transport *transport)
{
  return transport != NULL && transport->message.used ?
         &transport->message : NULL;
}

void vg_config_mqtt_transport_release(
  struct vg_config_mqtt_transport *transport)
{
  if (transport != NULL)
    {
      memset(&transport->message, 0, sizeof(transport->message));
    }
}

int vg_config_mqtt_transport_publish_state(
  struct vg_config_mqtt_transport *transport,
  const char *json,
  uint16_t *ticket)
{
  char topic[VG_CONFIG_MQTT_TOPIC_MAX];
  size_t length;

  if (transport == NULL || json == NULL || !transport->online)
    {
      return -ENOTCONN;
    }
  length = strlen(json);
  if (length == 0 || length >= VG_CONFIG_STATE_MAX)
    {
      return -EMSGSIZE;
    }

  snprintf(topic, sizeof(topic), "vg/%s/config/state",
           transport->device_id);
  if (mqtt_publish(&transport->client, topic, json, length,
                   MQTT_PUBLISH_QOS_1) != MQTT_OK)
    {
      return -EIO;
    }
  if (ticket != NULL)
    {
      *ticket = transport->client.pid_lfsr;
    }
  return 0;
}

int vg_config_mqtt_transport_acked(
  struct vg_config_mqtt_transport *transport,
  uint16_t ticket)
{
  struct mqtt_queued_message *message;

  if (transport == NULL || !transport->online || ticket == 0)
    {
      return -ENOTCONN;
    }
  message = mqtt_mq_find(&transport->client.mq, MQTT_CONTROL_PUBLISH,
                         &ticket);
  return message == NULL || message->state == MQTT_QUEUED_COMPLETE ? 1 : 0;
}

bool vg_config_mqtt_transport_online(
  const struct vg_config_mqtt_transport *transport)
{
  return transport != NULL && transport->online;
}

void vg_config_mqtt_transport_close(
  struct vg_config_mqtt_transport *transport)
{
  if (transport == NULL)
    {
      return;
    }
  if (g_active == transport)
    {
      g_active = NULL;
    }
  transport->online = false;
  vg_ota_https_close(&transport->tls);
  memset(transport->tx, 0, sizeof(transport->tx));
  memset(transport->rx, 0, sizeof(transport->rx));
  memset(&transport->message, 0, sizeof(transport->message));
}
