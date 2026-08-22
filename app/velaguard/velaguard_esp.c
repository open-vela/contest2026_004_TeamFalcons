/****************************************************************************
 * app/velaguard/velaguard_esp.c
 *
 * vgesp - ESP-01S 自测工具
 *   vgesp at              : 发 AT，等 OK（默认 /dev/ttyS1）
 *   vgesp cmd <AT+XXX>    : 发任意 AT 命令并打印响应（如 AT+GMR）
 *   vgesp at /dev/ttySx   : 可指定设备路径
 ****************************************************************************/

#include <nuttx/config.h>

#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <termios.h>
#include <errno.h>

#ifdef CONFIG_VG_NET_FAILOVER
#include "vg_esp_bearer.h"
#endif

#define DEFAULT_DEV   "/dev/ttyS1"   /* USART2 = ESP-01 */
#define RESP_BUF      256            /* 响应缓冲区大小 */
#define READ_LOOP_MS  100            /* 无数据时的轮询间隔 */
#define TIMEOUT_MS    3000           /* 总超时：3 秒 */

/****************************************************************************
 * 私有函数
 ****************************************************************************/

static int do_cmd(FAR const char *dev, FAR const char *cmd)
{
  char buf[RESP_BUF];
  ssize_t n;
  int fd;
  int total = 0;
  int waited = 0;

  /* O_NONBLOCK：read() 没数据时立即返回，配合下面的 sleep 轮询，
   * 这样 ESP 不回应时命令不会永久卡死。
   */

  fd = open(dev, O_RDWR | O_NONBLOCK);
  if (fd < 0)
    {
      fprintf(stderr, "vgesp: open %s failed: %d\n", dev, errno);
      return 1;
    }

  /* 清掉串口里可能残留的旧数据，避免污染响应判断 */

  tcflush(fd, TCIFLUSH);

  /* 发送命令。ESP AT 固件以 \r\n 作为命令结束符，
   * 所以调用方拼好的 cmd 已经带上了 \r\n。
   */

  n = write(fd, cmd, strlen(cmd));
  if (n < 0)
    {
      fprintf(stderr, "vgesp: write failed: %d\n", errno);
      close(fd);
      return 1;
    }

  printf("vgesp: sent %d bytes: %s\n", n, cmd);

  /* 轮询读取响应：收到数据就继续读；连续 3 秒没新数据就收工。
   * 发完命令后 fd 一直开着，串口有充足时间把字节发完，
   * 不会像 RS485 那次在 close 时被切尾巴。
   */

  memset(buf, 0, sizeof(buf));
  while (total < RESP_BUF - 1 && waited < TIMEOUT_MS)
    {
      n = read(fd, buf + total, RESP_BUF - 1 - total);
      if (n > 0)
        {
          total += n;
          waited = 0;
        }
      else
        {
          usleep(READ_LOOP_MS * 1000);
          waited += READ_LOOP_MS;
        }
    }

  buf[total] = '\0';

  printf("vgesp: response (%d bytes):\n%s\n", total, buf);
  close(fd);

  /* AT 固件正常应答里一定带 OK，用它做判定标志 */

  if (strstr(buf, "OK") != NULL)
    {
      printf("vgesp: PASS - got OK\n");
      return 0;
    }

  printf("vgesp: FAIL - no OK in response\n");
  return 1;
}

/****************************************************************************
 * 公共函数
 ****************************************************************************/

/**
  * @brief  vgesp NSH 入口：手工 AT 自测。
  * @note   CONFIG_VG_NET_FAILOVER 时若 net_mgr 已占用 /dev/ttyS1，
  *         直接拒绝，避免与 lesp_* 争用同一 UART。
  * @param  argc  参数个数。
  * @param  argv  at | cmd <AT+...> [devpath]。
  * @retval 0  PASS（响应含 OK）。
  * @retval 1  用法错误、UART busy、或 FAIL。
  */
int main(int argc, FAR char *argv[])
{
#ifdef CONFIG_VG_NET_FAILOVER
  /* Refuse when failover mgr already owns the ESP-01S AT port */

  if (vg_esp_uart_busy())
    {
      fprintf(stderr, "vgesp: ESP UART owned by net_mgr, skip\n");
      return 1;
    }
#endif
  FAR const char *dev = DEFAULT_DEV;

  if (argc < 2)
    {
      fprintf(stderr, "Usage: vgesp <at|cmd <AT+XXX>> [devpath]\n");
      return 1;
    }

  if (strcmp(argv[1], "at") == 0)
    {
      if (argc > 2)
        {
          dev = argv[2];
        }

      return do_cmd(dev, "AT\r\n");
    }
  else if (strcmp(argv[1], "cmd") == 0)
    {
      char full[RESP_BUF];

      if (argc < 3)
        {
          fprintf(stderr, "Usage: vgesp cmd <AT+XXX> [devpath]\n");
          return 1;
        }

      if (argc > 3)
        {
          dev = argv[3];
        }

      snprintf(full, sizeof(full), "%s\r\n", argv[2]);
      return do_cmd(dev, full);
    }

  fprintf(stderr, "vgesp: unknown subcommand '%s'\n", argv[1]);
  return 1;
}
