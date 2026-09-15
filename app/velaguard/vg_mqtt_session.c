/****************************************************************************
 * app/velaguard/vg_mqtt_session.c
 *
 * MQTT-C 常驻会话：CONNACK 才 online；切出口先 close 再 open。
 * dashboard-api：status / telemetry / alarm / point_table。
 * 发布只在本文件（net_mgr 线程）；采集/HMI 只入队。
 ****************************************************************************/

#include <nuttx/config.h>

#include <mqtt.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "vg_device_id.h"
#include "vg_discover.h"
#include "vg_mqtt_payload.h"
#include "vg_mqtt_session.h"
#include "vg_tcp_transport.h"

#ifndef CONFIG_VG_MQTT_BROKER_HOST
#  define CONFIG_VG_MQTT_BROKER_HOST "8.148.67.174"
#endif
#ifndef CONFIG_VG_MQTT_BROKER_PORT
#  define CONFIG_VG_MQTT_BROKER_PORT 1883
#endif
#ifndef CONFIG_VG_MQTT_KEEPALIVE
#  define CONFIG_VG_MQTT_KEEPALIVE 60
#endif
#ifndef CONFIG_VG_LIVE_VALUES_PATH
#  define CONFIG_VG_LIVE_VALUES_PATH "/data/velaguard/live/values.txt"
#endif
#ifndef CONFIG_VG_DISCOVER_POINTS_PATH
#  define CONFIG_VG_DISCOVER_POINTS_PATH "/data/velaguard/config/points.json"
#endif

#define VG_MQTT_TX_SZ     10240
#define VG_MQTT_RX_SZ     512
#define VG_MQTT_PT_MAX    8192
#define VG_MQTT_ALARM_N   8
#define VG_MQTT_ALARM_B   384
#define VG_MQTT_TEL_B     2048
#define VG_MQTT_PERIOD_MS 30000ull

#if VG_MQTT_TEL_HIST_MAX < VG_DISCOVER_MAX_POINTS
#  error VG_MQTT_TEL_HIST_MAX must be >= VG_DISCOVER_MAX_POINTS
#endif
#if VG_MQTT_TEL_ID_MAX < VG_POINT_ID_MAX
#  error VG_MQTT_TEL_ID_MAX must be >= VG_POINT_ID_MAX
#endif

struct vg_mqtt_alarm_slot
{
  char json[VG_MQTT_ALARM_B];
  int used;
};

static int g_fd = -1;
static struct mqtt_client g_client;
static uint8_t g_tx[VG_MQTT_TX_SZ];
static uint8_t g_rx[VG_MQTT_RX_SZ];
static bool g_online;
static char g_net[8];
static char g_will[96];
static char g_topic[VG_MQTT_TOPIC_MAX];
static char g_status[320];
static char g_pt_buf[VG_MQTT_PT_MAX];
static char g_tel_buf[VG_MQTT_TEL_B];
static struct vg_mqtt_alarm_slot g_alarm_q[VG_MQTT_ALARM_N];
static int g_alarm_head;
static int g_alarm_count;
static int g_tel_pending;
static struct vg_mqtt_tel_hist g_tel_hist[VG_MQTT_TEL_HIST_MAX];
static int g_tel_hist_n;
static struct vg_mqtt_tel_hist g_tel_sent[VG_MQTT_TEL_HIST_MAX];
static int g_tel_sent_n;
static int g_pt_dirty;
static int g_pt_sent;
static uint32_t g_boot_id;
static unsigned g_alarm_seq;
static uint64_t g_next_period_ms;
static pthread_mutex_t g_qlock = PTHREAD_MUTEX_INITIALIZER;

static uint64_t now_ms(void)
{
  struct timespec ts;

  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint64_t)ts.tv_sec * 1000ull +
         (uint64_t)ts.tv_nsec / 1000000ull;
}

static long long wall_ms(void)
{
  struct timespec ts;

  if (clock_gettime(CLOCK_REALTIME, &ts) != 0)
    {
      return 0;
    }

  if (ts.tv_sec < (time_t)1704067200)
    {
      return 0;
    }

  return (long long)ts.tv_sec * 1000LL + ts.tv_nsec / 1000000LL;
}

static void port_str(char *buf, size_t n)
{
  snprintf(buf, n, "%d", CONFIG_VG_MQTT_BROKER_PORT);
}

