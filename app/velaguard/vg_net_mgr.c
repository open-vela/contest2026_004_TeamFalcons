/****************************************************************************
 * app/velaguard/vg_net_mgr.c
 *
 * 故障转移胶水线程：
 *   eth0 ping 采样 → ESP 热备采样 → vg_net_policy_step →
 *   （可选）ESP 复位/重 join → TCP 关旧开新 → mqtt_sync。
 *
 * 同时只维持一条 MQTT TCP。API 见 vg_net_mgr.h。
 ****************************************************************************/

#include <nuttx/config.h>

#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "vg_esp_bearer.h"
#include "vg_eth.h"
#include "vg_mqtt_session.h"
#include "vg_net_mgr.h"
#include "vg_net_policy.h"
#include "vg_tcp_transport.h"

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static struct vg_net_policy g_policy;
static bool g_started;
static uint64_t g_next_mqtt_ms;
static char g_reason[80];

/* Latest bearer samples from the manager loop; readers must hold g_lock */

static struct vg_eth_sample g_eth_last;
static struct vg_esp_sample g_esp_last;

/**
  * @brief  读取单调时钟毫秒。
  * @retval 自开机起的毫秒数（CLOCK_MONOTONIC）。
  */
static uint64_t now_ms(void)
{
  struct timespec ts;

  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint64_t)ts.tv_sec * 1000ull + (uint64_t)(ts.tv_nsec / 1000000ull);
}

/**
  * @brief  将策略 egress 映射为 status JSON 的 network 字段。
  * @note   wifi 出口对外写作 "esp01"（手册用语），策略枚举仍是 wifi。
  * @param  e  活动出口。
  * @retval 静态字符串 rj45|esp01|none。
  */
static const char *net_name(vg_egress_t e)
{
  if (e == VG_EGRESS_RJ45)
    {
      return "rj45";
    }

  if (e == VG_EGRESS_WIFI)
    {
      return "esp01";
    }

  return "none";
}

/**
  * @brief  把 join 结果写入策略，并调度下次 join 时间。
  * @param  ok  true=关联成功（5s 后再采样重试窗口）；false=按退避重试。
  * @retval None
  */
static void note_wifi_result(bool ok)
{
  vg_net_policy_note_wifi_join(&g_policy, ok);
  if (ok)
    {
      g_policy.next_wifi_join_ms = now_ms() + 5000;
    }
  else
    {
      g_policy.next_wifi_join_ms =
        now_ms() + vg_net_policy_backoff_ms(g_policy.wifi_backoff_exp, 0);
    }
}

/**
  * @brief  net_mgr 主循环（detached pthread 入口）。
  * @note   禁止在 LED/NSH 线程里同步跑本循环；join/ping 可能阻塞数秒。
  *         上电先 ESP init+join（热备），再进入 200ms 周期：
  *         sample → policy → reset/join → MQTT 关旧开新 → poll。
  * @param  arg  未使用。
  * @retval NULL（永不返回）。
  */
