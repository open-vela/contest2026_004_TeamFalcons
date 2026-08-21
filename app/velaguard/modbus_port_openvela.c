/****************************************************************************
 * app/velaguard/modbus_port_openvela.c
 *
 * nanoMODBUS 串口适配层：把 /dev/rs485 接到 nmbs_platform_conf 的
 * read/write/flush。不含任何 Modbus 协议逻辑。
 *
 * 传输契约（nanoMODBUS v1.23.0）：
 *   - 阻塞到收满/发完 count 字节，或 byte_timeout_ms 到期；
 *   - byte_timeout_ms < 0 无限等待，== 0 非阻塞试一次；
 *   - 返回实际字节数；< 0 为传输错误；[0, count-1] 视为超时。
 *
 * DE/RE 由 NuttX UART7 RS485 驱动自动切换。tcdrain 之后补一段短延时，
 * 规避已知问题：uart_tcdrain 不等移位寄存器发完（见
 * docs/velaguard-bringup-known-issues.md §4）。
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdbool.h>
#include <stdint.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#include "modbus_port_openvela.h"

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static int32_t vg_modbus_now_ms(void)
{
  struct timespec ts;

  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (int32_t)(ts.tv_sec * 1000L + ts.tv_nsec / 1000000L);
}

/* 9600 8N1 下一个字符约 1.15ms；3.5 字符时间 ≈ 4ms。取 5ms 覆盖
 * tcdrain 未等 TC 的尾巴，同时满足 RTU 帧间隔。
 */

#define VG_MODBUS_TX_TAIL_US 5000

static int32_t vg_modbus_port_read(uint8_t *buf, uint16_t count,
                                   int32_t byte_timeout_ms, void *arg)
{
  FAR struct vg_modbus_port_s *port = arg;
  uint16_t got = 0;
  int32_t deadline = 0;
  bool infinite = (byte_timeout_ms < 0);

  if (port == NULL || port->fd < 0 || buf == NULL)
    {
      return -1;
    }

  if (count == 0)
    {
      return 0;
    }

  if (!infinite)
    {
      deadline = vg_modbus_now_ms() + byte_timeout_ms;
    }

  while (got < count)
    {
      struct pollfd pfd;
      int remain;
      int ret;
      ssize_t n;

      if (infinite)
        {
          remain = 1000;
        }
      else
        {
          remain = (int)(deadline - vg_modbus_now_ms());
          if (remain < 0)
            {
              remain = 0;
            }
        }

      pfd.fd      = port->fd;
      pfd.events  = POLLIN;
      pfd.revents = 0;

      ret = poll(&pfd, 1, remain);
      if (ret < 0)
        {
          if (errno == EINTR)
            {
              continue;
            }

          return -1;
        }

      if (ret == 0)
        {
          if (infinite)
            {
              continue;
            }

          return got;
        }

      n = read(port->fd, buf + got, count - got);
      if (n > 0)
        {
          got += (uint16_t)n;
          continue;
        }

      if (n == 0)
        {
          return got;
        }

      if (errno == EAGAIN || errno == EINTR)
        {
          continue;
        }

      return -1;
    }

  return got;
}

static int32_t vg_modbus_port_write(const uint8_t *buf, uint16_t count,
                                    int32_t byte_timeout_ms, void *arg)
{
  FAR struct vg_modbus_port_s *port = arg;
  uint16_t sent = 0;
  int32_t deadline = 0;
  bool infinite = (byte_timeout_ms < 0);

  if (port == NULL || port->fd < 0 || buf == NULL)
    {
      return -1;
    }

  if (count == 0)
    {
      return 0;
    }

  if (!infinite)
    {
      deadline = vg_modbus_now_ms() + byte_timeout_ms;
    }

  while (sent < count)
    {
      struct pollfd pfd;
      int remain;
      int ret;
      ssize_t n;

      if (infinite)
        {
          remain = 1000;
        }
      else
        {
          remain = (int)(deadline - vg_modbus_now_ms());
          if (remain < 0)
            {
              remain = 0;
            }
        }

      pfd.fd      = port->fd;
      pfd.events  = POLLOUT;
      pfd.revents = 0;

      ret = poll(&pfd, 1, remain);
      if (ret < 0)
        {
          if (errno == EINTR)
            {
              continue;
            }

          return -1;
        }

      if (ret == 0)
        {
          if (infinite)
            {
              continue;
            }

          return sent;
        }

      n = write(port->fd, buf + sent, count - sent);
      if (n > 0)
        {
          sent += (uint16_t)n;
          continue;
        }

      if (n == 0)
        {
          return sent;
        }

      if (errno == EAGAIN || errno == EINTR)
        {
          continue;
        }

      return -1;
    }

  if (tcdrain(port->fd) < 0)
    {
      return -1;
    }

  usleep(VG_MODBUS_TX_TAIL_US);
  return sent;
}

static void vg_modbus_port_flush(nmbs_t *nmbs, void *arg)
{
  FAR struct vg_modbus_port_s *port = arg;
  uint8_t tmp[32];

  (void)nmbs;

  if (port == NULL || port->fd < 0)
    {
      return;
    }

  for (; ; )
    {
      struct pollfd pfd;
      ssize_t n;

      pfd.fd      = port->fd;
      pfd.events  = POLLIN;
      pfd.revents = 0;

      if (poll(&pfd, 1, 0) <= 0)
        {
          break;
        }

      n = read(port->fd, tmp, sizeof(tmp));
      if (n <= 0)
        {
          break;
        }
    }
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int vg_modbus_port_open(FAR struct vg_modbus_port_s *port,
                        FAR const char *devpath)
{
  struct termios tio;
  int fd;

  if (port == NULL || devpath == NULL)
    {
      return -1;
    }

  fd = open(devpath, O_RDWR);
  if (fd < 0)
    {
      return -1;
    }

  if (tcgetattr(fd, &tio) < 0)
    {
      close(fd);
      return -1;
    }

  cfmakeraw(&tio);
  cfsetispeed(&tio, B9600);
  cfsetospeed(&tio, B9600);
  tio.c_cflag |= CLOCAL | CREAD;

  if (tcsetattr(fd, TCSANOW, &tio) < 0)
    {
      close(fd);
      return -1;
    }

  tcflush(fd, TCIOFLUSH);
  port->fd = fd;
  return 0;
}

void vg_modbus_port_close(FAR struct vg_modbus_port_s *port)
{
  if (port == NULL || port->fd < 0)
    {
      return;
    }

  close(port->fd);
  port->fd = -1;
}

void vg_modbus_port_bind(FAR struct vg_modbus_port_s *port,
                         FAR nmbs_platform_conf *pc)
{
  nmbs_platform_conf_create(pc);
  pc->transport = NMBS_TRANSPORT_RTU;
  pc->read      = vg_modbus_port_read;
  pc->write     = vg_modbus_port_write;
  pc->flush     = vg_modbus_port_flush;
  pc->arg       = port;
}