static const char *mqtt_user(void)
{
#ifdef CONFIG_VG_MQTT_USERNAME
  if (CONFIG_VG_MQTT_USERNAME[0] != '\0')
    {
      return CONFIG_VG_MQTT_USERNAME;
    }
#endif
  return NULL;
}

static const char *mqtt_pass(void)
{
#ifdef CONFIG_VG_MQTT_PASSWORD
  if (CONFIG_VG_MQTT_PASSWORD[0] != '\0')
    {
      return CONFIG_VG_MQTT_PASSWORD;
    }
#endif
  return NULL;
}

static void ensure_boot_id(void)
{
  if (g_boot_id != 0)
    {
      return;
    }

  g_boot_id = (uint32_t)now_ms();
  if (g_boot_id == 0)
    {
      g_boot_id = 1;
    }
}

static void mqtt_ignore_inbound(void **state,
                                struct mqtt_response_publish *publish)
{
  (void)state;
  (void)publish;
}

static int publish_raw(const char *kind, const void *payload, size_t len,
                       uint8_t flags)
{
  const char *id = vg_device_id();
  enum MQTTErrors err;

  if (vg_mqtt_topic(g_topic, sizeof(g_topic), id, kind) != 0)
    {
      return -1;
    }

  err = mqtt_publish(&g_client, g_topic, payload, len, flags);
  if (err != MQTT_OK)
    {
      printf("vgnet: mqtt pub %s failed %s\n", kind, mqtt_error_str(err));
      return -1;
    }

  return 0;
}

static int flush_publish_queue(void)
{
  int i;

  /* QoS1 stays AWAITING_ACK until PUBACK; do not wait for mq_find NULL. */
  for (i = 0; i < 8 && g_client.error == MQTT_OK; i++)
    {
      mqtt_sync(&g_client);
      usleep(20000);
    }

  if (g_client.error != MQTT_OK)
    {
      printf("vgnet: mqtt flush %s\n", mqtt_error_str(g_client.error));
      return -1;
    }

  return 0;
}

static void publish_status(void)
{
  struct timespec mono;
  long long uptime_ms = 0;

  if (clock_gettime(CLOCK_MONOTONIC, &mono) == 0)
    {
      uptime_ms = (long long)mono.tv_sec * 1000LL +
                  mono.tv_nsec / 1000000LL;
    }

  if (vg_mqtt_format_status(g_status, sizeof(g_status), vg_device_id(),
                            g_net, uptime_ms, wall_ms()) != 0)
    {
      return;
    }

  (void)publish_raw("status", g_status, strlen(g_status),
                    MQTT_PUBLISH_QOS_0 | MQTT_PUBLISH_RETAIN);
}

static int load_point_table(void)
{
  FILE *fp;
  size_t n;

  fp = fopen(CONFIG_VG_DISCOVER_POINTS_PATH, "r");
  if (fp == NULL)
    {
      return -1;
    }

  n = fread(g_pt_buf, 1, sizeof(g_pt_buf) - 1, fp);
  fclose(fp);
  g_pt_buf[n] = '\0';
  if (!vg_mqtt_point_table_has_points(g_pt_buf, n))
    {
      return -1;
    }

  return (int)n;
}

static void publish_point_table(void)
{
  int n;

  n = load_point_table();
  if (n <= 0)
    {
      return;
    }

  (void)publish_raw("point_table", g_pt_buf, (size_t)n,
                    MQTT_PUBLISH_QOS_1 | MQTT_PUBLISH_RETAIN);
  g_pt_sent = 1;
}