static void *vg_net_thread(void *arg)
{
  struct vg_eth_sample eth;
  struct vg_esp_sample esp;
  struct vg_net_sample sample;
  uint64_t t;
  bool do_ping;

  (void)arg;
  vg_net_policy_init(&g_policy);
  g_policy.jitter_pct = 20;
  snprintf(g_reason, sizeof(g_reason), "boot");

  /* Boot: initialize ESP AT and attempt hot-standby AP join */

  if (vg_esp_init() == 0)
    {
      if (vg_esp_join() == 0)
        {
          note_wifi_result(true);
        }
      else
        {
          note_wifi_result(false);
        }
    }

  for (; ; )
    {
      t = now_ms();
      do_ping = (t >= g_policy.next_rj45_probe_ms);

      /* Sample bearers; ping eth0 only when policy schedule allows */

      vg_eth_sample(&eth, do_ping);
      vg_esp_sample(&esp);

      /* Publish the samples for vg_net_mgr_status() readers */

      pthread_mutex_lock(&g_lock);
      g_eth_last = eth;
      g_esp_last = esp;
      pthread_mutex_unlock(&g_lock);

      memset(&sample, 0, sizeof(sample));
      sample.rj45_link         = eth.link;
      sample.rj45_has_ip       = eth.has_ip;
      sample.rj45_ping_ok      = eth.ping_ok;
      sample.rj45_ping_sampled = do_ping && eth.link && eth.has_ip;
      sample.wifi_assoc        = esp.assoc;
      sample.wifi_has_ip       = esp.has_ip;
//26821 读到这
      pthread_mutex_lock(&g_lock);
      vg_net_policy_step(&g_policy, t, &sample);

      /* Soft-reset ESP when join failures hit policy threshold */

      if (g_policy.request_esp_reset)
        {
          g_policy.request_esp_reset = false;
          pthread_mutex_unlock(&g_lock);
          printf("vgnet: esp reset\n");
          vg_esp_soft_reset();
          usleep(2000000);
          if (vg_esp_join() == 0)
            {
              pthread_mutex_lock(&g_lock);
              note_wifi_result(true);
            }
          else
            {
              pthread_mutex_lock(&g_lock);
              note_wifi_result(false);
            }
        }

      /* Retry join while disassociated and backoff elapsed */

      if (!esp.assoc && vg_esp_ssid()[0] != '\0' &&
          t >= g_policy.next_wifi_join_ms)
        {
          pthread_mutex_unlock(&g_lock);
          if (vg_esp_join() == 0)
            {
              pthread_mutex_lock(&g_lock);
              note_wifi_result(true);
            }
          else
            {
              pthread_mutex_lock(&g_lock);
              note_wifi_result(false);
            }
        }

      /* Single MQTT TCP: close old then open on the active backend */

      if (g_policy.tcp_reconnect ||
          (g_policy.tcp_backend != VG_TCP_NONE &&
           !vg_mqtt_session_online() && t >= g_next_mqtt_ms))
        {
          vg_tcp_backend_t backend = g_policy.tcp_backend;
          vg_egress_t egress = g_policy.active_egress;

          snprintf(g_reason, sizeof(g_reason), "egress=%s",
                   vg_egress_str(egress));
          pthread_mutex_unlock(&g_lock);
          vg_mqtt_session_close();
          if (backend != VG_TCP_NONE)
            {
              if (vg_mqtt_session_open(backend, net_name(egress)) != 0)
                {
                  g_next_mqtt_ms = now_ms() + 2000;
                }
              else
                {
                  g_next_mqtt_ms = 0;
                }
            }

          pthread_mutex_lock(&g_lock);
        }

      pthread_mutex_unlock(&g_lock);
      vg_mqtt_session_poll();
      usleep(200000);
    }

  return NULL;
}

/**
  * @brief  启动 net_mgr 后台线程（幂等）。
  * @note   attr 只是 create 时的临时配置，create 成功后即可 destroy；
  *         detach 表示无人 join，线程退出时自行回收，并非取消创建。
  *         由 velaguard_app_main 调用；不依赖 NSH。
  * @retval 0   已启动或早已启动。
  * @retval -1  pthread_create 失败。
  */
int vg_net_mgr_start(void)
{
  pthread_t tid;
  pthread_attr_t attr;

  if (g_started)
    {
      return 0;
    }

  /* 8 KiB stack: join/ping/MQTT sync need headroom beyond default */

  pthread_attr_init(&attr);
  pthread_attr_setstacksize(&attr, 8192);
  if (pthread_create(&tid, &attr, vg_net_thread, NULL) != 0)
    {
      pthread_attr_destroy(&attr);
      printf("vgnet: thread create failed\n");
      return -1;
    }

  pthread_attr_destroy(&attr);

  pthread_detach(tid);
  g_started = true;
  printf("vgnet: manager started\n");
  return 0;
}

/**
  * @brief  读取最近一拍的网络状态快照。
  * @note   只读 net_mgr 线程缓存的采样与策略字段，不做 IO、不碰 AT
  *         UART，HMI 周期任务可安全调用。net_mgr 未启动时全为默认值。
  * @param  out  输出；不可为 NULL。
  * @retval 0    成功。
  * @retval -1   参数非法。
  */
