/****************************************************************************
 * app/velaguard/vg_ui_backend_board.c
 *
 * NuttX HMI backend: Modbus discover @9600 + report file read.
 ****************************************************************************/

#include <nuttx/config.h>

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "model/vg_ui_backend.h"
#include "model/vg_mthings_points.h"
#include "model/vg_model.h"
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

#ifndef CONFIG_VG_HMI_REPORT_DIR
#  define CONFIG_VG_HMI_REPORT_DIR "/data/agent/reports"
#endif

#ifndef CONFIG_VG_DISCOVER_POINTS_PATH
#  define CONFIG_VG_DISCOVER_POINTS_PATH "/data/velaguard/config/points.json"
#endif

#ifndef CONFIG_VG_CONFIG_BASEDIR
#  define CONFIG_VG_CONFIG_BASEDIR "/data/velaguard/config"
#endif

#define VG_HMI_SCAN_STACKSIZE 8192

#define VG_SCAN_IDLE     0
#define VG_SCAN_RUNNING  1
#define VG_SCAN_DONE     2

static volatile int g_scan_status = VG_SCAN_IDLE;
static volatile int g_scan_last_rc;
static volatile bool g_scan_thread_active;
static volatile int g_apply_status = VG_SCAN_IDLE;
static volatile int g_apply_last_rc;
static volatile bool g_apply_thread_active;
static int g_scan_amin = CONFIG_VG_HMI_DISCOVER_ADDR_MIN;
static int g_scan_amax = CONFIG_VG_HMI_DISCOVER_ADDR_MAX;
static char g_rs485_dev[32];

static FAR const char *vg_hmi_rs485_devpath(void)
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
          snprintf(g_rs485_dev, sizeof(g_rs485_dev), "%s", candidates[i]);
          return g_rs485_dev;
        }
    }

  snprintf(g_rs485_dev, sizeof(g_rs485_dev), "%s",
           CONFIG_VG_HMI_RS485_DEVPATH);
  return g_rs485_dev;
}

static FAR void *vg_hmi_scan_thread(FAR void *arg)
{
  FAR struct vg_discover_summary *sum = vg_discover_state();
  int rc;

  (void)arg;

  vg_discover_reset(sum);
  rc = vg_bus_scan(sum, vg_hmi_rs485_devpath(),
                   CONFIG_VG_HMI_DISCOVER_BAUD,
                   g_scan_amin, g_scan_amax,
                   CONFIG_VG_HMI_DISCOVER_INTER_MS);
  g_scan_last_rc = rc;
  g_scan_status = (rc >= 0) ? VG_SCAN_DONE : -1;
  g_scan_thread_active = false;
  return NULL;
}

static int board_discover_scan_start(int addr_min, int addr_max)
{
  pthread_t tid;
  pthread_attr_t attr;

  if(g_scan_thread_active || g_scan_status == VG_SCAN_RUNNING ||
     g_apply_thread_active || g_apply_status == VG_SCAN_RUNNING) {
    return -EBUSY;
  }

  if(addr_min < 1) {
    addr_min = CONFIG_VG_HMI_DISCOVER_ADDR_MIN;
  }
  if(addr_max > 247) {
    addr_max = CONFIG_VG_HMI_DISCOVER_ADDR_MAX;
  }
  if(addr_min > addr_max) {
    return -EINVAL;
  }

  if(access(vg_hmi_rs485_devpath(), R_OK | W_OK) != 0) {
    g_scan_last_rc = -errno;
    return -errno;
  }

  g_scan_amin = addr_min;
  g_scan_amax = addr_max;
  g_scan_last_rc = 0;
  g_scan_status = VG_SCAN_RUNNING;
  g_scan_thread_active = true;
  g_apply_status = VG_SCAN_IDLE;

  pthread_attr_init(&attr);
  pthread_attr_setstacksize(&attr, VG_HMI_SCAN_STACKSIZE);
  if(pthread_create(&tid, &attr, vg_hmi_scan_thread, NULL) != 0) {
    pthread_attr_destroy(&attr);
    g_scan_thread_active = false;
    g_scan_status = VG_SCAN_IDLE;
    g_scan_last_rc = -EIO;
    return -EIO;
  }

  pthread_attr_destroy(&attr);
  pthread_detach(tid);
  return 0;
}