static void build_telemetry(void)
{
  static struct vg_live_snapshot snap;
  struct vg_mqtt_tel_pt pts[VG_DISCOVER_MAX_POINTS];
  struct vg_mqtt_tel_hist sent[VG_MQTT_TEL_HIST_MAX];
  uint32_t now;
  int i;
  int n;
  int nout;
  int sent_n;

  memset(&snap, 0, sizeof(snap));
  if (vg_live_snapshot_read(CONFIG_VG_LIVE_VALUES_PATH, &snap) != 0 ||
      snap.n <= 0)
    {
      return;
    }

  n = snap.n;
  if (n > VG_DISCOVER_MAX_POINTS)
    {
      n = VG_DISCOVER_MAX_POINTS;
    }

  now = vg_live_now_ms();
  nout = 0;
  sent_n = 0;
  memset(sent, 0, sizeof(sent));
  for (i = 0; i < n; i++)
    {
      const char *id = snap.samples[i].id;
      int idx;
      int prev_known;
      int prev_ok;

      idx = vg_mqtt_tel_hist_find(g_tel_hist, g_tel_hist_n, id);
      prev_known = (idx >= 0);
      prev_ok = prev_known ? (int)g_tel_hist[idx].last_ok : 0;
      if (!vg_mqtt_tel_want_send(snap.samples[i].ok, prev_known, prev_ok))
        {
          continue;
        }

      pts[nout].id = snap.samples[i].id;
      pts[nout].value = snap.samples[i].value;
      pts[nout].ok = snap.samples[i].ok;
      pts[nout].age_ms = vg_live_age_ms(snap.tick_ms, now);
      nout++;
      (void)vg_mqtt_tel_hist_upsert(sent, &sent_n, VG_MQTT_TEL_HIST_MAX,
                                    id, snap.samples[i].ok);
    }

  if (nout <= 0)
    {
      return;
    }

  if (vg_mqtt_format_telemetry(g_tel_buf, sizeof(g_tel_buf), pts, nout) != 0)
    {
      return;
    }

  pthread_mutex_lock(&g_qlock);
  memcpy(g_tel_sent, sent, sizeof(g_tel_sent));
  g_tel_sent_n = sent_n;
  g_tel_pending = 1;
  pthread_mutex_unlock(&g_qlock);
}

static void drain_queues(void)
{
  char alarm_copy[VG_MQTT_ALARM_B];
  int send_pt;
  int send_tel;

  for (; ; )
    {
      alarm_copy[0] = '\0';
      pthread_mutex_lock(&g_qlock);
      if (g_alarm_count > 0)
        {
          snprintf(alarm_copy, sizeof(alarm_copy), "%s",
                   g_alarm_q[g_alarm_head].json);
        }

      pthread_mutex_unlock(&g_qlock);
      if (alarm_copy[0] == '\0')
        {
          break;
        }

      if (publish_raw("alarm", alarm_copy, strlen(alarm_copy),
                      MQTT_PUBLISH_QOS_1) != 0)
        {
          return;
        }

      pthread_mutex_lock(&g_qlock);
      if (g_alarm_count > 0 &&
          strcmp(g_alarm_q[g_alarm_head].json, alarm_copy) == 0)
        {
          g_alarm_q[g_alarm_head].used = 0;
          g_alarm_head = (g_alarm_head + 1) % VG_MQTT_ALARM_N;
          g_alarm_count--;
        }

      pthread_mutex_unlock(&g_qlock);
    }

  pthread_mutex_lock(&g_qlock);
  send_pt = g_pt_dirty;
  g_pt_dirty = 0;
  send_tel = g_tel_pending;
  pthread_mutex_unlock(&g_qlock);

  if (send_pt)
    {
      publish_point_table();
    }

  if (send_tel)
    {
      if (publish_raw("telemetry", g_tel_buf, strlen(g_tel_buf),
                      MQTT_PUBLISH_QOS_0) == 0)
        {
          int i;

          pthread_mutex_lock(&g_qlock);
          for (i = 0; i < g_tel_sent_n; i++)
            {
              (void)vg_mqtt_tel_hist_upsert(g_tel_hist, &g_tel_hist_n,
                                            VG_MQTT_TEL_HIST_MAX,
                                            g_tel_sent[i].id,
                                            (int)g_tel_sent[i].last_ok);
            }

          g_tel_pending = 0;
          pthread_mutex_unlock(&g_qlock);
        }
    }
}

void vg_mqtt_enqueue_alarm(const char *id, const char *kind,
                           const char *state, float value, float thr,
                           const char *level, long long ts_ms)
{
  char alarm_id[80];
  char json[VG_MQTT_ALARM_B];
  struct vg_mqtt_alarm_slot *s;
  int slot;

  if (id == NULL || id[0] == '\0' || kind == NULL || state == NULL)
    {
      return;
    }

  pthread_mutex_lock(&g_qlock);
  ensure_boot_id();
  g_alarm_seq++;
  snprintf(alarm_id, sizeof(alarm_id), "%s-%08x-%u", vg_device_id(),
           (unsigned)g_boot_id, g_alarm_seq);
  pthread_mutex_unlock(&g_qlock);
  if (ts_ms == 0)
    {
      ts_ms = wall_ms();
    }

  if (vg_mqtt_format_alarm(json, sizeof(json), vg_device_id(), alarm_id, id,
                           kind, state, value, thr, level, ts_ms) != 0)
    {
      return;
    }

  pthread_mutex_lock(&g_qlock);
  if (g_alarm_count >= VG_MQTT_ALARM_N)
    {
      g_alarm_q[g_alarm_head].used = 0;
      g_alarm_head = (g_alarm_head + 1) % VG_MQTT_ALARM_N;
      g_alarm_count--;
    }

  slot = (g_alarm_head + g_alarm_count) % VG_MQTT_ALARM_N;
  s = &g_alarm_q[slot];
  snprintf(s->json, sizeof(s->json), "%s", json);
  s->used = 1;
  g_alarm_count++;
  pthread_mutex_unlock(&g_qlock);
}

