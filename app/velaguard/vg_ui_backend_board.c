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
#include "model/vg_model.h"
#include "vg_discover.h"
#ifdef CONFIG_VG_AGENT_OPS
#include "vg_agent_alarm.h"
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

#ifndef CONFIG_VG_HMI_DISCOVER_ADDR_MIN
#  define CONFIG_VG_HMI_DISCOVER_ADDR_MIN 1
#endif

#ifndef CONFIG_VG_HMI_DISCOVER_ADDR_MAX
#  define CONFIG_VG_HMI_DISCOVER_ADDR_MAX 32
#endif

#ifndef CONFIG_VG_HMI_REPORT_DIR
#  define CONFIG_VG_HMI_REPORT_DIR "/data/velaguard/reports"
#endif

#ifndef CONFIG_VG_DISCOVER_POINTS_PATH
#  define CONFIG_VG_DISCOVER_POINTS_PATH "/data/velaguard/config/points.json"
#endif

#ifndef CONFIG_VG_CONFIG_BASEDIR
#  define CONFIG_VG_CONFIG_BASEDIR "/data/velaguard/config"
#endif

#ifndef CONFIG_VG_LIVE_VALUES_PATH
#  define CONFIG_VG_LIVE_VALUES_PATH "/data/velaguard/live/values.txt"
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

  while(vg_bus_try_lock() != 0) {
    usleep(50000);
  }

  vg_discover_reset(sum);
  rc = vg_bus_scan(sum, vg_hmi_rs485_devpath(),
                   CONFIG_VG_HMI_DISCOVER_BAUD,
                   g_scan_amin, g_scan_amax,
                   CONFIG_VG_HMI_DISCOVER_INTER_MS);
  vg_bus_unlock();
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

  while(vg_bus_try_lock() != 0) {
    usleep(50000);
  }

  if(sum == NULL || sum->n_hits <= 0) {
    vg_bus_unlock();
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
    vg_bus_unlock();
    g_apply_last_rc = -ENOENT;
    g_apply_status = -1;
    g_apply_thread_active = false;
    return NULL;
  }

  rc = vg_point_table_apply(sum, CONFIG_VG_DISCOVER_POINTS_PATH,
                            CONFIG_VG_CONFIG_BASEDIR, true);
  vg_bus_unlock();
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
static uint32_t g_imported_gen;
static struct vg_live_snapshot g_live_snap_out;

static void import_live_to_model(void)
{
  struct vg_discover_summary live;
  vg_runtime_point_t pts[VG_DISCOVER_MAX_POINTS];
  int i;
  int n;

  n = vg_live_points_copy(&live);
  if(n < 0) {
    n = 0;
  }
  if(n > VG_DISCOVER_MAX_POINTS) {
    n = VG_DISCOVER_MAX_POINTS;
  }

  for(i = 0; i < n; i++) {
    FAR const struct vg_point_entry *p = &live.points[i];

    memset(&pts[i], 0, sizeof(pts[i]));
    snprintf(pts[i].id, sizeof(pts[i].id), "%s", p->id);
    snprintf(pts[i].name, sizeof(pts[i].name), "%s",
             p->name[0] ? p->name : p->id);
    pts[i].addr = p->addr;
    pts[i].fc = p->fc;
    pts[i].reg = p->reg;
    snprintf(pts[i].unit, sizeof(pts[i].unit), "%s", p->unit);
    snprintf(pts[i].dtype, sizeof(pts[i].dtype), "%s", p->dtype);
    pts[i].scale = p->scale;
    snprintf(pts[i].cmp, sizeof(pts[i].cmp), "%s", p->cmp);
    pts[i].has_warn = p->has_warn;
    pts[i].has_crit = p->has_crit;
    pts[i].warn = p->warn;
    pts[i].crit = p->crit;
    pts[i].fail_n = p->fail_n;
  }

  vg_model_import_runtime_points(pts, n);
  g_imported_gen = vg_live_points_gen();
}

void vg_ui_backend_boot_points(void)
{
  int i;

  for(i = 0; i < 10; i++) {
    if(vg_live_points_load(CONFIG_VG_DISCOVER_POINTS_PATH) == 0) {
      break;
    }

    usleep(100000);
  }

  import_live_to_model();
}

static bool board_bus_busy(void)
{
  return g_scan_thread_active || g_scan_status == VG_SCAN_RUNNING ||
         g_apply_thread_active || g_apply_status == VG_SCAN_RUNNING;
}

