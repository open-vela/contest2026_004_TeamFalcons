/****************************************************************************
 * app/velaguard/vg_hmi.c
 *
 * NuttX LVGL entry: LTDC + touch, gui/main/ui via Makefile (Phase B).
 * NSH: vghmi
 *      vghmi scan [-a min-max]  — RS485 discover without UI (bring-up)
 ****************************************************************************/

#include <nuttx/config.h>
#include <unistd.h>
#include <sys/boardctl.h>
#include <stdio.h>
#include <string.h>

#include <lvgl/lvgl.h>
#include "app/vg_app.h"
#include "model/vg_model.h"

#ifdef CONFIG_VG_HMI_DISCOVER
#include "vg_discover.h"
#endif

#ifndef CONFIG_VG_HMI_INPUT_DEVPATH
#  define CONFIG_VG_HMI_INPUT_DEVPATH "/dev/input0"
#endif

#ifndef CONFIG_VG_HMI_RS485_DEVPATH
#  define CONFIG_VG_HMI_RS485_DEVPATH "/dev/rs485"
#endif

#ifndef CONFIG_VG_HMI_DISCOVER_BAUD
#  define CONFIG_VG_HMI_DISCOVER_BAUD 9600
#endif

#ifndef CONFIG_VG_HMI_DISCOVER_INTER_MS
#  define CONFIG_VG_HMI_DISCOVER_INTER_MS 50
#endif

#undef NEED_BOARDINIT
#if defined(CONFIG_BOARDCTL) && !defined(CONFIG_NSH_ARCHINIT)
#  define NEED_BOARDINIT 1
#endif

#ifdef CONFIG_VG_HMI_DISCOVER
static FAR const char *vg_hmi_pick_rs485(void)
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

static int vg_hmi_cli_scan(int amin, int amax)
{
  FAR struct vg_discover_summary *sum = vg_discover_state();
  FAR const char *dev = vg_hmi_pick_rs485();
  int inter = CONFIG_VG_HMI_DISCOVER_INTER_MS;
  int ret;
  int i;

  if(inter < 50)
    {
      inter = 50;
    }

  vg_discover_reset(sum);
  printf("vghmi scan: %s @%d addr %d..%d inter=%dms\n",
         dev, CONFIG_VG_HMI_DISCOVER_BAUD, amin, amax, inter);
  ret = vg_bus_scan(sum, dev, CONFIG_VG_HMI_DISCOVER_BAUD, amin, amax, inter);
  if(ret < 0)
    {
      fprintf(stderr, "vghmi scan: failed %d\n", ret);
      return 1;
    }

  printf("vghmi scan: found %d/%d slave(s):\n", sum->n_hits, amax - amin + 1);
  for(i = 0; i < sum->n_hits; i++)
    {
      printf("  addr=%u reg=%u\n",
             (unsigned)sum->hits[i].addr,
             (unsigned)sum->hits[i].probe_reg);
    }

  return (sum->n_hits >= (amax - amin + 1)) ? 0 : 1;
}
#endif

int main(int argc, char *argv[])
{
  lv_nuttx_dsc_t info;
  lv_nuttx_result_t result;
  uint32_t idle;

#ifdef CONFIG_VG_HMI_DISCOVER
  if(argc >= 2 && strcmp(argv[1], "scan") == 0)
    {
      int amin = 1;
      int amax = 32;

      if(argc >= 4 && strcmp(argv[2], "-a") == 0)
        {
          if(!vg_discover_parse_addr_range(argv[3], &amin, &amax))
            {
              fprintf(stderr, "vghmi scan: bad -a range\n");
              return 1;
            }
        }

      return vg_hmi_cli_scan(amin, amax);
    }
#endif

  if (lv_is_initialized())
    {
      return -1;
    }

#ifdef NEED_BOARDINIT
  boardctl(BOARDIOC_INIT, 0);
#endif

  lv_init();
  lv_nuttx_dsc_init(&info);

#ifdef CONFIG_LV_USE_NUTTX_LCD
  info.fb_path = "/dev/lcd0";
#endif

#ifdef CONFIG_INPUT_TOUCHSCREEN
  info.input_path = CONFIG_VG_HMI_INPUT_DEVPATH;
#endif

  lv_nuttx_init(&info, &result);
  if (result.disp == NULL)
    {
      return 1;
    }

  vg_app_init();
  {
    uint16_t fleet_n = 0;
    FILE *fp;

    (void)vg_model_get_sensors(&fleet_n);
    printf("vghmi: home fleet n=%u\n", (unsigned)fleet_n);
    fp = fopen("/data/velaguard/hmi_fleet.txt", "w");
    if(fp != NULL) {
      fprintf(fp, "n=%u\n", (unsigned)fleet_n);
      fclose(fp);
    }
  }

  while (1)
    {
      idle = lv_timer_handler();
      idle = idle ? idle : 1;
      usleep(idle * 1000);
    }

  /* not reached */
}
