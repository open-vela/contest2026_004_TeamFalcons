/****************************************************************************
 * app/velaguard/velaguard_mqtt.c
 *
 * vgmqtt - VelaGuard MQTT-C bring-up tool (NSH command).
 *
 * 一次性调试：连看板 Broker，发 retained status + LWT（同主题，
 * {"device_id","online":false}）。常驻上报走 vg_mqtt_session，不靠本命令。
 *
 * 用法（NSH）：
 *   vgmqtt [-h <broker>] [-p <port>] [-u <user>] [-P <pass>]
 *          [-t <topic>] [-m <json>] [-q <0|1|2>] [-r] [-w <secs>]
 *
 * 缺省：Kconfig Broker/用户，topic=vg/{uid}/status，QoS0 retained。
 * client_id 来自芯片 UID（CONFIG_VG_MQTT_DEVICE_ID 可覆盖）。
 * -w：保持连接以便拔线触发 LWT；正常退出不触发 LWT。
 * 不要在日志里打印密码。
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#include <mqtt.h>

#include "vg_device_id.h"
#include "vg_mqtt_payload.h"

#ifndef CONFIG_VG_MQTT_BROKER_HOST
#  define CONFIG_VG_MQTT_BROKER_HOST "8.148.67.174"
#endif
#ifndef CONFIG_VG_MQTT_BROKER_PORT
#  define CONFIG_VG_MQTT_BROKER_PORT 1883
#endif
#ifndef CONFIG_VG_MQTT_KEEPALIVE
#  define CONFIG_VG_MQTT_KEEPALIVE 60
#endif

#define VGMQTT_DEFAULT_PORT "1883"
#define VGMQTT_TXBUFSZ      512
#define VGMQTT_RXBUFSZ      512

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct vgmqtt_cfg_s
{
  FAR const char *host;
  FAR const char *port;
  FAR const char *topic;
  FAR const char *msg;
  FAR const char *user;
  FAR const char *pass;
  uint8_t         qos;
  bool            retain;
  int             wait_secs;
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/* 单调时钟毫秒：确认等待用真实墙钟计时，避免被阻塞式 mqtt_sync 稀释 */

static long long now_ms(void)
{
  struct timespec ts;

  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (long long)ts.tv_sec * 1000LL + ts.tv_nsec / 1000000LL;
}

static void show_usage(FAR const char *prog)
{
  fprintf(stderr,
          "Usage: %s -h <broker> [-p <port>] [-u <user>] [-P <pass>]\n"
          "       [-t <topic>] [-m <json>] [-q <0|1|2>] [-r]\n"
          "Defaults: host=%s port=%s topic=vg/{uid}/status QoS=0 retain=on\n",
          prog, CONFIG_VG_MQTT_BROKER_HOST, VGMQTT_DEFAULT_PORT);
}

/* 解析命令行参数（沿用 vg* 工具风格，getopt） */

static int parse_args(int argc, FAR char *argv[],
                      FAR struct vgmqtt_cfg_s *cfg)
{
  int opt;

  memset(cfg, 0, sizeof(*cfg));
#ifdef CONFIG_VG_MQTT_BROKER_HOST
  cfg->host   = CONFIG_VG_MQTT_BROKER_HOST;
#else
  cfg->host   = NULL;
#endif
#ifdef CONFIG_VG_MQTT_USERNAME
  if (CONFIG_VG_MQTT_USERNAME[0] != '\0')
    {
      cfg->user = CONFIG_VG_MQTT_USERNAME;
    }
#endif
#ifdef CONFIG_VG_MQTT_PASSWORD
  if (CONFIG_VG_MQTT_PASSWORD[0] != '\0')
    {
      cfg->pass = CONFIG_VG_MQTT_PASSWORD;
    }
#endif
  cfg->port   = VGMQTT_DEFAULT_PORT;
  cfg->qos    = 0;
  cfg->retain = true;
  cfg->wait_secs = 0;

  while ((opt = getopt(argc, argv, "h:p:u:P:t:m:q:r:w:")) != ERROR)
    {
      switch (opt)
        {
          case 'h':
            cfg->host = optarg;
            break;

          case 'p':
            cfg->port = optarg;
            break;

          case 'u':
            cfg->user = optarg;
            break;

          case 'P':
            cfg->pass = optarg;
            break;

          case 't':
            cfg->topic = optarg;
            break;

          case 'm':
            cfg->msg = optarg;
            break;

          case 'q':
            cfg->qos = (uint8_t)strtol(optarg, NULL, 10);
            break;

          case 'r':
            cfg->retain = true;
            break;

          case 'w':
            cfg->wait_secs = atoi(optarg);
            break;

          default:
            show_usage(argv[0]);
            return ERROR;
        }
    }

  if (cfg->host == NULL || cfg->host[0] == '\0')
    {
      fprintf(stderr, "ERROR: broker host (-h) is required\n");
      show_usage(argv[0]);
      return ERROR;
    }

  if (cfg->qos > 2)
    {
      fprintf(stderr, "ERROR: qos must be 0, 1 or 2\n");
      return ERROR;
    }

  return OK;
}