static void write_live_snapshot(FAR const struct vg_discover_summary *live,
                                int n)
{
  int i;

  memset(&g_live_snap_out, 0, sizeof(g_live_snap_out));
  g_live_snap_out.tick_ms = vg_live_now_ms();
  if(n < 0) {
    n = 0;
  }
  if(n > VG_DISCOVER_MAX_POINTS) {
    n = VG_DISCOVER_MAX_POINTS;
  }

  g_live_snap_out.n = n;
  for(i = 0; i < n; i++) {
    FAR const struct vg_point_entry *p = &live->points[i];

    snprintf(g_live_snap_out.samples[i].id,
             sizeof(g_live_snap_out.samples[i].id), "%s", p->id);
    snprintf(g_live_snap_out.samples[i].unit,
             sizeof(g_live_snap_out.samples[i].unit), "%s", p->unit);
    if(g_live_on[i]) {
      g_live_snap_out.samples[i].ok = 1;
      g_live_snap_out.samples[i].value = g_live_v[i];
    }
  }

  (void)vg_live_snapshot_write(CONFIG_VG_LIVE_VALUES_PATH, &g_live_snap_out);
}

static FAR void *vg_hmi_acq_thread(FAR void *arg)
{
  static int live_ok_logs;
  (void)arg;

  for(;;) {
    struct vg_discover_summary live;
    int i;
    int n;
    int ok = 0;
    float a1 = 0.0f;

    if(board_bus_busy()) {
      usleep(200000);
      continue;
    }

    n = vg_live_points_copy(&live);
    if(n < 0) {
      n = 0;
    }
    if(n > VG_LIVE_MAX) {
      n = VG_LIVE_MAX;
    }

    if(n <= 0) {
      g_live_n = 0;
      write_live_snapshot(&live, 0);
      usleep(500000);
      continue;
    }

    if(vg_bus_try_lock() != 0) {
      usleep(200000);
      continue;
    }

    {
      uint16_t raws[VG_LIVE_MAX];
      uint8_t oks[VG_LIVE_MAX];
      int rc;

      rc = vg_discover_poll_points(vg_hmi_rs485_devpath(),
                                   CONFIG_VG_HMI_DISCOVER_BAUD,
                                   live.points, n,
                                   raws, oks,
                                   CONFIG_VG_HMI_DISCOVER_INTER_MS);
      vg_bus_unlock();
      if(rc != 0) {
        printf("vghmi: live open failed rc=%d\n", rc);
        for(i = 0; i < n; i++) {
          g_live_on[i] = 0;
        }
        g_live_n = n;
        write_live_snapshot(&live, n);
        usleep(500000);
        continue;
      }

      for(i = 0; i < n; i++) {
        FAR const struct vg_point_entry *p = &live.points[i];
        int signed_v = (strcmp(p->dtype, "uint16") != 0);

        if(oks[i]) {
          float v = signed_v ? ((float)(int16_t)raws[i] * p->scale)
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
    write_live_snapshot(&live, n);
    if(ok > 0) {
      if(live_ok_logs < 8) {
        printf("vghmi: live ok=%d/%d a1=%.1f\n", ok, n, (double)a1);
        live_ok_logs++;
      }
    }
    else if(live_ok_logs == 0) {
      printf("vghmi: live ok=0/%d a1=0.0\n", n);
      live_ok_logs = -1;
    }
    usleep(200000);
  }

  return NULL;
}

void vg_ui_backend_acq_start(void)
{
  pthread_t tid;
  pthread_attr_t attr;
  struct vg_discover_summary live;
  int n;

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
  n = vg_live_points_copy(&live);
  printf("vghmi: acq start ok points=%d\n", n);
}

bool vg_ui_backend_apply_live(void)
{
  int i;
  int n = g_live_n;
  bool changed = false;

  if(vg_live_points_gen() != g_imported_gen) {
    import_live_to_model();
    changed = true;
  }

  if(n <= 0) {
    return changed;
  }

  if(n > VG_LIVE_MAX) {
    n = VG_LIVE_MAX;
  }

  for(i = 0; i < n; i++) {
    if(vg_model_set_live((uint16_t)i, g_live_v[i], g_live_on[i] != 0)) {
      changed = true;
    }
  }

#ifdef CONFIG_VG_AGENT_OPS
  {
    const vg_alarm_t *a = vg_model_get_active_alarm();

    if(a != NULL && a->active) {
      const vg_sensor_t *s = vg_model_get_sensor(a->sensor_id);
      char buf[256];
      const char *type = (a->severity == VG_SEV_OFFLINE) ? "offline"
                                                        : "threshold";

      snprintf(buf, sizeof(buf),
               "type=%s\ntag=%s\nslave=%u\nreg=%ld\nvalue=%.4g\n"
               "threshold=%.4g\n"
               "hint=use alarm_interpretation skill\n",
               type,
               a->sensor_id,
               s ? (unsigned)s->slave_addr : 0u,
               s ? (long)s->reg_addr : 0L,
               (double)a->value,
               (double)a->threshold);
      (void)vg_pending_alarm_write(buf);
    }
  }
#endif

  return changed;
}

const vg_ui_backend_t *vg_ui_backend_get(void)
{
  return &s_board_backend;
}
