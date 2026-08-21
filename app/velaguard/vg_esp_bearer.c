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

void vg_esp_at_lock(void)
{
  pthread_mutex_lock(&g_esp_lock);
}

void vg_esp_at_unlock(void)
{
  pthread_mutex_unlock(&g_esp_lock);
}

void vg_esp_cred_init(void)
{
  if (g_ssid[0] == '\0')
    {
      strlcpy(g_ssid, CONFIG_VG_WIFI_SSID, sizeof(g_ssid));
      strlcpy(g_psk, CONFIG_VG_WIFI_PASSWORD, sizeof(g_psk));
    }
}

int vg_esp_set_wifi(FAR const char *ssid, FAR const char *psk)
{
  if (ssid == NULL || psk == NULL || ssid[0] == '\0')
    {
      return -EINVAL;
    }

  strlcpy(g_ssid, ssid, sizeof(g_ssid));
  strlcpy(g_psk, psk, sizeof(g_psk));
  return 0;
}

bool vg_esp_uart_busy(void)
{
  return g_esp_busy;
}

#ifdef CONFIG_NETUTILS_ESP8266
static int vg_esp_init_locked(void)
{
  int ret;

  vg_esp_cred_init();
  if (g_esp_inited)
    {
      return 0;
    }

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

  ret = lesp_soft_reset();
  vg_esp_at_unlock();
  return ret;
#else
  return -ENOTSUP;
#endif
}

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

FAR const char *vg_esp_ssid(void)
{
  vg_esp_cred_init();
  return g_ssid;
}
