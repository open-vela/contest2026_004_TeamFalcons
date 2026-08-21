/****************************************************************************
 * app/velaguard/vg_tcp_transport.c
 *
 * 单活动 TCP：POSIX 或 lesp_*（句柄或上 VG_MQTT_LESP_TAG）。
 * 提供 MQTT-C pal weak hook。API 见 vg_tcp_transport.h。
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#ifdef CONFIG_NETUTILS_ESP8266
#include "netutils/esp8266.h"
#endif

#include <mqtt.h>
#include "vg_esp_bearer.h"
#include "vg_tcp_transport.h"

static void set_timeo(int fd, bool lesp)
{
  struct timeval tv;
  tv.tv_sec  = 2;
  tv.tv_usec = 0;
#ifdef CONFIG_NETUTILS_ESP8266
  if (lesp)
    {
      vg_esp_at_lock();
      lesp_setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
      vg_esp_at_unlock();
      return;
    }
#endif
  setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
  setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
}

static int posix_connect(const char *host, const char *port)
{
  struct addrinfo hints;
  struct addrinfo *res;
  struct addrinfo *it;
  int fd = -1;
  int ret;

  memset(&hints, 0, sizeof(hints));
  hints.ai_family   = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  ret = getaddrinfo(host, port, &hints, &res);
  if (ret != 0)
    {
      return -1;
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
      fd = -1;
    }

  freeaddrinfo(res);
  if (fd >= 0)
    {
      set_timeo(fd, false);
    }

  return fd;
}

#ifdef CONFIG_NETUTILS_ESP8266
static int lesp_connect_host(const char *host, const char *port)
{
  struct sockaddr_in addr;
  struct hostent *he;
  int fd;
  int p;

  p = atoi(port);
  vg_esp_at_lock();
  he = lesp_gethostbyname(host);
  if (he == NULL || he->h_addr_list == NULL || he->h_addr_list[0] == NULL)
    {
      vg_esp_at_unlock();
      return -1;
    }

  fd = lesp_socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (fd < 0)
    {
      vg_esp_at_unlock();
      return -1;
    }

  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_port   = htons((uint16_t)p);
  memcpy(&addr.sin_addr, he->h_addr_list[0], sizeof(addr.sin_addr));
  if (lesp_connect(fd, (FAR struct sockaddr *)&addr, sizeof(addr)) < 0)
    {
      lesp_closesocket(fd);
      vg_esp_at_unlock();
      return -1;
    }

  vg_esp_at_unlock();
  set_timeo(fd, true);
  return fd | VG_MQTT_LESP_TAG;
}
#endif

int vg_tcp_open(vg_tcp_backend_t backend, const char *host, const char *port)
{
  if (host == NULL || port == NULL)
    {
      return -1;
    }

  if (backend == VG_TCP_POSIX)
    {
      return posix_connect(host, port);
    }

#ifdef CONFIG_NETUTILS_ESP8266
  if (backend == VG_TCP_LESP)
    {
      return lesp_connect_host(host, port);
    }
#endif

  return -1;
}

void vg_tcp_close(int *fd)
{
  int raw;

  if (fd == NULL || *fd < 0)
    {
      return;
    }

  raw = *fd;
#ifdef CONFIG_NETUTILS_ESP8266
  if (raw & VG_MQTT_LESP_TAG)
    {
      vg_esp_at_lock();
      lesp_closesocket(raw & ~VG_MQTT_LESP_TAG);
      vg_esp_at_unlock();
      *fd = -1;
      return;
    }
#endif

  close(raw);
  *fd = -1;
}

int vg_mqtt_pal_try_sendall(mqtt_pal_socket_handle fd, const void *buf,
                            size_t len, int flags, ssize_t *out)
{
#ifdef CONFIG_NETUTILS_ESP8266
  const uint8_t *p = (const uint8_t *)buf;
  size_t sent = 0;

  if (out == NULL || (fd & VG_MQTT_LESP_TAG) == 0)
    {
      return -1;
    }

  vg_esp_at_lock();
  while (sent < len)
    {
      ssize_t n = lesp_send(fd & ~VG_MQTT_LESP_TAG, p + sent, len - sent,
                            flags);
      if (n < 1)
        {
          vg_esp_at_unlock();
          *out = MQTT_ERROR_SOCKET_ERROR;
          return 0;
        }

      sent += (size_t)n;
    }

  vg_esp_at_unlock();

  *out = (ssize_t)sent;
  return 0;
#else
  (void)fd;
  (void)buf;
  (void)len;
  (void)flags;
  (void)out;
  return -1;
#endif
}

int vg_mqtt_pal_try_recvall(mqtt_pal_socket_handle fd, void *buf,
                            size_t bufsz, int flags, ssize_t *out)
{
#ifdef CONFIG_NETUTILS_ESP8266
  ssize_t n;

  if (out == NULL || (fd & VG_MQTT_LESP_TAG) == 0)
    {
      return -1;
    }

  vg_esp_at_lock();
  n = lesp_recv(fd & ~VG_MQTT_LESP_TAG, (FAR uint8_t *)buf, bufsz, flags);
  vg_esp_at_unlock();
  if (n < 0)
    {
      *out = MQTT_ERROR_SOCKET_ERROR;
      return 0;
    }

  *out = n;
  return 0;
#else
  (void)fd;
  (void)buf;
  (void)bufsz;
  (void)flags;
  (void)out;
  return -1;
#endif
}
