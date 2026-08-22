/****************************************************************************
 * app/velaguard/velaguard_mqtt.c
 *
 * vgmqtt - VelaGuard MQTT-C bring-up tool (NSH command).
 *
 * 用途：M2 里程碑的调试工具。连接云 Broker（明文测试端口），发布 retained
 * status 消息，并携带 LWT 遗嘱（同 topic, {"online":false}, retained），
 * 用于验证"设备掉线后云侧立即感知"。
 *
 * 用法（NSH）：
 *   vgmqtt -h <broker> [-p <port>] [-u <user>] [-P <pass>]
 *          [-t <topic>] [-m <json>] [-q <0|1|2>] [-r] [-w <secs>]
 *
 * 缺省：port=1883, topic=vg/{DEVID}/status, payload=合同 §4.1 status JSON,
 *       QoS=0（合同 §3：status 用 QoS0 + retained）, retain=on, 等待 0 秒。
 *       -q 可覆盖为 1/2（仅供调试；QoS1 需等 PUBACK，回程不稳时会超时）。
 * -w <secs>：发布后保持连接 sleep（用于 LWT 演示：等待期间拔网线/断电，
 *   云侧即可收到遗嘱 {"online":false}；正常退出属优雅断开，不触发 LWT）。
 * DEVID 可在编译期用 -DDEVID="xxx" 覆盖（test 构建，手册 §16.1）。
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

#ifndef DEVID
#  define DEVID "vg-test-01"
#endif

#define VGMQTT_DEFAULT_PORT "1883"
#define VGMQTT_TOPIC        "vg/" DEVID "/status"
#define VGMQTT_WILL_MSG     "{\"online\":false}"
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
          "Defaults: port=%s topic=%s QoS=0 retain=on\n",
          prog, VGMQTT_DEFAULT_PORT, VGMQTT_TOPIC);
}

/* 解析命令行参数（沿用 vg* 工具风格，getopt） */

static int parse_args(int argc, FAR char *argv[],
                      FAR struct vgmqtt_cfg_s *cfg)
{
  int opt;

  memset(cfg, 0, sizeof(*cfg));
  cfg->port   = VGMQTT_DEFAULT_PORT;
  cfg->topic  = VGMQTT_TOPIC;
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

  if (cfg->host == NULL)
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
  struct timespec real;
  long long uptime_ms = 0;
  long long ts_ms = 0;

  if (clock_gettime(CLOCK_MONOTONIC, &mono) == 0)
    {
      uptime_ms = (long long)mono.tv_sec * 1000LL +
                  mono.tv_nsec / 1000000LL;
    }

  if (clock_gettime(CLOCK_REALTIME, &real) == 0)
    {
      ts_ms = (long long)real.tv_sec * 1000LL +
              real.tv_nsec / 1000000LL;
    }

  snprintf(buf, bufsz,
           "{\"device_id\":\"%s\",\"online\":true,"
           "\"firmware\":\"0.1.0\",\"build_mode\":\"TEST\","
           "\"network\":\"rj45\",\"uptime_ms\":%lld,\"ts_ms\":%lld,"
           "\"time_quality\":\"unknown\"}",
           DEVID, uptime_ms, ts_ms);
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
  uint8_t pubflags;
  uint8_t connflags;
  long long start_ms;
  int fd;
  int ret;

  if (parse_args(argc, argv, &cfg) != OK)
    {
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

  /* keepalive 15s：LWT 演示时云侧约 1.5x 周期内判定掉线 */

  ret = mqtt_connect(&client, DEVID, cfg.topic,
                     VGMQTT_WILL_MSG, strlen(VGMQTT_WILL_MSG),
                     cfg.user, cfg.pass, connflags, 15);
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

  printf("vgmqtt: connected (client_id=%s)\n", DEVID);

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
