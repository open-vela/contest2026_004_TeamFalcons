/****************************************************************************
 * app/velaguard/vg_mqtt_session.c
 *
 * MQTT-C 常驻会话：CONNACK 才 online；切出口先 close 再 open。
 * status QoS0 retained，network=rj45|esp01|none。API 见 vg_mqtt_session.h。
 ****************************************************************************/

#include <nuttx/config.h>

#include <mqtt.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "vg_mqtt_session.h"
#include "vg_tcp_transport.h"

#ifndef DEVID
#  define DEVID "vg-test-01"
#endif

#ifndef CONFIG_VG_MQTT_BROKER_HOST
#  define CONFIG_VG_MQTT_BROKER_HOST "107.174.123.74"
#endif
#ifndef CONFIG_VG_MQTT_BROKER_PORT
#  define CONFIG_VG_MQTT_BROKER_PORT 1883
#endif

#define VGMQTT_WILL_MSG "{\"online\":false}"

static int g_fd = -1;
static struct mqtt_client g_client;
static uint8_t g_tx[512];
static uint8_t g_rx[512];
static bool g_online;
static char g_net[8];

static void port_str(char *buf, size_t n)
{
  snprintf(buf, n, "%d", CONFIG_VG_MQTT_BROKER_PORT);
}

void vg_mqtt_session_close(void)
{
  if (g_fd >= 0)
    {
      vg_tcp_close(&g_fd);
    }

  g_online = false;
}

static void publish_status(void)
{
  char payload[256];
  struct timespec mono;
  long long uptime_ms = 0;

  if (clock_gettime(CLOCK_MONOTONIC, &mono) == 0)
    {
      uptime_ms = (long long)mono.tv_sec * 1000LL +
                  mono.tv_nsec / 1000000LL;
    }

  snprintf(payload, sizeof(payload),
           "{\"device_id\":\"%s\",\"online\":true,"
           "\"firmware\":\"0.1.0\",\"build_mode\":\"TEST\","
           "\"network\":\"%s\",\"uptime_ms\":%lld,\"ts_ms\":0,"
           "\"time_quality\":\"unknown\"}",
           DEVID, g_net, uptime_ms);

  mqtt_publish(&g_client, "vg/" DEVID "/status", payload, strlen(payload),
               MQTT_PUBLISH_QOS_0 | MQTT_PUBLISH_RETAIN);
}

int vg_mqtt_session_open(vg_tcp_backend_t backend, const char *net_name)
{
  char port[8];
  uint8_t connflags;
  int ret;
  int i;

  vg_mqtt_session_close();
  strlcpy(g_net, net_name ? net_name : "none", sizeof(g_net));
  port_str(port, sizeof(port));

  g_fd = vg_tcp_open(backend, CONFIG_VG_MQTT_BROKER_HOST, port);
  if (g_fd < 0)
    {
      printf("vgnet: tcp connect %s:%s via %s failed\n",
             CONFIG_VG_MQTT_BROKER_HOST, port,
             vg_tcp_backend_str(backend));
      return -1;
    }

  mqtt_init(&g_client, g_fd, g_tx, sizeof(g_tx), g_rx, sizeof(g_rx), NULL);
  connflags = MQTT_CONNECT_CLEAN_SESSION |
              MQTT_CONNECT_WILL_QOS_0 |
              MQTT_CONNECT_WILL_RETAIN;
  ret = mqtt_connect(&g_client, DEVID, "vg/" DEVID "/status",
                     VGMQTT_WILL_MSG, strlen(VGMQTT_WILL_MSG),
                     NULL, NULL, connflags, 15);
  if (ret != MQTT_OK)
    {
      vg_mqtt_session_close();
      return -1;
    }

  for (i = 0; i < 40 && !g_client.event_connect &&
       g_client.error == MQTT_OK; i++)
    {
      mqtt_sync(&g_client);
      usleep(50000);
    }

  if (!g_client.event_connect)
    {
      printf("vgnet: no CONNACK via %s\n", vg_tcp_backend_str(backend));
      vg_mqtt_session_close();
      return -1;
    }

  g_online = true;
  publish_status();
  mqtt_sync(&g_client);
  printf("vgnet: mqtt online network=%s backend=%s\n",
         g_net, vg_tcp_backend_str(backend));
  return 0;
}

void vg_mqtt_session_poll(void)
{
  if (g_fd < 0 || !g_online)
    {
      return;
    }

  mqtt_sync(&g_client);
  if (g_client.error != MQTT_OK)
    {
      printf("vgnet: mqtt error %s\n", mqtt_error_str(g_client.error));
      vg_mqtt_session_close();
    }
}

bool vg_mqtt_session_online(void)
{
  return g_online;
}