int vg_net_mgr_status(struct vg_net_live_status *out)
{
  if (out == NULL)
    {
      return -1;
    }

  memset(out, 0, sizeof(*out));

  pthread_mutex_lock(&g_lock);
  out->rj45_link    = g_eth_last.link;
  out->rj45_has_ip  = g_eth_last.has_ip;
  out->rj45_ping_ok = g_eth_last.ping_ok;
  out->wifi_assoc   = g_esp_last.assoc;
  out->wifi_has_ip  = g_esp_last.has_ip;
  out->mqtt_online  = vg_mqtt_session_online();
  out->egress       = g_policy.active_egress;
  out->state        = g_policy.state;

  if (g_eth_last.ip[0] != '\0')
    {
      snprintf(out->ip, sizeof(out->ip), "%s", g_eth_last.ip);
    }
  else if (g_esp_last.ip[0] != '\0')
    {
      snprintf(out->ip, sizeof(out->ip), "%s", g_esp_last.ip);
    }
  pthread_mutex_unlock(&g_lock);

  return 0;
}

/**
  * @brief  打印当前策略与承载状态到 out。
  * @note   出口已选定但 MQTT 未 CONNACK 时，对外 state 显示为 connecting。
  *         eth 采样 do_ping=false，避免 status 命令触发额外 ICMP。
  * @param  out  输出流，通常为 stdout。
  * @retval None
  */
void vg_net_mgr_print_status(FILE *out)
{
  struct vg_eth_sample eth;
  struct vg_esp_sample esp;
  vg_net_state_t st;

  vg_eth_sample(&eth, false);
  vg_esp_sample(&esp);

  pthread_mutex_lock(&g_lock);
  st = g_policy.state;
  if ((g_policy.active_egress == VG_EGRESS_RJ45 ||
       g_policy.active_egress == VG_EGRESS_WIFI) &&
      !vg_mqtt_session_online())
    {
      st = VG_NET_CONNECTING;
    }

  fprintf(out,
          "state=%s egress=%s tcp=%s mqtt=%s reason=%s\n"
          "rj45 link=%d ip=%s gw=%s ping_ok=%d fail_streak=%u\n"
          "wifi ssid=%s assoc=%d ip=%s\n",
          vg_net_state_str(st),
          vg_egress_str(g_policy.active_egress),
          vg_tcp_backend_str(g_policy.tcp_backend),
          vg_mqtt_session_online() ? "up" : "down",
          g_reason,
          eth.link, eth.ip[0] ? eth.ip : "-",
          eth.gw[0] ? eth.gw : "-", eth.ping_ok,
          g_policy.rj45_ping_fail_streak,
          vg_esp_ssid(), esp.assoc, esp.ip[0] ? esp.ip : "-");
  pthread_mutex_unlock(&g_lock);
}

/**
  * @brief  注入 bearer 健康覆盖，用于无物理拔线时验收切换。
  * @param  bearer  "rj45" 或 "wifi"。
  * @param  mode    "down" | "up" | "auto"。
  * @retval 0   成功。
  * @retval -1  参数非法。
  */
int vg_net_mgr_inject(const char *bearer, const char *mode)
{
  vg_inject_t inj;

  if (strcmp(mode, "down") == 0)
    {
      inj = VG_INJECT_FORCE_DOWN;
    }
  else if (strcmp(mode, "up") == 0)
    {
      inj = VG_INJECT_FORCE_UP;
    }
  else if (strcmp(mode, "auto") == 0)
    {
      inj = VG_INJECT_AUTO;
    }
  else
    {
      return -1;
    }

  pthread_mutex_lock(&g_lock);
  if (strcmp(bearer, "rj45") == 0)
    {
      g_policy.inject_rj45 = inj;
    }
  else if (strcmp(bearer, "wifi") == 0)
    {
      g_policy.inject_wifi = inj;
    }
  else
    {
      pthread_mutex_unlock(&g_lock);
      return -1;
    }

  pthread_mutex_unlock(&g_lock);
  return 0;
}

/**
  * @brief  覆盖 RAM 中的 Wi-Fi 凭据；下次 join 重试时生效。
  * @param  ssid  AP SSID。
  * @param  psk   AP 密码。
  * @retval 0     成功。
  * @retval 负值  参数非法（见 vg_esp_set_wifi）。
  */
int vg_net_mgr_set_wifi(const char *ssid, const char *psk)
{
  return vg_esp_set_wifi(ssid, psk);
}