static int board_discover_scan_status(void)
{
  return g_scan_status;
}

static FAR void *vg_hmi_apply_thread(FAR void *arg)
{
  FAR struct vg_discover_summary *sum = vg_discover_state();
  FAR const char *dev = vg_hmi_rs485_devpath();
  int i;
  int rc;

  (void)arg;

  if(sum == NULL || sum->n_hits <= 0) {
    g_apply_last_rc = -ENOENT;
    g_apply_status = -1;
    g_apply_thread_active = false;
    return NULL;
  }

  /* Scan alone only fills hits; probe+infer before persistence. */
  for(i = 0; i < sum->n_hits; i++) {
    if(!sum->hits[i].alive) {
      continue;
    }

    rc = vg_reg_probe_slave(sum, dev, CONFIG_VG_HMI_DISCOVER_BAUD,
                            sum->hits[i].addr, 3, 32);
    if(rc < 0) {
      /* Keep going — other slaves may still yield points. */
      continue;
    }
  }

  if(sum->n_points == 0) {
    vg_point_table_infer(sum);
  }

  if(sum->n_points <= 0) {
    g_apply_last_rc = -ENOENT;
    g_apply_status = -1;
    g_apply_thread_active = false;
    return NULL;
  }

  rc = vg_point_table_apply(sum, CONFIG_VG_DISCOVER_POINTS_PATH,
                            CONFIG_VG_CONFIG_BASEDIR, true);
  g_apply_last_rc = rc;
  g_apply_status = (rc == 0) ? VG_SCAN_DONE : -1;
  g_apply_thread_active = false;
  return NULL;
}

static int board_discover_apply_start(void)
{
  pthread_t tid;
  pthread_attr_t attr;
  FAR const struct vg_discover_summary *sum = vg_discover_state();

  if(g_scan_thread_active || g_scan_status == VG_SCAN_RUNNING ||
     g_apply_thread_active || g_apply_status == VG_SCAN_RUNNING) {
    return -EBUSY;
  }

  if(sum == NULL || sum->n_hits <= 0) {
    return -ENOENT;
  }

  g_apply_last_rc = 0;
  g_apply_status = VG_SCAN_RUNNING;
  g_apply_thread_active = true;

  pthread_attr_init(&attr);
  pthread_attr_setstacksize(&attr, VG_HMI_SCAN_STACKSIZE);
  if(pthread_create(&tid, &attr, vg_hmi_apply_thread, NULL) != 0) {
    pthread_attr_destroy(&attr);
    g_apply_thread_active = false;
    g_apply_status = VG_SCAN_IDLE;
    g_apply_last_rc = -EIO;
    return -EIO;
  }

  pthread_attr_destroy(&attr);
  pthread_detach(tid);
  return 0;
}

static int board_discover_apply_status(void)
{
  return g_apply_status;
}

static int board_get_slaves(vg_ui_slave_t *out, int max)
{
  FAR const struct vg_discover_summary *sum = vg_discover_state();
  uint8_t persisted[VG_DISCOVER_MAX_SLAVES];
  int i;
  int n = 0;
  int persisted_n = 0;

  if(out == NULL || max <= 0) {
    return 0;
  }

  if(sum != NULL) {
    for(i = 0; i < sum->n_hits && n < max; i++) {
      if(!sum->hits[i].alive) {
        continue;
      }

      out[n].addr = sum->hits[i].addr;
      out[n].probe_reg = sum->hits[i].probe_reg;
      snprintf(out[n].label, sizeof(out[n].label), "addr=%u",
               (unsigned)sum->hits[i].addr);
      n++;
    }
  }

  if(n > 0) {
    return n;
  }

  /* Cold start: last confirmed table, not mock 24. Retry — FAT may
   * still be settling when vghmi is task_create'd. */
  for(i = 0; i < 10 && persisted_n <= 0; i++) {
    persisted_n = vg_point_table_read_slaves(CONFIG_VG_DISCOVER_POINTS_PATH,
                                            persisted, VG_DISCOVER_MAX_SLAVES);
    if(persisted_n > 0) {
      break;
    }

    usleep(100000);
  }
  if(persisted_n <= 0) {
    return 0;
  }

  if(persisted_n > max) {
    persisted_n = max;
  }

  for(i = 0; i < persisted_n; i++) {
    out[n].addr = persisted[i];
    out[n].probe_reg = 0;
    snprintf(out[n].label, sizeof(out[n].label), "addr=%u",
             (unsigned)persisted[i]);
    n++;
  }

  return n;
}

