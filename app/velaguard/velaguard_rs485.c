/****************************************************************************
 * app/velaguard/velaguard_rs485.c
 *
 * vgrs485 - RS485 自测工具
 *   vgrs485 tx [dev]  : 发送 26 字节字母表
 *   vgrs485 rx [dev]  : 接收 26 字节并校验内容
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

#define DEFAULT_DEV "/dev/rs485"
#define PAYLOAD "abcdefghijklmnopqrstuvwxyz" /* 26 字节 */
#define PAYLOAD_LEN 26

/****************************************************************************
 * 私有函数
 ****************************************************************************/

static int do_tx(FAR const char *dev)
{
    char buf[PAYLOAD_LEN];
    ssize_t n;
    int fd;
    int total = 0;

    memcpy(buf, PAYLOAD, PAYLOAD_LEN);

    fd = open(dev, O_RDWR);
    if (fd < 0)
    {
        fprintf(stderr, "vgrs485: open %s failed: %d\n", dev, errno);
        return 1;
    }

    /* write() 一次可能只接受部分字节，循环直到 26 字节全部交给驱动。
     */

    while (total < PAYLOAD_LEN)
    {
        n = write(fd, buf + total, PAYLOAD_LEN - total);
        if (n < 0)
        {
            fprintf(stderr, "vgrs485: write failed: %d\n", errno);
            close(fd);
            return 1;
        }

        total += n;
    }

    /* write() 只是把数据交给驱动队列，tcdrain() 会阻塞到字节真正从
     * UART 发出去，这样 close() 不会丢掉还没发完的尾巴。
     */

    tcdrain(fd);
    usleep(50*1000);
    printf("vgrs485: sent %d bytes: %s\n", total, buf);
    close(fd);
    return 0;
}

static int do_rx(FAR const char *dev)
{
    char buf[PAYLOAD_LEN];
    ssize_t n;
    int fd;
    int total = 0;

    fd = open(dev, O_RDWR);
    if (fd < 0)
    {
        fprintf(stderr, "vgrs485: open %s failed: %d\n", dev, errno);
        return 1;
    }

    printf("vgrs485: waiting for %d bytes on %s...\n", PAYLOAD_LEN, dev);

    /* 串口 read() 返回"当前能拿到的字节"，可能少于请求长度，
     * 所以循环往 buf 的下一个位置接着读，直到凑满 PAYLOAD_LEN 字节。
     */

    while (total < PAYLOAD_LEN)
    {
        n = read(fd, buf + total, PAYLOAD_LEN - total);
        if (n < 0)
        {
            fprintf(stderr, "vgrs485: read failed: %d\n", errno);
            close(fd);
            return 1;
        }

        total += n;
    }

    if (memcmp(buf, PAYLOAD, PAYLOAD_LEN) == 0)
    {
        printf("vgrs485: PASS - payload matched\n");
        close(fd);
        return 0;
    }

    printf("vgrs485: FAIL - mismatch, received:\n");
    for (int i = 0; i < PAYLOAD_LEN; i++)
    {
        printf("%02x ", (unsigned char)buf[i]);
    }

    printf("\n");
    close(fd);
    return 1;
}

/****************************************************************************
 * 公共函数
 ****************************************************************************/

int main(int argc, FAR char *argv[])
{
    FAR const char *dev = DEFAULT_DEV;

    if (argc < 2)
    {
        fprintf(stderr, "Usage: vgrs485 <tx|rx> [devpath]\n");
        return 1;
    }

    if (argc > 2)
    {
        dev = argv[2];
    }

    if (strcmp(argv[1], "tx") == 0)
    {
        return do_tx(dev);
    }
    else if (strcmp(argv[1], "rx") == 0)
    {
        return do_rx(dev);
    }

    fprintf(stderr, "vgrs485: unknown subcommand '%s'\n", argv[1]);
    return 1;
}
