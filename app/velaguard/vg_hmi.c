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
#include <malloc.h>
#include <stdio.h>
#include <string.h>

#include <lvgl/lvgl.h>
#include "app/vg_app.h"
#include "model/vg_model.h"
#include "model/vg_ui_backend.h"
#include "shell/vg_shell.h"
#include "vg_hmi_perf.h"
#include "vg_hmi_sched.h"

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

/* ---------------- HMI performance observation (measurement builds) ----------------
 * Read-only observation of loop / input / model tick / render / flush / nav
 * plus a 1 Hz resource snapshot. No business behavior changes, no on-screen
 * overlay, no per-frame output. Compiled out entirely without
 * CONFIG_VG_HMI_PERF. */

#ifdef VG_HMI_PERF_ENABLED

#define VG_PERF_INDEV_MAX 4
#define VG_PERF_OBJ_QUEUE 512

static lv_indev_t * s_orig_indev[VG_PERF_INDEV_MAX];
static lv_indev_read_cb_t s_orig_read[VG_PERF_INDEV_MAX];
static int s_orig_n;

static void perf_indev_read_cb(lv_indev_t * indev, lv_indev_data_t * data)
{
  lv_indev_read_cb_t orig = NULL;
  uint32_t t0;
  int i;

  for(i = 0; i < s_orig_n; i++)
    {
      if(s_orig_indev[i] == indev)
        {
          orig = s_orig_read[i];
          break;
        }
    }

  /* Sampling cost of the raw read callback only; not touch latency. */
  t0 = vg_hmi_perf_now_us();
  if(orig != NULL)
    {
      orig(indev, data);
    }
  vg_hmi_perf_span(VG_PERF_INPUT_READ, t0);
}

static void perf_wrap_indevs(void)
{
  lv_indev_t * indev = lv_indev_get_next(NULL);

  while(indev != NULL && s_orig_n < VG_PERF_INDEV_MAX)
    {
      lv_indev_read_cb_t orig = lv_indev_get_read_cb(indev);
      if(orig != NULL && orig != perf_indev_read_cb)
        {
          s_orig_indev[s_orig_n] = indev;
          s_orig_read[s_orig_n] = orig;
          s_orig_n++;
          lv_indev_set_read_cb(indev, perf_indev_read_cb);
        }
      indev = lv_indev_get_next(indev);
    }
}

static void perf_disp_event_cb(lv_event_t * e)
{
  switch(lv_event_get_code(e))
    {
      case LV_EVENT_RENDER_START:
        vg_hmi_perf_begin(VG_PERF_RENDER);
        break;
      case LV_EVENT_RENDER_READY:
        vg_hmi_perf_end(VG_PERF_RENDER);
        break;
      case LV_EVENT_FLUSH_START:
        vg_hmi_perf_begin(VG_PERF_FLUSH);
        break;
      case LV_EVENT_FLUSH_FINISH:
        vg_hmi_perf_end(VG_PERF_FLUSH);
        break;
      default:
        break;
    }
}

/* BFS over the page subtree; HMI thread only, once per second. */
static uint32_t perf_count_page_objects(void)
{
  lv_obj_t * queue[VG_PERF_OBJ_QUEUE];
  uint32_t count = 0;
  int qh = 0;
  int qt = 0;
  lv_obj_t * root = vg_shell_get_content();

  if(root == NULL)
    {
      return 0;
    }

  queue[qt++] = root;
  while(qh < qt && qt < VG_PERF_OBJ_QUEUE)
    {
      lv_obj_t * o = queue[qh++];
      uint32_t i;
      uint32_t n = lv_obj_get_child_count(o);

      count++;
      for(i = 0; i < n && qt < VG_PERF_OBJ_QUEUE; i++)
        {
          queue[qt++] = lv_obj_get_child(o, (int32_t)i);
        }
    }

  return count;
}

