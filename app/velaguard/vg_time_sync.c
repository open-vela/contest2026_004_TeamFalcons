/****************************************************************************
 * app/velaguard/vg_time_sync.c
 *
 * 板子无电池 RTC，断电时钟归零。本模块：
 *   1. 开机从 eMMC clock.txt 恢复墙钟（误差 = 断电时长）；
 *   2. 每 3 分钟把当前墙钟写回（tmp + rename 原子替换）；
 *   3. RJ45 有 IP 时发 SNTP 查询，成功后把时钟设为北京时间并立即落盘。
 *
 * 系统时钟统一跑北京时间墙钟（UTC+8），不依赖 TZ 数据库；HMI 时钟、
 * 事件日志、日报文件名因此直接是北京时间。
 *
 * SNTP 只走 RJ45 出口（wifi 热备不校时）；报文纯函数见 vg_sntp.c。
 ****************************************************************************/

#include <nuttx/config.h>

#include <arpa/inet.h>
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include "vg_sntp.h"
#include "vg_time_sync.h"

#ifdef CONFIG_VG_NET_FAILOVER
#  include "vg_net_mgr.h"
#endif

#ifndef CONFIG_VG_TIME_SYNC_PATH
#  define CONFIG_VG_TIME_SYNC_PATH "/data/velaguard/clock.txt"
#endif

#define VG_TIME_DIR             "/data/velaguard"

#define VG_TIME_TICK_S          15
#define VG_TIME_PERSIST_S       (3 * 60)
#define VG_TIME_RESYNC_S        (30 * 60)
#define VG_TIME_RETRY_S         60
#define VG_SNTP_TIMEOUT_S       3

/* 阿里 NTP、腾讯 NTP；固定 IP 免去 DNS 依赖 */
static const char * const g_ntp_servers[] =
{
  "203.107.6.1",
  "106.55.184.199",
};

#define VG_NTP_SERVER_N (sizeof(g_ntp_servers) / sizeof(g_ntp_servers[0]))

/**
  * @brief  读取单调时钟秒（调度用，避免墙钟跳变影响间隔）。
  */
static int64_t mono_sec(void)
{
  struct timespec ts;

  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (int64_t)ts.tv_sec;
}

static void wall_to_str(int64_t wall, char *out, size_t n)
{
  time_t t = (time_t)wall;
  struct tm tmv;

  (void)gmtime_r(&t, &tmv);
  (void)strftime(out, n, "%Y-%m-%d %H:%M:%S", &tmv);
}

/**
  * @brief  把北京时间墙钟写入系统时钟并打印。
  */
static void set_wall(int64_t wall, const char *why)
{
  struct timespec ts;
  char buf[32];

  ts.tv_sec = (time_t)wall;
  ts.tv_nsec = 0;
  if (clock_settime(CLOCK_REALTIME, &ts) != 0)
    {
      printf("vgtime: settime failed errno=%d\n", errno);
      return;
    }

  wall_to_str(wall, buf, sizeof(buf));
  printf("vgtime: %s %s\n", why, buf);
}

/**
  * @brief  当前墙钟写回 eMMC；时间不合法（如 1970）则跳过不落盘。
  */
static void persist_now(void)
{
  int64_t wall = (int64_t)time(NULL);
  char tmp[64];
  char buf[32];
  FILE *fp;
  int n;

  if (!vg_time_wall_plausible(wall))
    {
      return;
    }

  snprintf(tmp, sizeof(tmp), "%s.tmp", CONFIG_VG_TIME_SYNC_PATH);
  fp = fopen(tmp, "w");
  if (fp == NULL)
    {
      return;
    }

  n = snprintf(buf, sizeof(buf), "%lld\n", (long long)wall);
  (void)fwrite(buf, 1, (size_t)n, fp);
  (void)fclose(fp);

  if (rename(tmp, CONFIG_VG_TIME_SYNC_PATH) != 0)
    {
      (void)unlink(tmp);
      printf("vgtime: persist failed errno=%d\n", errno);
    }
}

/**
  * @brief  开机恢复：clock.txt 合法则设为系统时钟。
  */
static void restore(void)
{
  FILE *fp;
  char line[32];
  long long wall;

  fp = fopen(CONFIG_VG_TIME_SYNC_PATH, "r");
  if (fp == NULL)
    {
      printf("vgtime: no saved time\n");
      return;
    }

  if (fgets(line, sizeof(line), fp) == NULL)
    {
      (void)fclose(fp);
      printf("vgtime: empty clock file\n");
      return;
    }
  (void)fclose(fp);

  wall = strtoll(line, NULL, 10);
  if (!vg_time_wall_plausible((int64_t)wall))
    {
      printf("vgtime: saved time implausible %lld\n", wall);
      return;
    }

  set_wall((int64_t)wall, "restore");
}

