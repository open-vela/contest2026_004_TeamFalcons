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

/**
  * @brief  为已连接套接字设置收/发超时（约 2s）。
  * @note   lesp 路径只设 SO_RCVTIMEO 且须持 AT 锁；POSIX 同时设 RCV/SND。
  * @param  fd    原始套接字（无 TAG）。
  * @param  lesp  true=走 lesp_setsockopt。
  * @retval None
  */
static void set_timeo(int fd, bool lesp)
{
  struct timeval tv;
  /* lesp recv blocks until rcv_timeo on empty FIFO; keep short so
   * mqtt_sync (recv-before-send) can poll CONNACK without stalling. */
  tv.tv_sec  = lesp ? 0 : 2;
  tv.tv_usec = lesp ? 500000 : 0;
#ifdef CONFIG_NETUTILS_ESP8266
  if (lesp)
    {
      vg_esp_at_lock();
      lesp_setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
      vg_esp_at_unlock();
      return;
    }
#endif

  /* POSIX path: apply both receive and send timeouts */

  setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
  setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
}

/**
  * @brief  经内核 TCP（eth0）连接到 host:port。
  * @note   遍历 getaddrinfo 结果直至 connect 成功；成功后设超时。
  * @param  host  Broker 主机名或 IP。
  * @param  port  端口十进制字符串。
  * @retval >=0  POSIX fd。
  * @retval -1   解析或连接失败。
  */
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

  /* Try each resolved address until one connects */

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
/**
  * @brief  经 ESP AT（lesp_*）连接到 host:port。
  * @note   返回值或上 VG_MQTT_LESP_TAG，供 MQTT-C pal hook 识别。
  *         DNS 与 connect 全程持 AT 锁。
  * @param  host  Broker 主机名或 IP。
  * @param  port  端口十进制字符串。
  * @retval >=0  带 TAG 的 lesp 句柄。
  * @retval -1   DNS / socket / connect 失败。
  */
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

  /* Tag so mqtt_pal weak hooks divert send/recv to lesp_* */

  return fd | VG_MQTT_LESP_TAG;
}
#endif

/**
  * @brief  按 backend 建立到 host:port 的 TCP 连接。
  * @note   同时只应有一条活动连接；切换出口时须先 vg_tcp_close。
  *         VG_TCP_NONE 或未知 backend 返回 -1。
  * @param  backend  VG_TCP_POSIX 或 VG_TCP_LESP。
  * @param  host     Broker 主机名或 IP。
  * @param  port     端口十进制字符串。
  * @retval >=0  句柄；lesp 路径会或上 VG_MQTT_LESP_TAG。
  * @retval -1   参数非法或建连失败。
  */
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

/**
  * @brief  关闭连接并把 *fd 置为 -1。
  * @note   按 TAG 选择 close 或 lesp_closesocket；lesp 路径持 AT 锁。
  * @param  fd  指向句柄的指针；NULL 或 *fd<0 时无操作。
  * @retval None
  */
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

/**
  * @brief  MQTT-C pal weak hook：带 LESP TAG 时走 lesp_send。
  * @note   返回 -1 表示本 hook 不处理，调用方继续默认 POSIX send。
  *         返回 0 表示已处理（成功写 *out=字节数，失败写 MQTT_ERROR_SOCKET_ERROR）。
  * @param  fd     mqtt_pal_socket_handle（可能带 TAG）。
  * @param  buf    发送缓冲。
  * @param  len    字节数。
  * @param  flags  传给 lesp_send。
  * @param  out    输出已发送字节或错误码。
  * @retval 0   本 hook 已处理。
  * @retval -1  非 lesp fd（或 out==NULL）。
  */
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

  /* Drain the full buffer under the AT lock */

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

/**
  * @brief  MQTT-C pal weak hook：带 LESP TAG 时走 lesp_recv。
  * @note   语义同 vg_mqtt_pal_try_sendall。
  * @param  fd     mqtt_pal_socket_handle（可能带 TAG）。
  * @param  buf    接收缓冲。
  * @param  bufsz  缓冲大小。
  * @param  flags  传给 lesp_recv。
  * @param  out    输出已收字节或错误码。
  * @retval 0   本 hook 已处理。
  * @retval -1  非 lesp fd。
  */
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
      /* lesp_recv uses -1 for rcv_timeo expiry too; MQTT-C mqtt_sync recv's
       * before send, so an empty FIFO on the first poll must not abort CONNECT.
       * Match POSIX mqtt_pal_recvall: timeout => 0 bytes, not socket error. */
      if (errno == ETIMEDOUT || errno == EAGAIN || errno == EWOULDBLOCK)
        {
          *out = 0;
          return 0;
        }

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