/* 建立 TCP 连接（getaddrinfo 支持 IP 字面量；域名需启用 DNS 客户端） */

static int tcp_connect(FAR const char *host, FAR const char *port)
{
  struct addrinfo hints;
  FAR struct addrinfo *res = NULL;
  FAR struct addrinfo *it;
  struct timeval tv;
  int fd = ERROR;
  int ret;

  memset(&hints, 0, sizeof(hints));
  hints.ai_family   = AF_INET;
  hints.ai_socktype = SOCK_STREAM;

  ret = getaddrinfo(host, port, &hints, &res);
  if (ret != 0)
    {
      fprintf(stderr, "ERROR: getaddrinfo(%s:%s): %s\n",
              host, port, gai_strerror(ret));
      return ERROR;
    }

  for (it = res; it != NULL; it = it->ai_next)
    {
      fd = socket(it->ai_family, it->ai_socktype, it->ai_protocol);
      if (fd < 0)
        {
          continue;
        }

      if (connect(fd, it->ai_addr, it->ai_addrlen) == 0)
        {
          break;
        }

      close(fd);
      fd = ERROR;
    }

  freeaddrinfo(res);
  if (fd < 0)
    {
      return ERROR;
    }

  /* 接收/发送超时 2s：mqtt_sync 的 recv 是阻塞的，没有超时会在
   * CONNACK/PUBACK 不到时无限卡死（见 mqtt_pal_recvall）。 */

  tv.tv_sec  = 5;
  tv.tv_usec = 0;
  setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
  setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
  return fd;
}

/* 构造合同 §4.1 的 status JSON（snprintf 手拼，不引入 cjson） */

