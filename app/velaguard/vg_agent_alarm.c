/****************************************************************************
 * Local alarm detector: offline (frame stats) + threshold (Modbus read).
 * Writes /data/velaguard/pending_alarm.txt for Agent HEARTBEAT pickup.
 ****************************************************************************/

#include <nuttx/config.h>

#ifdef CONFIG_VG_AGENT_OPS

#include "vg_agent_alarm.h"

#ifndef CONFIG_VG_HMI
#include "vg_modbus_read.h"
#ifdef CONFIG_VG_FRAME_STATS
#include "vg_frame_stats.h"
#endif
#include <pthread.h>
#endif

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#ifndef CONFIG_VG_HMI
#ifndef CONFIG_VG_AGENT_ALARM_SLAVE
#  define CONFIG_VG_AGENT_ALARM_SLAVE 1
#endif

#ifndef CONFIG_VG_AGENT_ALARM_POLL_SEC
#  define CONFIG_VG_AGENT_ALARM_POLL_SEC 60
#endif

#ifndef CONFIG_VG_AGENT_ALARM_TEMP_REG
#  define CONFIG_VG_AGENT_ALARM_TEMP_REG 0
#endif

#ifndef CONFIG_VG_AGENT_ALARM_TEMP_THRESHOLD
#  define CONFIG_VG_AGENT_ALARM_TEMP_THRESHOLD 3000
#endif

#ifndef CONFIG_VG_AGENT_ALARM_OFFLINE_PCT
#  define CONFIG_VG_AGENT_ALARM_OFFLINE_PCT 50
#endif

static volatile int g_alarm_run;
static pthread_t g_alarm_tid;
#endif

static int ensure_reports_dir(void)
{
  int ret;

  ret = mkdir("/data/velaguard", 0755);
  if (ret < 0 && errno != EEXIST)
    {
      return ret;
    }

  ret = mkdir("/data/velaguard/reports", 0755);
  if (ret < 0 && errno != EEXIST)
    {
      return ret;
    }

  return 0;
}

static int pending_exists(void)
{
  struct stat st;

  return stat("/data/velaguard/pending_alarm.txt", &st) == 0;
}

static int write_pending(const char *body)
{
  int fd;

  if (body == NULL || body[0] == '\0')
    {
      return -EINVAL;
    }

  if (pending_exists())
    {
      return 0;
    }

  (void)ensure_reports_dir();
  fd = open("/data/velaguard/pending_alarm.txt",
            O_WRONLY | O_CREAT | O_TRUNC, 0644);
  if (fd < 0)
    {
      return -errno;
    }

  if (write(fd, body, strlen(body)) != (ssize_t)strlen(body))
    {
      close(fd);
      return -EIO;
    }

  close(fd);
  return 0;
}

int vg_pending_alarm_write(const char *body)
{
  return write_pending(body);
}

#ifndef CONFIG_VG_HMI
static void check_offline(uint8_t slave)
{
#ifdef CONFIG_VG_FRAME_STATS
  struct vg_fs_summary sum;
  char buf[256];
  unsigned int pct;

  if (vg_fs_summary(slave, &sum) != 0 || sum.total < 5)
    {
      return;
    }

  pct = (sum.timeout * 100U) / sum.total;
  if (pct < (unsigned int)CONFIG_VG_AGENT_ALARM_OFFLINE_PCT)
    {
      return;
    }

  snprintf(buf, sizeof(buf),
           "type=offline\nslave=%u\ntimeout_pct=%u\n"
           "total=%lu timeout=%lu crc=%lu\n"
           "hint=use alarm_interpretation skill with vgstats dump %u\n",
           (unsigned int)slave, pct,
           (unsigned long)sum.total, (unsigned long)sum.timeout,
           (unsigned long)sum.crc_err, (unsigned int)slave);
  (void)write_pending(buf);
#endif
}

static void check_threshold(uint8_t slave)
{
  uint16_t regs[2];
  char buf[256];

  if (vg_modbus_read_holding(slave, CONFIG_VG_AGENT_ALARM_TEMP_REG, 1,
                             regs) != 0)
    {
      return;
    }

  if (regs[0] <= (uint16_t)CONFIG_VG_AGENT_ALARM_TEMP_THRESHOLD)
    {
      return;
    }

  snprintf(buf, sizeof(buf),
           "type=threshold\nslave=%u\nreg=%u\nvalue=%u\nthreshold=%u\n"
           "hint=use alarm_interpretation skill; verify with "
           "vgmodbus -a %u -r %u -c 1 -n 1 -i 0\n",
           (unsigned int)slave,
           (unsigned int)CONFIG_VG_AGENT_ALARM_TEMP_REG,
           (unsigned int)regs[0],
           (unsigned int)CONFIG_VG_AGENT_ALARM_TEMP_THRESHOLD,
           (unsigned int)slave,
           (unsigned int)CONFIG_VG_AGENT_ALARM_TEMP_REG);
  (void)write_pending(buf);
}

static void *alarm_thread(FAR void *arg)
{
  const uint8_t slave = (uint8_t)CONFIG_VG_AGENT_ALARM_SLAVE;

  (void)arg;

  /* RS485 / eMMC may still be settling; avoid early bus access panic. */
  sleep(15);

  while (g_alarm_run)
    {
      if (!pending_exists())
        {
          check_threshold(slave);
          check_offline(slave);
        }

      sleep(CONFIG_VG_AGENT_ALARM_POLL_SEC);
    }

  return NULL;
}

#endif /* !CONFIG_VG_HMI */

int vg_agent_alarm_start(void)
{
#ifdef CONFIG_VG_HMI
  /* HMI live path evaluates the committed table and writes pending.
   * Do not start a second Modbus poller (slave 1 / hardcoded temp).
   */
  (void)ensure_reports_dir();
  return 0;
#else
  int ret;

  if (g_alarm_run)
    {
      return 0;
    }

  (void)ensure_reports_dir();
  g_alarm_run = true;
  ret = pthread_create(&g_alarm_tid, NULL, alarm_thread, NULL);
  if (ret != 0)
    {
      g_alarm_run = false;
      return -ret;
    }

  return 0;
#endif
}

#endif /* CONFIG_VG_AGENT_OPS */
