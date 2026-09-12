/****************************************************************************
 * app/velaguard/modbus_collector.c
 *
 * vgmodbus - Modbus RTU 主站 bring-up 工具（NSH 命令）。
 *
 * 用途：阶段 2 最小闭环。经 nanoMODBUS client 周期读一个从站的
 * Holding / Input Register，打印数值与连续失败次数（后续离线判定的输入）。
 *
 * 用法（NSH）：
 *   vgmodbus [-d <dev>] [-a <addr>] [-r <start>] [-c <qty>]
 *            [-t 3|4] [-n <loops>] [-i <secs>]
 *
 * 缺省：dev=/dev/rs485, addr=1, start=0, qty=4, FC=3 (holding),
 *       loops=0（一直轮询）, interval=1s。
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>

#include "modbus_port_openvela.h"

#include "vg_discover.h"

#ifdef CONFIG_VG_FRAME_STATS
#include "vg_frame_stats.h"
#endif

#ifndef VGMODBUS_DEFAULT_DEV
#  define VGMODBUS_DEFAULT_DEV "/dev/rs485"
#endif

#define VGMODBUS_MAX_REGS     32
#define VGMODBUS_READ_TO_MS   2000
#define VGMODBUS_BYTE_TO_MS   20

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct vgmodbus_cfg_s
{
  FAR const char *dev;
  uint8_t         addr;
  uint16_t        start;
  uint16_t        qty;
  uint8_t         fc;          /* 3=holding, 4=input */
  unsigned int    loops;       /* 0 = forever */
  unsigned int    interval_s;
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static void show_usage(FAR const char *prog)
{
  fprintf(stderr,
          "Usage: %s [-d <dev>] [-a <addr>] [-r <start>] [-c <qty>]\n"
          "          [-t 3|4] [-n <loops>] [-i <secs>]\n"
          "Defaults: dev=%s addr=1 start=0 qty=4 fc=3 loops=0 interval=1s\n"
          "  -t 3  Read Holding Registers (FC 03)\n"
          "  -t 4  Read Input Registers   (FC 04)\n"
          "  -n 0  poll forever\n",
          prog, VGMODBUS_DEFAULT_DEV);
}

static int parse_args(int argc, FAR char *argv[],
                      FAR struct vgmodbus_cfg_s *cfg)
{
  int opt;
  long v;

  memset(cfg, 0, sizeof(*cfg));
  cfg->dev        = VGMODBUS_DEFAULT_DEV;
  cfg->addr       = 1;
  cfg->start      = 0;
  cfg->qty        = 4;
  cfg->fc         = 3;
  cfg->loops      = 0;
  cfg->interval_s = 1;

  optind = 1;

  while ((opt = getopt(argc, argv, "d:a:r:c:t:n:i:")) != ERROR)
    {
      switch (opt)
        {
          case 'd':
            cfg->dev = optarg;
            break;

          case 'a':
            v = strtol(optarg, NULL, 0);
            if (v < 1 || v > 247)
              {
                fprintf(stderr, "ERROR: addr must be 1..247\n");
                return ERROR;
              }

            cfg->addr = (uint8_t)v;
            break;

          case 'r':
            v = strtol(optarg, NULL, 0);
            if (v < 0 || v > 65535)
              {
                fprintf(stderr, "ERROR: start register out of range\n");
                return ERROR;
              }

            cfg->start = (uint16_t)v;
            break;

          case 'c':
            v = strtol(optarg, NULL, 0);
            if (v < 1 || v > VGMODBUS_MAX_REGS)
              {
                fprintf(stderr, "ERROR: qty must be 1..%d\n",
                        VGMODBUS_MAX_REGS);
                return ERROR;
              }

            cfg->qty = (uint16_t)v;
            break;

          case 't':
            v = strtol(optarg, NULL, 0);
            if (v != 3 && v != 4)
              {
                fprintf(stderr, "ERROR: -t must be 3 (holding) or 4 (input)\n");
                return ERROR;
              }

            cfg->fc = (uint8_t)v;
            break;

          case 'n':
            v = strtol(optarg, NULL, 0);
            if (v < 0)
              {
                fprintf(stderr, "ERROR: loops must be >= 0\n");
                return ERROR;
              }

            cfg->loops = (unsigned int)v;
            break;

          case 'i':
            v = strtol(optarg, NULL, 0);
            if (v < 0)
              {
                fprintf(stderr, "ERROR: interval must be >= 0\n");
                return ERROR;
              }

            cfg->interval_s = (unsigned int)v;
            break;

          default:
            show_usage(argv[0]);
            return ERROR;
        }
    }

  return OK;
}