static void build_status_payload(FAR char *buf, size_t bufsz)
{
  struct timespec mono;
  long long uptime_ms = 0;

  if (clock_gettime(CLOCK_MONOTONIC, &mono) == 0)
    {
      uptime_ms = (long long)mono.tv_sec * 1000LL +
                  mono.tv_nsec / 1000000LL;
    }

  vg_mqtt_format_status(buf, bufsz, vg_device_id(), "rj45", uptime_ms,
                        0);
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int main(int argc, FAR char *argv[])
{
  struct vgmqtt_cfg_s cfg;
  struct mqtt_client client;
  static uint8_t sendbuf[VGMQTT_TXBUFSZ];
  static uint8_t recvbuf[VGMQTT_RXBUFSZ];
  char payload[256];
  char topic[VG_MQTT_TOPIC_MAX];
  char will[96];
  const char *devid;
  uint8_t pubflags;
  uint8_t connflags;
  long long start_ms;
  int fd;
  int ret;

  if (parse_args(argc, argv, &cfg) != OK)
    {
      return ERROR;
    }

  devid = vg_device_id();
  if (cfg.topic == NULL)
    {
      if (vg_mqtt_topic(topic, sizeof(topic), devid, "status") != 0)
        {
          fprintf(stderr, "ERROR: topic\n");
          return ERROR;
        }

      cfg.topic = topic;
    }

  if (vg_mqtt_format_lwt(will, sizeof(will), devid) != 0)
    {
      fprintf(stderr, "ERROR: lwt\n");
      return ERROR;
    }

  if (cfg.msg == NULL)
    {
      build_status_payload(payload, sizeof(payload));
      cfg.msg = payload;
    }

  printf("vgmqtt: connecting to %s:%s topic=%s qos=%u retain=%s wait=%us\n",
         cfg.host, cfg.port, cfg.topic, cfg.qos,
         cfg.retain ? "on" : "off", cfg.wait_secs);

  fd = tcp_connect(cfg.host, cfg.port);
  if (fd < 0)
    {
      fprintf(stderr, "ERROR: cannot connect to %s:%s (%s)\n",
              cfg.host, cfg.port, strerror(errno));
      return ERROR;
    }

  mqtt_init(&client, fd, sendbuf, sizeof(sendbuf),
            recvbuf, sizeof(recvbuf), NULL);

  /* LWT：掉线时云侧收到同 topic 的 {"online":false}，retained */

  connflags = MQTT_CONNECT_CLEAN_SESSION |
              MQTT_CONNECT_WILL_QOS_0 |
              MQTT_CONNECT_WILL_RETAIN;

  /* keepalive 60s：与常驻会话一致 */

  ret = mqtt_connect(&client, devid, cfg.topic,
                     will, strlen(will),
                     cfg.user, cfg.pass, connflags,
                     (uint16_t)CONFIG_VG_MQTT_KEEPALIVE);
  if (ret != MQTT_OK)
    {
      fprintf(stderr, "ERROR: mqtt_connect failed: %s\n",
              mqtt_error_str((enum MQTTErrors)ret));
      close(fd);
      return ERROR;
    }

  /* 等待 CONNACK（event_connect 由 mqtt_sync 处理 CONNACK 时置位），
   * 最多 10s；mqtt_connect 返回 OK 只代表 CONNECT 已排队，不代表连上。 */

  start_ms = now_ms();
  while (!client.event_connect && client.error == MQTT_OK &&
         now_ms() - start_ms < 10000)
    {
      mqtt_sync(&client);
      usleep(50000);
    }

  if (!client.event_connect)
    {
      fprintf(stderr, "ERROR: no CONNACK from broker (err=%s)\n",
              mqtt_error_str(client.error));
      close(fd);
      return ERROR;
    }

  printf("vgmqtt: connected (client_id=%s)\n", devid);

  /* cfg.qos 存数字 0/1/2，MQTT-C 发布标志需要编码值（qos << 1） */

  pubflags = (uint8_t)(cfg.qos << 1);
  if (cfg.retain)
    {
      pubflags |= MQTT_PUBLISH_RETAIN;
    }

  ret = mqtt_publish(&client, cfg.topic, cfg.msg, strlen(cfg.msg),
                     pubflags);
  if (ret != MQTT_OK)
    {
      fprintf(stderr, "ERROR: mqtt_publish failed: %s\n",
              mqtt_error_str((enum MQTTErrors)ret));
      mqtt_disconnect(&client);
      close(fd);
      return ERROR;
    }

  /* 确认 PUBLISH 已发出/已确认：MQTT-C 中消息完成后仍留在队列
   * （mqtt_mq_clean 只在发送缓冲不足时被调用），mqtt_mq_length 无法作为
   * 完成信号；改用 mqtt_mq_find(PUBLISH)——未完成时返回消息指针，
   * 完成后返回 NULL。 */

  start_ms = now_ms();
  while (client.error == MQTT_OK &&
         mqtt_mq_find(&client.mq, MQTT_CONTROL_PUBLISH, NULL) != NULL &&
         now_ms() - start_ms < 10000)
    {
      mqtt_sync(&client);
      usleep(50000);
    }

  if (client.error != MQTT_OK)
    {
      fprintf(stderr, "ERROR: publish failed: %s\n",
              mqtt_error_str(client.error));
      mqtt_disconnect(&client);
      close(fd);
      return ERROR;
    }

  if (mqtt_mq_find(&client.mq, MQTT_CONTROL_PUBLISH, NULL) != NULL)
    {
      fprintf(stderr, "ERROR: publish not confirmed within 10s\n");
      mqtt_disconnect(&client);
      close(fd);
      return ERROR;
    }

  printf("vgmqtt: published %zu bytes -> %s\n", strlen(cfg.msg),
         cfg.topic);

  /* LWT 演示窗口：保持连接，等待期间拔线/断电可触发遗嘱 */

  if (cfg.wait_secs > 0)
    {
      printf("vgmqtt: holding connection %us (pull cable/power to test LWT)...\n",
             cfg.wait_secs);
      sleep(cfg.wait_secs);
    }

  mqtt_disconnect(&client);
  mqtt_sync(&client);   /* 正常退出发送 DISCONNECT，避免误触发 LWT */
  close(fd);
  return OK;
}