static int pick_latest_daily(FAR char *path, size_t path_sz)
{
  DIR *dir;
  struct dirent *ent;
  char best[64];
  char candidate[128];

  best[0] = '\0';

  dir = opendir(CONFIG_VG_HMI_REPORT_DIR);
  if(dir == NULL) {
    return -errno;
  }

  while((ent = readdir(dir)) != NULL) {
    if(strncmp(ent->d_name, "daily-", 6) != 0) {
      continue;
    }
    if(strstr(ent->d_name, ".md") == NULL) {
      continue;
    }
    if(best[0] == '\0' || strcmp(ent->d_name, best) > 0) {
      strncpy(best, ent->d_name, sizeof(best) - 1);
      best[sizeof(best) - 1] = '\0';
    }
  }

  closedir(dir);

  if(best[0] == '\0') {
    return -ENOENT;
  }

  snprintf(candidate, sizeof(candidate), "%s/%s",
           CONFIG_VG_HMI_REPORT_DIR, best);
  if(path != NULL && path_sz > 0) {
    snprintf(path, path_sz, "%s", candidate);
  }

  return 0;
}

static int board_read_latest_report(char *body, size_t body_sz,
                                    char *path, size_t path_sz)
{
  char file_path[128];
  int fd;
  ssize_t n;
  size_t total = 0;

  if(pick_latest_daily(file_path, sizeof(file_path)) != 0) {
    if(path != NULL && path_sz > 0) {
      snprintf(path, path_sz, "%s/daily-*.md", CONFIG_VG_HMI_REPORT_DIR);
    }
    if(body != NULL && body_sz > 0) {
      snprintf(body, body_sz,
               "暂无日报文件。\n\n路径: %s\n(需 net+emmc 预设落盘)",
               CONFIG_VG_HMI_REPORT_DIR);
    }
    return -ENOENT;
  }

  if(path != NULL && path_sz > 0) {
    snprintf(path, path_sz, "%s", file_path);
  }

  if(body == NULL || body_sz == 0) {
    return 0;
  }

  fd = open(file_path, O_RDONLY);
  if(fd < 0) {
    snprintf(body, body_sz, "无法打开: %s (%d)", file_path, errno);
    return -errno;
  }

  while(total + 1 < body_sz) {
    n = read(fd, body + total, body_sz - 1 - total);
    if(n <= 0) {
      break;
    }
    total += (size_t)n;
  }

  close(fd);
  body[total] = '\0';
  return 0;
}

int vg_ui_backend_scan_last_result(void)
{
  return g_scan_last_rc;
}

int vg_ui_backend_apply_last_result(void)
{
  return g_apply_last_rc;
}

void vg_ui_backend_scan_progress(int *cur_addr, int *addr_max)
{
  FAR const struct vg_discover_summary *sum = vg_discover_state();

  if(cur_addr != NULL) {
    *cur_addr = (sum != NULL) ? sum->scan_addr : 0;
  }
  if(addr_max != NULL) {
    *addr_max = (sum != NULL) ? sum->scan_addr_max : 0;
  }
}