/**
  * @brief  RJ45 出口是否有 IP（SNTP 第一版只走 RJ45）。
  */
static bool rj45_ready(void)
{
#ifdef CONFIG_VG_NET_FAILOVER
  struct vg_net_live_status st;

  if (vg_net_mgr_status(&st) != 0)
    {
      return false;
    }

  return st.rj45_has_ip;
#else
  return false;
#endif
}

/**
  * @brief  向单台 NTP 服务器发一次 SNTP 查询。
  * @retval 0   成功，*utc_out 为 Unix UTC 秒。
  * @retval -1  socket/超时/报文非法。
  */
static int sntp_query(const char *server, int64_t *utc_out)
{
  struct sockaddr_in addr;
  struct timeval tv;
  uint8_t tx[48];
  uint8_t rx[48];
  ssize_t n;
  int fd;
  int rc;

  if (vg_sntp_build_query(tx) != 0)
    {
      return -1;
    }

  fd = socket(AF_INET, SOCK_DGRAM, 0);
  if (fd < 0)
    {
      return -1;
    }

  tv.tv_sec = VG_SNTP_TIMEOUT_S;
  tv.tv_usec = 0;
  (void)setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_port = HTONS(123);
  addr.sin_addr.s_addr = inet_addr(server);

  rc = sendto(fd, tx, sizeof(tx), 0, (struct sockaddr *)&addr, sizeof(addr));
  if (rc < 0)
    {
      (void)close(fd);
      return -1;
    }

  n = recvfrom(fd, rx, sizeof(rx), 0, NULL, NULL);
  (void)close(fd);
  if (n < 48)
    {
      return -1;
    }

  return vg_sntp_parse_response(rx, utc_out);
}

/**
  * @brief  逐台尝试服务器；成功即设北京时间墙钟。
  * @retval 0   校时成功。
  * @retval -1  全部服务器不可达或结果不合法。
  */
static int sync_now(void)
{
  size_t i;

  for (i = 0; i < VG_NTP_SERVER_N; i++)
    {
      int64_t utc;
      int64_t wall;

      if (sntp_query(g_ntp_servers[i], &utc) == 0)
        {
          wall = vg_time_utc_to_wall(utc);
          if (vg_time_wall_plausible(wall))
            {
              set_wall(wall, "synced");
              return 0;
            }
        }
    }

  printf("vgtime: sync failed (servers unreachable)\n");
  return -1;
}

/**
  * @brief  等 eMMC 挂载完成（板级晚挂载，最多 5s）。
  * @note   挂载前写文件会落在被挂载遮住的底层 fs 上，恢复即失效。
  */
static void wait_for_data_mount(void)
{
  struct stat st;
  int i;

  for (i = 0; i < 50; i++)
    {
      if (stat("/data", &st) == 0)
        {
          return;
        }

      usleep(100000);
    }
}

static FAR void *vg_time_sync_thread(FAR void *arg)
{
  int64_t last_ok = 0;
  int64_t last_try = 0;
  int64_t last_persist;
  bool online_prev = false;

  (void)arg;

  wait_for_data_mount();
  (void)mkdir(VG_TIME_DIR, 0777);

  restore();
  persist_now();
  last_persist = mono_sec();

  for (; ; )
    {
      bool online = rj45_ready();
      int64_t now = mono_sec();

      /* 联网沿触发：开机后首次有 IP 时尽快校时 */

      if (online && !online_prev)
        {
          last_try = 0;
        }

      online_prev = online;

      if (online &&
          (last_ok == 0 || now - last_ok >= VG_TIME_RESYNC_S) &&
          now - last_try >= VG_TIME_RETRY_S)
        {
          last_try = now;
          if (sync_now() == 0)
            {
              last_ok = now;
              persist_now();
            }
        }

      if (now - last_persist >= VG_TIME_PERSIST_S)
        {
          persist_now();
          last_persist = now;
        }

      sleep(VG_TIME_TICK_S);
    }

  return NULL;
}

void vg_time_sync_start(void)
{
  pthread_t tid;
  pthread_attr_t attr;

  pthread_attr_init(&attr);
  pthread_attr_setstacksize(&attr, 4096);
  if (pthread_create(&tid, &attr, vg_time_sync_thread, NULL) != 0)
    {
      pthread_attr_destroy(&attr);
      printf("vgtime: thread create failed\n");
      return;
    }

  pthread_attr_destroy(&attr);
  pthread_detach(tid);
  printf("vgtime: sync started\n");
}
