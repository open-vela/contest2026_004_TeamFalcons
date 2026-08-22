/****************************************************************************
 * app/velaguard/vg_esp_bearer.c
 *
 * ESP-01S AT 承载（lesp_*）。上电 join 作热备；与 vgesp 互斥占用 UART。
 * API 见 vg_esp_bearer.h。
 ****************************************************************************/

#include <nuttx/config.h>

#include <arpa/inet.h>
#include <errno.h>
#include <pthread.h>
#include <string.h>
#include <stdio.h>

#ifdef CONFIG_NETUTILS_ESP8266
#include "netutils/esp8266.h"
#endif

#include "vg_esp_bearer.h"

#ifndef CONFIG_VG_WIFI_SSID
#  define CONFIG_VG_WIFI_SSID "xxx"
#endif
#ifndef CONFIG_VG_WIFI_PASSWORD
#  define CONFIG_VG_WIFI_PASSWORD "1472583690"
#endif

static pthread_mutex_t g_esp_lock = PTHREAD_MUTEX_INITIALIZER;
static bool g_esp_inited;
static bool g_esp_busy;
static char g_ssid[33];
static char g_psk[65];

/**
  * @brief  获取 AT UART 互斥锁。
  * @note   lesp_*、sample、soft_reset 与 status 路径共享此锁，
  *         防止 net_mgr 与并发 AT 调用交错。
  * @retval None
  */
void vg_esp_at_lock(void)
{
  /* Serialize all ESP-01S AT traffic on /dev/ttyS1 */

  pthread_mutex_lock(&g_esp_lock);
}

/**
  * @brief  释放 AT UART 互斥锁。
  * @retval None
  */
void vg_esp_at_unlock(void)
{
  pthread_mutex_unlock(&g_esp_lock);
}

/**
  * @brief  若 RAM 凭据为空，填入 Kconfig 默认 SSID/PSK。
  * @note   仅首次（ssid 空）写入；vg_esp_set_wifi / vgnet wifi 覆盖后不再回填。
  * @retval None
  */
void vg_esp_cred_init(void)
{
  /* Lazy-load compile-time defaults into mutable RAM credentials */

  if (g_ssid[0] == '\0')
    {
      strlcpy(g_ssid, CONFIG_VG_WIFI_SSID, sizeof(g_ssid));
      strlcpy(g_psk, CONFIG_VG_WIFI_PASSWORD, sizeof(g_psk));
    }
}

/**
  * @brief  覆盖 RAM 中的 Wi-Fi 凭据。
  * @note   立即生效于后续 vg_esp_join；不主动断联当前关联。
  * @param  ssid  非空 SSID。
  * @param  psk   密码缓冲（可为空串，视 AP 而定）；不可为 NULL。
  * @retval 0         成功。
  * @retval -EINVAL   参数非法。
  */
int vg_esp_set_wifi(FAR const char *ssid, FAR const char *psk)
{
  if (ssid == NULL || psk == NULL || ssid[0] == '\0')
    {
      return -EINVAL;
    }

  /* Replace in-RAM credentials used by the next join attempt */

  strlcpy(g_ssid, ssid, sizeof(g_ssid));
  strlcpy(g_psk, psk, sizeof(g_psk));
  return 0;
}

/**
  * @brief  查询 net_mgr 是否已占用 ESP UART。
  * @note   vgesp 在 busy 时应拒绝，避免与 lesp worker 抢 /dev/ttyS1。
  * @retval true   已 initialize，UART 归 net_mgr。
  * @retval false  空闲（或未开 NETUTILS_ESP8266）。
  */
bool vg_esp_uart_busy(void)
{
  return g_esp_busy;
}

#ifdef CONFIG_NETUTILS_ESP8266
/**
  * @brief  在已持锁前提下执行 lesp_initialize（幂等）。
  * @note   成功后置 g_esp_busy / g_esp_inited；失败清除 busy。
  * @retval 0     成功或已初始化。
  * @retval 负值  lesp_initialize 失败。
  */
static int vg_esp_init_locked(void)
{
  int ret;

  vg_esp_cred_init();
  if (g_esp_inited)
    {
      return 0;
    }

  /* Mark UART owned before AT init so vgesp can refuse race */

  g_esp_busy = true;
  ret = lesp_initialize();
  if (ret < 0)
    {
      g_esp_busy = false;
      printf("vgnet: lesp_initialize failed\n");
      return ret;
    }

  g_esp_inited = true;
  return 0;
}
#endif

