/****************************************************************************
 * app/velaguard/vgscan.c
 *
 * Lightweight NSH: Modbus address scan @9600 (HMI / bring-up).
 ****************************************************************************/

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <nuttx/config.h>

#include "vg_discover.h"

#ifndef CONFIG_VG_HMI_RS485_DEVPATH
#  define CONFIG_VG_HMI_RS485_DEVPATH "/dev/rs485"
#endif

#ifndef CONFIG_VG_HMI_DISCOVER_BAUD
#  define CONFIG_VG_HMI_DISCOVER_BAUD 9600
#endif

#ifndef CONFIG_VG_HMI_DISCOVER_INTER_MS
#  define CONFIG_VG_HMI_DISCOVER_INTER_MS 50
#endif

#ifndef CONFIG_VG_HMI_DISCOVER_ADDR_MIN
#  define CONFIG_VG_HMI_DISCOVER_ADDR_MIN 1
#endif

#ifndef CONFIG_VG_HMI_DISCOVER_ADDR_MAX
#  define CONFIG_VG_HMI_DISCOVER_ADDR_MAX 32
#endif

static FAR const char *vgscan_pick_dev(void)
{
  static const char *const candidates[] =
  {
    CONFIG_VG_HMI_RS485_DEVPATH,
    "/dev/rs485",
    "/dev/ttyS1",
    "/dev/ttyS2",
    NULL
  };
  int i;

  for(i = 0; candidates[i] != NULL; i++)
    {
      if(access(candidates[i], R_OK | W_OK) == 0)
        {
          return candidates[i];
        }
    }

  return CONFIG_VG_HMI_RS485_DEVPATH;
}

int main(int argc, FAR char *argv[])
{
  struct vg_discover_summary local;
  FAR struct vg_discover_summary *sum = &local;
  FAR const char *dev;
  int amin = CONFIG_VG_HMI_DISCOVER_ADDR_MIN;
  int amax = CONFIG_VG_HMI_DISCOVER_ADDR_MAX;
  int inter = CONFIG_VG_HMI_DISCOVER_INTER_MS;
  int ret;
  int i;

  if(argc >= 3 && strcmp(argv[1], "-a") == 0)
    {
      if(!vg_discover_parse_addr_range(argv[2], &amin, &amax))
        {
          fprintf(stderr, "vgscan: bad -a range\n");
          return 1;
        }
    }

  vg_discover_reset(sum);
  dev = vgscan_pick_dev();

  printf("vgscan: %s @%d addr %d..%d inter=%dms\n",
         dev, CONFIG_VG_HMI_DISCOVER_BAUD, amin, amax, inter);

  if (vg_bus_lock_timeout(1000) != 0) { fprintf(stderr, "vgscan: bus busy\n"); return 1; }
  ret = vg_bus_scan(sum, dev, CONFIG_VG_HMI_DISCOVER_BAUD, amin, amax, inter);
  vg_bus_unlock();
  if(ret < 0)
    {
      fprintf(stderr, "vgscan: failed %d (errno=%d)\n", ret, errno);
      return 1;
    }

  printf("vgscan: found %d/%d slave(s):\n", sum->n_hits,
         amax - amin + 1);
  for(i = 0; i < sum->n_hits; i++)
    {
      printf("  addr=%u reg=%u\n",
             (unsigned)sum->hits[i].addr,
             (unsigned)sum->hits[i].probe_reg);
    }

  if (sum->flags) fprintf(stderr, "vgscan: capacity-limited results\n");
  return 0;
}