static const vg_ui_backend_t s_board_backend = {
  .discover_scan_start   = board_discover_scan_start,
  .discover_scan_status  = board_discover_scan_status,
  .discover_apply_start  = board_discover_apply_start,
  .discover_apply_status = board_discover_apply_status,
  .get_slaves            = board_get_slaves,
  .read_latest_report    = board_read_latest_report,
};

#define VG_LIVE_MAX 64

static float g_live_v[VG_LIVE_MAX];
static uint8_t g_live_on[VG_LIVE_MAX];
static volatile int g_live_n;
static volatile bool g_acq_started;

static bool board_bus_busy(void)
{
  return g_scan_thread_active || g_scan_status == VG_SCAN_RUNNING ||
         g_apply_thread_active || g_apply_status == VG_SCAN_RUNNING;
}

static FAR void *vg_hmi_acq_thread(FAR void *arg)
{
  static int live_ok_logs;
  (void)arg;

  for(;;) {
    int i;
    int n = vg_mthings_point_count;
    int ok = 0;
    float a1 = 0.0f;

    if(n > VG_LIVE_MAX) {
      n = VG_LIVE_MAX;
    }

    if(board_bus_busy()) {
      usleep(200000);
      continue;
    }

    {
      uint8_t addrs[VG_LIVE_MAX];
      uint16_t regs[VG_LIVE_MAX];
      uint16_t raws[VG_LIVE_MAX];
      uint8_t oks[VG_LIVE_MAX];
      int rc;

      for(i = 0; i < n; i++) {
        addrs[i] = vg_mthings_points[i].addr;
        regs[i] = vg_mthings_points[i].reg;
      }

      rc = vg_discover_poll_holding(vg_hmi_rs485_devpath(),
                                    CONFIG_VG_HMI_DISCOVER_BAUD,
                                    addrs, regs, raws, oks, n,
                                    CONFIG_VG_HMI_DISCOVER_INTER_MS);
      if(rc != 0) {
        printf("vghmi: live open failed rc=%d\n", rc);
        usleep(500000);
        continue;
      }

      for(i = 0; i < n; i++) {
        const vg_mthings_point_t *p = &vg_mthings_points[i];

        if(oks[i]) {
          float v = p->is_signed ? ((float)(int16_t)raws[i] * p->scale)
                                 : ((float)raws[i] * p->scale);
          g_live_v[i] = v;
          g_live_on[i] = 1;
          ok++;
          if(p->addr == 1 && p->reg == 0) {
            a1 = v;
          }
        }
        else {
          g_live_on[i] = 0;
        }
      }
    }

    g_live_n = n;
    if(live_ok_logs < 3) {
      printf("vghmi: live ok=%d/%d a1=%.1f\n", ok, n, (double)a1);
      live_ok_logs++;
    }
    usleep(200000);
  }

  return NULL;
}

void vg_ui_backend_acq_start(void)
{
  pthread_t tid;
  pthread_attr_t attr;

  if(g_acq_started) {
    return;
  }

  g_acq_started = true;
  pthread_attr_init(&attr);
  pthread_attr_setstacksize(&attr, VG_HMI_SCAN_STACKSIZE);
  if(pthread_create(&tid, &attr, vg_hmi_acq_thread, NULL) != 0) {
    pthread_attr_destroy(&attr);
    g_acq_started = false;
    printf("vghmi: acq start failed\n");
    return;
  }

  pthread_attr_destroy(&attr);
  pthread_detach(tid);
  printf("vghmi: acq start ok points=%d\n", vg_mthings_point_count);
}

bool vg_ui_backend_apply_live(void)
{
  int i;
  int n = g_live_n;
  bool changed = false;

  if(n <= 0) {
    return false;
  }

  if(n > VG_LIVE_MAX) {
    n = VG_LIVE_MAX;
  }

  for(i = 0; i < n; i++) {
    if(vg_model_set_live((uint16_t)i, g_live_v[i], g_live_on[i] != 0)) {
      changed = true;
    }
  }

  return changed;
}

const vg_ui_backend_t *vg_ui_backend_get(void)
{
  return &s_board_backend;
}