/**
  * @brief  初始化 ESP AT 栈（lesp_initialize）。
  * @note   上电热备路径由 net_mgr 调用；未配置 NETUTILS_ESP8266 时返回 -ENOTSUP。
  * @retval 0       成功或已初始化。
  * @retval -ENOTSUP  未启用 ESP8266 netutils。
  * @retval 负值    lesp 失败。
  */
int vg_esp_init(void)
{
#ifdef CONFIG_NETUTILS_ESP8266
  int ret;

  vg_esp_at_lock();
  ret = vg_esp_init_locked();
  vg_esp_at_unlock();
  return ret;
#else
  return -ENOTSUP;
#endif
}

/**
  * @brief  用当前凭据执行 lesp_ap_connect（热备 join）。
  * @note   与 RJ45 是否健康无关：始终尝试保持关联；TCP 仍仅在策略选 wifi 时走 lesp。
  *         超时参数固定 20（lesp API 单位）。
  * @retval 0         关联成功。
  * @retval -EINVAL   SSID 为空。
  * @retval -ENOTSUP  未启用 ESP8266。
  * @retval 负值      init 或 ap_connect 失败。
  */
int vg_esp_join(void)
{
#ifdef CONFIG_NETUTILS_ESP8266
  int ret;

  vg_esp_cred_init();
  if (g_ssid[0] == '\0')
    {
      return -EINVAL;
    }

  vg_esp_at_lock();
  ret = vg_esp_init_locked();
  if (ret < 0)
    {
      vg_esp_at_unlock();
      return ret;
    }

  /* Hot-standby association; TCP egress remains policy-selected */

  g_esp_busy = true;
  ret = lesp_ap_connect(g_ssid, g_psk, 20);
  vg_esp_at_unlock();
  if (ret < 0)
    {
      printf("vgnet: join %s failed\n", g_ssid);
    }
  else
    {
      printf("vgnet: joined %s\n", g_ssid);
    }

  return ret;
#else
  return -ENOTSUP;
#endif
}

/**
  * @brief  ESP 软复位（AT+RST 路径）；未 init 时先 init。
  * @note   由策略 request_esp_reset 触发；复位后调用方应重新 join。
  * @retval 0         成功。
  * @retval -ENOTSUP  未启用 ESP8266。
  * @retval 负值      失败。
  */
int vg_esp_soft_reset(void)
{
#ifdef CONFIG_NETUTILS_ESP8266
  int ret;

  vg_esp_at_lock();
  if (!g_esp_inited)
    {
      ret = vg_esp_init_locked();
      vg_esp_at_unlock();
      return ret;
    }

  /* Soft reset via lesp; hard RST GPIO is optional and not used here */

  ret = lesp_soft_reset();
  vg_esp_at_unlock();
  return ret;
#else
  return -ENOTSUP;
#endif
}

/**
  * @brief  采样 Wi-Fi 关联与 STA IPv4。
  * @note   本树 lesp_ap_is_connected() 无实现；以 CIPSTA 非零 IP 推断 assoc。
  *         未 init 时输出全清零。
  * @param  out  输出；不可为 NULL。
  * @retval None
  */
void vg_esp_sample(struct vg_esp_sample *out)
{
#ifdef CONFIG_NETUTILS_ESP8266
  in_addr_t ip = 0;
  in_addr_t mask = 0;
  in_addr_t gw = 0;

  memset(out, 0, sizeof(*out));
  vg_esp_at_lock();
  if (!g_esp_inited)
    {
      vg_esp_at_unlock();
      return;
    }

  /* Header declares lesp_ap_is_connected(); this NuttX tree does not
   * implement it. Station IP from CIPSTA is the association signal.
   */

  if (lesp_get_net(lesp_eMODE_STATION, &ip, &mask, &gw) == 0 && ip != 0)
    {
      struct in_addr a;
      a.s_addr = ip;
      out->assoc = true;
      out->has_ip = true;
      inet_ntop(AF_INET, &a, out->ip, sizeof(out->ip));
    }

  vg_esp_at_unlock();
#else
  memset(out, 0, sizeof(*out));
#endif
}

/**
  * @brief  返回当前 SSID（内部静态缓冲的只读指针）。
  * @note   必要时先 cred_init；调用方不得 free / 写入。
  * @retval 指向 g_ssid 的指针。
  */
FAR const char *vg_esp_ssid(void)
{
  vg_esp_cred_init();
  return g_ssid;
}