static nmbs_error do_read(FAR nmbs_t *nmbs, FAR const struct vgmodbus_cfg_s *cfg,
                          FAR uint16_t *regs)
{
  if (cfg->fc == 4)
    {
      return nmbs_read_input_registers(nmbs, cfg->start, cfg->qty, regs);
    }

  return nmbs_read_holding_registers(nmbs, cfg->start, cfg->qty, regs);
}

#ifdef CONFIG_VG_FRAME_STATS
static int32_t now_ms(void)
{
  struct timespec ts;

  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (int32_t)(ts.tv_sec * 1000L + ts.tv_nsec / 1000000L);
}

static enum vg_fs_result nmbs_to_fs(nmbs_error err)
{
  if (err == NMBS_ERROR_NONE)
    {
      return VG_FS_OK;
    }

  if (err == NMBS_ERROR_CRC)
    {
      return VG_FS_CRC;
    }

  if (err == NMBS_ERROR_TIMEOUT)
    {
      return VG_FS_TIMEOUT;
    }

  return VG_FS_OTHER;
}

static void record_frame(uint8_t slave, nmbs_error err, int32_t t0_ms)
{
  int32_t t1 = now_ms();
  uint32_t lat = (t1 >= t0_ms) ? (uint32_t)(t1 - t0_ms) : 0;

  (void)vg_fs_record(slave, nmbs_to_fs(err),
                     (err == NMBS_ERROR_NONE) ? lat : 0);
}
#endif

static void print_regs(uint8_t addr, uint16_t start, uint16_t qty,
                       FAR const uint16_t *regs)
{
  unsigned int i;

  printf("addr=%u start=%u:", (unsigned int)addr, (unsigned int)start);
  for (i = 0; i < qty; i++)
    {
      printf(" [%u]=%u", (unsigned int)(start + i), (unsigned int)regs[i]);
    }

  printf("\n");
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int main(int argc, FAR char *argv[])
{
  struct vgmodbus_cfg_s cfg;
  struct vg_modbus_port_s port;
  nmbs_platform_conf pc;
  nmbs_t nmbs;
  nmbs_error err;
  uint32_t fails = 0;
  unsigned int n = 0;

#ifdef CONFIG_VG_FRAME_STATS
  vg_fs_init();
#endif

  if (parse_args(argc, argv, &cfg) != OK)
    {
      return 1;
    }

  port.fd = -1;
  if (vg_modbus_port_open(&port, cfg.dev) < 0)
    {
      fprintf(stderr, "vgmodbus: open %s failed: %d\n", cfg.dev, errno);
      return 1;
    }

  vg_modbus_port_bind(&port, &pc);

  err = nmbs_client_create(&nmbs, &pc);
  if (err != NMBS_ERROR_NONE)
    {
      fprintf(stderr, "vgmodbus: nmbs_client_create: %s\n",
              nmbs_strerror(err));
      vg_modbus_port_close(&port);
      return 1;
    }

  nmbs_set_read_timeout(&nmbs, VGMODBUS_READ_TO_MS);
  nmbs_set_byte_timeout(&nmbs, VGMODBUS_BYTE_TO_MS);
  nmbs_set_destination_rtu_address(&nmbs, cfg.addr);

  printf("vgmodbus: %s addr=%u fc=%u start=%u qty=%u\n",
         cfg.dev, (unsigned int)cfg.addr, (unsigned int)cfg.fc,
         (unsigned int)cfg.start, (unsigned int)cfg.qty);

  for (; ; )
    {
      uint16_t regs[VGMODBUS_MAX_REGS];
      unsigned int waits = 0;
      bool locked;
#ifdef CONFIG_VG_FRAME_STATS
      int32_t t0 = now_ms();
#endif

      memset(regs, 0, sizeof(regs));

      /* Serialize with the HMI acq poller / vgpoint: a collision on the
       * half-duplex bus shows up as failures on both sides. */

      while (vg_bus_try_lock() != 0)
        {
          if (++waits > 50)
            {
              break;
            }

          usleep(20000);
        }

      locked = (waits <= 50);
      if (!locked)
        {
          printf("vgmodbus: bus busy, skip round\n");
        }
      else
        {
          err = do_read(&nmbs, &cfg, regs);
          vg_bus_unlock();
#ifdef CONFIG_VG_FRAME_STATS
          record_frame(cfg.addr, err, t0);
#endif
        }

      if (locked)
        {
          if (err == NMBS_ERROR_NONE)
            {
              fails = 0;
              print_regs(cfg.addr, cfg.start, cfg.qty, regs);
            }
          else
            {
              fails++;
              printf("read failed: %s (fails=%lu)\n",
                     nmbs_strerror(err), (unsigned long)fails);
            }
        }

      n++;
      if (cfg.loops != 0 && n >= cfg.loops)
        {
          break;
        }

      if (cfg.interval_s > 0)
        {
          sleep(cfg.interval_s);
        }
    }

  vg_modbus_port_close(&port);
  return (fails == 0) ? 0 : 1;
}