void vg_mqtt_notify_point_table(void)
{
  pthread_mutex_lock(&g_qlock);
  g_pt_dirty = 1;
  pthread_mutex_unlock(&g_qlock);
}

void vg_mqtt_session_close(void)
{
  if (g_fd >= 0)
    {
      vg_tcp_close(&g_fd);
    }

  memset(&g_client, 0, sizeof(g_client));
  g_fd = -1;
  g_online = false;
  g_pt_sent = 0;
}

int vg_mqtt_session_open(vg_tcp_backend_t backend, const char *net_name)
{
  char port[8];
  uint8_t connflags;
  const char *id;
  int ret;
  int i;

  vg_mqtt_session_close();
  strlcpy(g_net, net_name ? net_name : "none", sizeof(g_net));
  port_str(port, sizeof(port));
  id = vg_device_id();
  ensure_boot_id();

  if (vg_mqtt_format_lwt(g_will, sizeof(g_will), id) != 0)
    {
      return -1;
    }

  g_fd = vg_tcp_open(backend, CONFIG_VG_MQTT_BROKER_HOST, port);
  if (g_fd < 0)
    {
      printf("vgnet: tcp connect %s:%s via %s failed\n",
             CONFIG_VG_MQTT_BROKER_HOST, port,
             vg_tcp_backend_str(backend));
      return -1;
    }

  mqtt_init(&g_client, g_fd, g_tx, sizeof(g_tx), g_rx, sizeof(g_rx),
            mqtt_ignore_inbound);
  connflags = MQTT_CONNECT_CLEAN_SESSION |
              MQTT_CONNECT_WILL_QOS_0 |
              MQTT_CONNECT_WILL_RETAIN;
  if (vg_mqtt_topic(g_topic, sizeof(g_topic), id, "status") != 0)
    {
      vg_mqtt_session_close();
      return -1;
    }

  ret = mqtt_connect(&g_client, id, g_topic, g_will, strlen(g_will),
                     mqtt_user(), mqtt_pass(), connflags,
                     (uint16_t)CONFIG_VG_MQTT_KEEPALIVE);
  if (ret != MQTT_OK)
    {
      vg_mqtt_session_close();
      return -1;
    }

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
  pthread_mutex_lock(&g_qlock);
  g_pt_dirty = 1;
  g_next_period_ms = now_ms() + VG_MQTT_PERIOD_MS;
  pthread_mutex_unlock(&g_qlock);
  drain_queues();
  if (flush_publish_queue() != 0)
    {
      vg_mqtt_session_close();
      return -1;
    }

  printf("vgnet: mqtt online network=%s backend=%s device=%s\n",
         g_net, vg_tcp_backend_str(backend), id);
  return 0;
}

void vg_mqtt_session_poll(void)
{
  uint64_t t;

  if (g_fd < 0 || !g_online)
    {
      return;
    }

  mqtt_sync(&g_client);
  if (g_client.error != MQTT_OK)
    {
      printf("vgnet: mqtt error %s\n", mqtt_error_str(g_client.error));
      vg_mqtt_session_close();
      return;
    }

  t = now_ms();
  pthread_mutex_lock(&g_qlock);
  if (t >= g_next_period_ms)
    {
      g_next_period_ms = t + VG_MQTT_PERIOD_MS;
      pthread_mutex_unlock(&g_qlock);
      publish_status();
      build_telemetry();
      pthread_mutex_lock(&g_qlock);
      if (!g_pt_sent)
        {
          g_pt_dirty = 1;
        }

      pthread_mutex_unlock(&g_qlock);
    }
  else
    {
      pthread_mutex_unlock(&g_qlock);
    }

  drain_queues();
  mqtt_sync(&g_client);
}

bool vg_mqtt_session_online(void)
{
  return g_online;
}
