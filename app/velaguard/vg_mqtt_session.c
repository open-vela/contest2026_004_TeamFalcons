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

/**
  * @brief  将 Kconfig Broker 端口格式化为十进制字符串。
  * @param  buf  输出缓冲。
  * @param  n    缓冲长度。
  * @retval None
  */
static void port_str(char *buf, size_t n)
{
  snprintf(buf, n, "%d", CONFIG_VG_MQTT_BROKER_PORT);
}

/**
  * @brief  关闭当前 MQTT TCP，清除 online 标志。
  * @note   切换出口时必须先调用本函数，禁止双连接并存。
  * @retval None
  */
void vg_mqtt_session_close(void)
{
  if (g_fd >= 0)
    {
      vg_tcp_close(&g_fd);
    }

  g_online = false;
}

/**
  * @brief  发布 QoS0 retained status（含 network 与 uptime）。
  * @note   仅在 CONNACK 成功后调用；不做 TLS / AI topic。
  * @retval None
  */
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

/**
  * @brief  在指定 backend 上建连、CONNECT，等待 CONNACK 后发 status。
  * @note   内部先 close 旧会话。status.network 使用 net_name（rj45|esp01|none）。
  *         约 4s（80×50ms，含 lesp 500ms recv 轮询）内未收到 CONNACK 则关闭并失败。
  *         vgmqtt 一次性调试命令语义不变。
  * @param  backend   VG_TCP_POSIX 或 VG_TCP_LESP。
  * @param  net_name  写入 status JSON 的 network 字段。
  * @retval 0   CONNACK 成功且已 publish status。
  * @retval -1  建连/CONNECT/CONNACK 失败（会话已关闭）。
  */
int vg_mqtt_session_open(vg_tcp_backend_t backend, const char *net_name)
{
  char port[8];
  uint8_t connflags;
  int ret;
  int i;

  /* Tear down any prior session before opening a new egress */

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

  /* Plaintext MQTT-C CONNECT with LWT on status topic */

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

  /* Wait for CONNACK; online flag stays false until event_connect.
   * mqtt_sync receives before sending queued CONNECT, so lesp recv may
   * spin with 0-byte reads until CONNECT goes out and CONNACK returns. */

  for (i = 0; i < 80 && !g_client.event_connect &&
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

/**
  * @brief  驱动 mqtt_sync；出错则关闭会话。
  * @note   由 net_mgr 周期调用；无在线会话时立即返回。
  * @retval None
  */
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

/**
  * @brief  当前会话是否已收到 CONNACK。
  * @retval true   在线。
  * @retval false  未连接或已关闭。
  */
bool vg_mqtt_session_online(void)
{
  return g_online;
}