static void perf_resource_timer_cb(lv_timer_t * t)
{
  vg_perf_resources_t res;
  lv_timer_t * it;
  uint32_t timers_total = 0;

  LV_UNUSED(t);
  memset(&res, 0, sizeof(res));
  res.page_id = (int32_t)vg_nav_current();
  res.page_objects = perf_count_page_objects();
  res.model_listeners = vg_model_debug_listener_count();
  res.shell_timers = vg_shell_debug_timer_count();
  for(it = lv_timer_get_next(NULL); it != NULL; it = lv_timer_get_next(it))
    {
      timers_total++;
    }
  res.timers_total = timers_total;

  vg_hmi_perf_publish_resources(&res);
}

static void perf_print_metric(const char * name, const vg_perf_metric_stats_t * m)
{
  uint32_t win[VG_PERF_WINDOW];
  char p50_s[16];
  char p95_s[16];

  if(m->win_n > 0)
    {
      uint16_t i;
      uint16_t start = (uint16_t)(m->win_head - m->win_n);
      for(i = 0; i < m->win_n; i++)
        {
          win[i] = m->win[(start + i) % VG_PERF_WINDOW];
        }
      snprintf(p50_s, sizeof(p50_s), "%u",
               (unsigned)vg_hmi_perf_nearest_rank(win, m->win_n, 50));
      snprintf(p95_s, sizeof(p95_s), "%u",
               (unsigned)vg_hmi_perf_nearest_rank(win, m->win_n, 95));
    }
  else
    {
      /* Empty window: explicit n/a, never a fake zero-duration value. */
      snprintf(p50_s, sizeof(p50_s), "n/a");
      snprintf(p95_s, sizeof(p95_s), "n/a");
    }

  printf("%s: n=%u total_us=%llu max_us=%u win_n=%u p50_us=%s p95_us=%s "
         "incomplete=%u\n",
         name, (unsigned)m->total_n, (unsigned long long)m->total_us,
         (unsigned)m->max_us, (unsigned)m->win_n, p50_s, p95_s,
         (unsigned)m->incomplete_n);
}

static void perf_print_resources(const vg_perf_report_t * rep)
{
  struct mallinfo mi = mallinfo();
  char cpu_raw[96];

  cpu_raw[0] = '\0';
  {
    FILE * fp = fopen("/proc/cpuload", "r");
    if(fp != NULL)
      {
        size_t n = fread(cpu_raw, 1, sizeof(cpu_raw) - 1, fp);
        fclose(fp);
        while(n > 0 && (cpu_raw[n - 1] == '\n' || cpu_raw[n - 1] == ' '))
          {
            n--;
          }
        cpu_raw[n] = '\0';
      }
  }

  printf("submit: commits=%u last_commit_ms=%u\n",
         (unsigned)rep->res.commits, (unsigned)rep->res.last_commit_ms);
  if(rep->res.valid)
    {
      printf("page: id=%ld objects=%u listeners=%u timers_total=%u "
             "shell_timers=%u\n",
             (long)rep->res.page_id, (unsigned)rep->res.page_objects,
             (unsigned)rep->res.model_listeners,
             (unsigned)rep->res.timers_total,
             (unsigned)rep->res.shell_timers);
    }
  else
    {
      printf("page: n/a (no 1 Hz snapshot yet)\n");
    }

  printf("heap: used=%d free=%d\n", mi.uordblks, mi.fordblks);
  printf("cpu: %s\n",
         (cpu_raw[0] != '\0') ? cpu_raw : "unavailable (/proc/cpuload absent)");
  printf("stack: unavailable (no per-thread reader in this build)\n");
}

static int vg_hmi_cli_perf(void)
{
  static vg_perf_report_t rep; /* ~6.5 KB: static, vghmi task also fine */
  const char * names[VG_PERF_METRIC_N] =
  {
    "loop", "input_read", "model_tick", "render", "flush", "nav"
  };
  int i;

  if(!vg_hmi_perf_read(&rep))
    {
      printf("vghmi perf: state=not_running (HMI has not initialized stats)\n");
      return 1;
    }

  printf("vghmi perf: state=%s uptime_ms=%u clock_res_us=%u gran_us=%u "
         "dropped=%u budget=%u metrics=%d window=%d\n",
         rep.hmi_running ? "running" : "not_running",
         (unsigned)rep.uptime_ms, (unsigned)rep.clock_res_us,
         (unsigned)rep.granularity_us, (unsigned)rep.dropped_n,
         (unsigned)vg_hmi_perf_budget_bytes(), VG_PERF_METRIC_N,
         VG_PERF_WINDOW);
  printf("vghmi perf: units are microseconds; gran_us is the observed "
         "clock granularity, not a resolution guarantee\n");

  for(i = 0; i < VG_PERF_METRIC_N; i++)
    {
      perf_print_metric(names[i], &rep.m[i]);
    }

  perf_print_resources(&rep);
  return 0;
}

#else

static int vg_hmi_cli_perf(void)
{
  printf("vghmi perf: state=disabled (built without CONFIG_VG_HMI_PERF)\n");
  return 0;
}

#endif /* VG_HMI_PERF_ENABLED */

int main(int argc, char *argv[])
{
  lv_nuttx_dsc_t info;
  lv_nuttx_result_t result;
  uint32_t idle;

  /* Diagnostics split off before the lv_is_initialized() guard:
   * `vghmi perf` never calls lv_init(), never starts acquisition and
   * never creates a second HMI; it only reads the published stats. */
  if(argc >= 2 && strcmp(argv[1], "perf") == 0)
    {
      return vg_hmi_cli_perf();
    }

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

  /* T1: pointer indev read period 10 ms. Display refr stays LV_DEF_REFR_PERIOD
   * (16 ms); model tick stays 1000 ms. Missing touch is logged, not fatal. */
  {
    lv_indev_t * indev = lv_indev_get_next(NULL);
    int pointer_n = 0;

    while(indev != NULL)
      {
        if(lv_indev_get_type(indev) == LV_INDEV_TYPE_POINTER)
          {
            lv_timer_t * read_tmr = lv_indev_get_read_timer(indev);

            if(read_tmr != NULL)
              {
                lv_timer_set_period(read_tmr, VG_HMI_INDEV_READ_MS);
                pointer_n++;
              }
          }

        indev = lv_indev_get_next(indev);
      }

    if(pointer_n == 0)
      {
        printf("vghmi: no pointer indev (touch input unavailable)\n");
      }
    else
      {
        printf("vghmi: pointer indev read=%ums n=%d\n",
               (unsigned)VG_HMI_INDEV_READ_MS, pointer_n);
      }
  }

#ifdef VG_HMI_PERF_ENABLED
  /* Measurement build: init stats, wrap the sampling callbacks and watch
   * the render/flush protocol events. The 1 Hz resource snapshot runs on
   * this (HMI) thread. Input period / sleep bounds are set above for all
   * builds; observation wrappers remain measurement-only. */
  vg_hmi_perf_init();
  vg_hmi_perf_set_running(true);
  perf_wrap_indevs();
  lv_display_add_event_cb(result.disp, perf_disp_event_cb,
                          LV_EVENT_RENDER_START, NULL);
  lv_display_add_event_cb(result.disp, perf_disp_event_cb,
                          LV_EVENT_RENDER_READY, NULL);
  lv_display_add_event_cb(result.disp, perf_disp_event_cb,
                          LV_EVENT_FLUSH_START, NULL);
  lv_display_add_event_cb(result.disp, perf_disp_event_cb,
                          LV_EVENT_FLUSH_FINISH, NULL);
  lv_timer_create(perf_resource_timer_cb, 1000, NULL);
#endif

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
  vg_ui_backend_acq_start();

  while (1)
    {
      uint32_t loop_t0 = vg_hmi_perf_now_us();
      uint32_t sleep_ms;

      idle = lv_timer_handler();
      vg_hmi_perf_span(VG_PERF_LOOP, loop_t0);
      sleep_ms = vg_hmi_bound_idle_ms(idle);
      usleep(sleep_ms * 1000u);
    }

  /* not reached */
}
