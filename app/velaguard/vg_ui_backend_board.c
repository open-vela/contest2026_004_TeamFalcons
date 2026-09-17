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
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "model/vg_ui_backend.h"
#include "model/vg_model.h"
#include "vg_discover.h"
#ifdef CONFIG_VG_NET_FAILOVER
#include "vg_net_mgr.h"
#endif
#include "vg_advice.h"
#include "vg_agent_alarm.h"
#ifdef CONFIG_EXAMPLES_AI_AGENT_VELA
#include "vg_agent_round.h"
#endif
#ifdef CONFIG_VG_FRAME_STATS
#include "vg_runtime.h"
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

static int pick_runtime_report(FAR char *path, size_t path_sz)
{
  char candidate[128];
  int fd;

  snprintf(candidate, sizeof(candidate), "%s/runtime-report.md",
           CONFIG_VG_HMI_REPORT_DIR);
  fd = open(candidate, O_RDONLY);
  if(fd < 0) {
    return -errno;
  }

  close(fd);
  if(path != NULL && path_sz > 0) {
    snprintf(path, path_sz, "%s", candidate);
  }

  return 0;
}

/* Today's local date, formatted as prefix<YYYY-MM-DD>suffix.
 * Returns false while the clock is still unsynced: "today" would be
 * meaningless then, and the daily report must not be keyed off it. */

static bool report_stamp(FAR char *out, size_t out_sz,
                         const char *prefix, const char *suffix)
{
  time_t now = time(NULL);
  struct tm tmv;
  char date[16];

  if(now < (time_t)1704067200) {   /* before 2024-01-01: not synced */
    return false;
  }

  if(localtime_r(&now, &tmv) == NULL) {
    return false;
  }

  if(strftime(date, sizeof(date), "%Y-%m-%d", &tmv) == 0) {
    return false;
  }

  snprintf(out, out_sz, "%s%s%s", prefix, date, suffix);
  return true;
}

/* The Agent's report for the current local day, or -ENOENT.
 *
 * This replaces a "largest daily-*.md wins" scan, which would happily serve
 * last week's file as today's report. Only the exact name for today counts. */

static int pick_agent_daily(FAR char *path, size_t path_sz)
{
  char want[64];
  char candidate[128];

  if(!report_stamp(want, sizeof(want), "daily-", ".md")) {
    return -ENOENT;
  }

  snprintf(candidate, sizeof(candidate), "%s/%s",
           CONFIG_VG_HMI_REPORT_DIR, want);

  if(access(candidate, R_OK) != 0) {
    return -errno;
  }

  if(path != NULL && path_sz > 0) {
    snprintf(path, path_sz, "%s", candidate);
  }

  return 0;
}

/* The 480x272 label cannot render markdown; reports are plain text now,
 * but older agent output may still carry decorations — drop them here. */

static void strip_markdown(char *s)
{
  char *r = s;
  char *w = s;

  while(*r != '\0') {
    char *e = strchr(r, '\n');
    char *line_end = (e != NULL) ? e : r + strlen(r);
    const char *p = r;
    bool fence_line;

    while(p < line_end && *p == ' ') {
      p++;
    }
    fence_line = (line_end - p >= 3 &&
                  p[0] == '`' && p[1] == '`' && p[2] == '`');
    if(!fence_line) {
      bool table_sep = false;

      while(p < line_end && *p == '#') {
        p++;
      }
      while(p < line_end && *p == ' ') {
        p++;
      }
      if(p < line_end && *p == '|') {
        const char *q;

        table_sep = true;
        for(q = p; q < line_end; q++) {
          if(*q != '|' && *q != '-' && *q != ':' && *q != ' ' &&
             *q != '\t') {
            table_sep = false;
            break;
          }
        }
      }
      if(!table_sep) {
        for(; p < line_end; p++) {
          if(*p == '*' || *p == '`') {
            continue;
          }
          *w++ = (*p == '|') ? ' ' : *p;
        }
      }
    }

    if(e != NULL) {
      *w++ = '\n';
      r = e + 1;
    }
    else {
      r = line_end;
    }
  }
  *w = '\0';
}

static int read_whole_file(const char *file_path, char *body, size_t body_sz)
{
  int fd;
  ssize_t n;
  size_t total = 0;

  fd = open(file_path, O_RDONLY);
  if(fd < 0) {
    return -errno;
  }

  while(body != NULL && body_sz > 0 && total + 1 < body_sz) {
    n = read(fd, body + total, body_sz - 1 - total);
    if(n <= 0) {
      break;
    }
    total += (size_t)n;
  }

  close(fd);

  if(body != NULL && body_sz > 0) {
    body[total] = '\0';
  }

  return 0;
}

/* Pick the report to show and say where it came from.
 *
 * Order matters.  The firmware report is rewritten on every request, so any
 * "which file is newer" comparison would always pick it; the Agent's report
 * wins purely on being today's and on passing vg_ai_report_validate().  When
 * it is absent, stale, or rejected, the firmware statistics are shown with
 * from_agent false, which is what keeps the offline behaviour honest. */

/* Today's agent report, read off eMMC and put through the board-side
 * validator.  Returns 0 only for a report the page may show as Agent output.
 * Kept separate from the page path so the scheduler can ask the same
 * question: "is today's report done?" means "validated", not "a file with
 * that name exists". */

#define VG_HMI_DAILY_RAW 1600

static int agent_daily_load(char *body, size_t body_sz,
                            char *path, size_t path_sz)
{
  static char raw[VG_HMI_DAILY_RAW];
  char file_path[128];
  char today[16];
  struct stat st;

  if(!report_stamp(today, sizeof(today), "", "") ||
     pick_agent_daily(file_path, sizeof(file_path)) != 0) {
    return -ENOENT;
  }

  if(read_whole_file(file_path, raw, sizeof(raw)) != 0 ||
     stat(file_path, &st) != 0) {
    return -EIO;
  }

  /* The validator works on the C string, so a byte that the file has but
   * strlen() does not see would leave the tail unexamined.  Anything that
   * does not match the file size is either an embedded NUL or a file larger
   * than the buffer, and both are rejected outright. */

  if((size_t)st.st_size != strlen(raw)) {
    return -EINVAL;
  }

  if(vg_ai_report_validate(raw, strlen(raw), today,
                           (long)st.st_mtime, (long)time(NULL)) != VG_AI_OK) {
    return -EINVAL;
  }

  if(body != NULL && body_sz > 0) {
    snprintf(body, body_sz, "%s", raw);
  }

  if(path != NULL && path_sz > 0) {
    snprintf(path, path_sz, "%s", file_path);
  }

  return 0;
}

static int board_read_latest_report(char *body, size_t body_sz,
                                    char *path, size_t path_sz,
                                    bool *from_agent)
{
  char file_path[128];
  bool ok = false;

  if(from_agent != NULL) {
    *from_agent = false;
  }

  if(body != NULL && body_sz > 0 &&
     agent_daily_load(body, body_sz, file_path, sizeof(file_path)) == 0) {
    ok = true;
    if(path != NULL && path_sz > 0) {
      snprintf(path, path_sz, "%s", file_path);
    }
    if(from_agent != NULL) {
      *from_agent = true;
    }
  }

  if(!ok) {
    if(pick_runtime_report(file_path, sizeof(file_path)) != 0) {
      if(path != NULL && path_sz > 0) {
        snprintf(path, path_sz, "%s/runtime-report.md",
                 CONFIG_VG_HMI_REPORT_DIR);
      }
      if(body != NULL && body_sz > 0) {
        snprintf(body, body_sz,
                 "暂无运行报告。\n\n路径: %s/runtime-report.md\n"
                 "点右上角刷新可更新本次上电以来的运行概况。",
                 CONFIG_VG_HMI_REPORT_DIR);
      }
      return -ENOENT;
    }

    if(path != NULL && path_sz > 0) {
      snprintf(path, path_sz, "%s", file_path);
    }

    if(body != NULL && body_sz > 0 && read_whole_file(file_path, body, body_sz) != 0) {
      snprintf(body, body_sz, "无法打开: %s (%d)", file_path, errno);
      return -errno;
    }
  }

  if(body != NULL && body_sz > 0) {
    strip_markdown(body);
  }

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

/* Ask the board agent to write today's report.
 *
 * The old body was a stub that only returned true, because a ReAct round
 * from the heartbeat poke used to HARDFAULT the board.  That root cause (an
 * unbounded offset accumulation in the skill summary) was fixed on
 * 2026-09-16, and the round is now driven from this file worker through
 * vg_agent_round rather than from the heartbeat, so nothing pokes the agent
 * behind the scheduler's back.  The firmware report is still written and
 * still read whenever the agent's answer is missing or fails validation. */

#ifndef VG_HMI_DAILY_REQUEST
#  define VG_HMI_DAILY_REQUEST \
    "请按 /data/agent/skills/operations_report.md 生成今日运行日报，" \
    "写入 /data/velaguard/reports/%s"
#endif

/* Returns the queue result rather than a bool: -EBUSY means the channel is
 * momentarily held by the other flow and is worth retrying on the next tick,
 * while anything else means the request never reached the agent and should
 * cost a full retry interval. */

static int daily_round_submit(void)
{
#ifdef CONFIG_EXAMPLES_AI_AGENT_VELA
  char req[224];
  char want[64];

  /* Pin the exact filename here.  The board knows today's date from its own
   * clock; leaving the model to translate a date into a path is one more
   * place for an otherwise good round to land on the wrong name. */

  if(!report_stamp(want, sizeof(want), "daily-", ".md")) {
    return -ENODEV;
  }

  snprintf(req, sizeof(req), VG_HMI_DAILY_REQUEST, want);
  return vg_agent_round_queue_owned(req, VG_AGENT_ROUND_OWNER_DAILY);
#else
  return -ENODEV;
#endif
}

static bool board_request_daily_report(void)
{
  return (daily_round_submit() == 0);
}

#define VG_DAILY_RETRY_MS 300000u

/* Ask for today's report once, then leave the channel alone.
 *
 * "Already have it" means today's file is there and passes the board-side
 * validator, so no marker file is needed, a bad report does not silence the
 * flow for the rest of the day, and a failed attempt (offline, or the agent
 * erroring out) simply retries after VG_DAILY_RETRY_MS. */

static void daily_maybe_request(void)
{
#ifdef CONFIG_EXAMPLES_AI_AGENT_VELA
  static uint32_t last_try_ms;
  char want[64];
  char path[128];
  struct stat st;
  uint32_t now = vg_agent_round_now_ms();

  /* Our own round is over.  The artifact is the file, so there is nothing to
   * read from the reply here; releasing the channel is all this flow owes,
   * and the advice flow must not be handed a round it did not ask for. */

  if(vg_agent_round_owner() == VG_AGENT_ROUND_OWNER_DAILY) {
    enum vg_agent_round_state rst = vg_agent_round_state();
    if(rst == VG_AGENT_ROUND_DONE || rst == VG_AGENT_ROUND_ERROR) {
      vg_agent_round_clear();
    }
  }

  /* Same reason as in the advice flow: an unclaimed finished round blocks
   * the strict queue, and nothing else will ever release it. */

  vg_agent_round_reclaim(VG_AGENT_ROUND_OWNER_DAILY, VG_AGENT_ROUND_RECLAIM_MS);

  if(!report_stamp(want, sizeof(want), "daily-", ".md")) {
    return;                    /* local clock not usable yet */
  }

  /* "Already have it" means today's report is there and passes validation.
   * Keying off the file name alone would let one bad report — wrong date
   * line, missing marker — silence the flow for the rest of the day while
   * the page quietly shows the firmware fallback. */

  snprintf(path, sizeof(path), "%s/%s", CONFIG_VG_HMI_REPORT_DIR, want);
  if(stat(path, &st) == 0 && agent_daily_load(NULL, 0, NULL, 0) == 0) {
    return;
  }

  if(vg_agent_round_busy()) {
    return;                    /* an advice round owns the channel */
  }

  if(last_try_ms != 0 && now - last_try_ms < VG_DAILY_RETRY_MS) {
    return;
  }

  /* Only a request that took the channel, or one the agent never received,
   * counts as an attempt.  -EBUSY means the advice flow still owns an
   * unconsumed result, which clears within a tick, so it must not cost five
   * minutes of backoff; a delivery failure must, or the daily flow would
   * retry every tick and fill the agent's queue by itself. */

  {
    int rc = daily_round_submit();

    if(rc == 0) {
      last_try_ms = now;
      printf("vghmi: daily report requested\n");
    }
    else if(rc != -EBUSY) {
      last_try_ms = now;
      printf("vghmi: daily report not delivered (%d)\n", rc);
    }
  }
#endif /* CONFIG_EXAMPLES_AI_AGENT_VELA */
}

#define VG_HMI_FILE_WORKER_STACKSIZE 8192

typedef enum {
  VG_ASYNC_ALARM_NONE = 0,
  VG_ASYNC_ALARM_CLEAR,
  VG_ASYNC_ALARM_WRITE,
} vg_async_alarm_op_t;

typedef struct {
  vg_async_alarm_op_t op;
  uint32_t req_id;
  char buf[256];
} vg_async_alarm_req_t;

static pthread_mutex_t g_file_worker_lock = PTHREAD_MUTEX_INITIALIZER;
static bool g_file_worker_started = false;

static vg_async_alarm_req_t g_alarm_req;
static uint32_t g_alarm_req_id = 0;
static uint32_t g_alarm_done_id = 0;

static vg_ui_report_snapshot_t g_report_snap;
static bool g_report_req_pending = false;
static bool g_report_allow_gen = false;
static uint32_t g_report_req_counter = 1;

static FAR void *vg_hmi_file_worker_thread(FAR void *arg)
{
  (void)arg;

  for(;;) {
    vg_async_alarm_req_t alarm_work;
    bool do_alarm = false;
    bool do_report_read = false;
    bool allow_gen = false;

    pthread_mutex_lock(&g_file_worker_lock);

    if(g_alarm_req.op != VG_ASYNC_ALARM_NONE && g_alarm_req.req_id != g_alarm_done_id) {
      alarm_work = g_alarm_req;
      do_alarm = true;
    }

    if(g_report_req_pending) {
      g_report_req_pending = false;
      allow_gen = g_report_allow_gen;
      do_report_read = true;
    }

    pthread_mutex_unlock(&g_file_worker_lock);

    if(do_alarm) {
      int rc = 0;
      if(alarm_work.op == VG_ASYNC_ALARM_CLEAR) {
        rc = vg_pending_alarm_clear();
      }
      else if(alarm_work.op == VG_ASYNC_ALARM_WRITE) {
        (void)vg_pending_alarm_clear();
        rc = vg_pending_alarm_write(alarm_work.buf);
      }
      pthread_mutex_lock(&g_file_worker_lock);
      if(rc == 0 || alarm_work.op == VG_ASYNC_ALARM_CLEAR) {
        g_alarm_done_id = alarm_work.req_id;
      }
      pthread_mutex_unlock(&g_file_worker_lock);
    }

    if(do_report_read) {
      char body[sizeof(g_report_snap.body)];
      char path[128];
      bool from_agent = false;
      int rc;

#ifdef CONFIG_VG_FRAME_STATS
      snprintf(path, sizeof(path), "%s/runtime-report.md",
               CONFIG_VG_HMI_REPORT_DIR);
      (void)mkdir(CONFIG_VG_HMI_REPORT_DIR, 0755);
      rc = vg_runtime_write_report(path);
      if(rc != 0) {
        printf("vghmi: runtime report write %d\n", rc);
      }
#endif
      if(allow_gen) {
        (void)board_request_daily_report();
      }

      rc = board_read_latest_report(body, sizeof(body), path, sizeof(path),
                                    &from_agent);
      pthread_mutex_lock(&g_file_worker_lock);
      if(rc == 0) {
        snprintf(g_report_snap.body, sizeof(g_report_snap.body), "%s", body);
        snprintf(g_report_snap.path, sizeof(g_report_snap.path), "%s", path);
        g_report_snap.status = VG_UI_REPORT_READY;
        g_report_snap.err = 0;
        g_report_snap.from_agent = from_agent;
        g_report_snap.truncated =
          (strlen(body) + 1 >= sizeof(g_report_snap.body));
        g_report_snap.version++;
      }
      else if(rc == -ENOENT) {
        g_report_snap.status = VG_UI_REPORT_EMPTY;
        g_report_snap.err = -ENOENT;
        g_report_snap.from_agent = false;
        g_report_snap.version++;
      }
      else {
        g_report_snap.status = VG_UI_REPORT_ERROR;
        g_report_snap.err = rc;
        g_report_snap.from_agent = false;
        g_report_snap.version++;
      }
      pthread_mutex_unlock(&g_file_worker_lock);
    }

    /* Agent rounds are driven from here, not from the heartbeat: the HMI
     * build keeps heartbeat_send() gated, and a second unsynchronised
     * trigger would race with this scheduler for the single callback slot. */

#ifdef CONFIG_EXAMPLES_AI_AGENT_VELA
    vg_agent_round_tick();
#endif
    /* The daily report asks at most once a day and is a hard requirement for
     * the report page, so it gets first refusal on the channel; a long
     * running alarm set would otherwise keep re-asking for advice and never
     * let the daily through. */

    daily_maybe_request();
    vg_advice_tick();

    usleep(100000);
  }

  return NULL;
}

static void ensure_file_worker_started(void)
{
  pthread_t tid;
  pthread_attr_t attr;

  pthread_mutex_lock(&g_file_worker_lock);
  if(g_file_worker_started) {
    pthread_mutex_unlock(&g_file_worker_lock);
    return;
  }
  g_file_worker_started = true;
  pthread_attr_init(&attr);
  pthread_attr_setstacksize(&attr, VG_HMI_FILE_WORKER_STACKSIZE);
  if(pthread_create(&tid, &attr, vg_hmi_file_worker_thread, NULL) != 0) {
    pthread_attr_destroy(&attr);
    g_file_worker_started = false;
    printf("vghmi: file worker start failed\n");
    pthread_mutex_unlock(&g_file_worker_lock);
    return;
  }
  pthread_attr_destroy(&attr);
  pthread_detach(tid);
  pthread_mutex_unlock(&g_file_worker_lock);
}

static int board_report_request(bool allow_generate, uint32_t *request_id)
{
  ensure_file_worker_started();
  pthread_mutex_lock(&g_file_worker_lock);
  if(g_report_snap.status == VG_UI_REPORT_GENERATING && !allow_generate) {
    if(request_id) {
      *request_id = g_report_snap.request_id;
    }
    pthread_mutex_unlock(&g_file_worker_lock);
    return 0;
  }

  g_report_snap.request_id = ++g_report_req_counter;
  if(request_id) {
    *request_id = g_report_snap.request_id;
  }
  g_report_snap.status = VG_UI_REPORT_READING;
  g_report_snap.version++;
  g_report_snap.err = 0;
  g_report_req_pending = true;
  g_report_allow_gen = allow_generate;
  pthread_mutex_unlock(&g_file_worker_lock);
  return 0;
}

static bool board_report_snapshot(vg_ui_report_snapshot_t *out)
{
  if(out == NULL) return false;
  ensure_file_worker_started();
  pthread_mutex_lock(&g_file_worker_lock);
  *out = g_report_snap;
  pthread_mutex_unlock(&g_file_worker_lock);
  return true;
}

static const vg_ui_backend_t s_board_backend = {
  .discover_scan_start   = board_discover_scan_start,
  .discover_scan_status  = board_discover_scan_status,
  .discover_apply_start  = board_discover_apply_start,
  .discover_apply_status = board_discover_apply_status,
  .get_slaves            = board_get_slaves,
  .read_latest_report    = board_read_latest_report,
  .request_daily_report  = board_request_daily_report,
  .report_request        = board_report_request,
  .report_snapshot       = board_report_snapshot,
};

#define VG_LIVE_MAX 64

static pthread_mutex_t g_live_lock = PTHREAD_MUTEX_INITIALIZER;
static float g_live_v[VG_LIVE_MAX];
static uint8_t g_live_on[VG_LIVE_MAX];
static volatile int g_live_n;
static volatile bool g_acq_started;
static uint32_t g_imported_gen;
/* Bumped by the acq thread after each completed poll; the 1 Hz model tick
 * must apply each poll snapshot exactly once, otherwise one failed poll
 * counts 2-5x in the offline sliding window and the counts flap. */
static volatile uint32_t g_live_cycle;
static struct vg_live_snapshot g_live_snap_out;

static void import_live_to_model(void)
{
  struct vg_discover_summary live;
  vg_runtime_point_t pts[VG_DISCOVER_MAX_POINTS];
  int i;
  int n;
  uint32_t gen = 0;

  n = vg_live_points_copy_versioned(&live, &gen);
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
  g_imported_gen = gen;
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
    uint32_t poll_gen = 0;

    if(board_bus_busy()) {
      usleep(200000);
      continue;
    }

    n = vg_live_points_copy_versioned(&live, &poll_gen);
    if(n < 0) {
      n = 0;
    }
    if(n > VG_LIVE_MAX) {
      n = VG_LIVE_MAX;
    }

    if(n <= 0) {
      pthread_mutex_lock(&g_live_lock);
      g_live_n = 0;
      write_live_snapshot(&live, 0);
      pthread_mutex_unlock(&g_live_lock);
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
        pthread_mutex_lock(&g_live_lock);
        for(i = 0; i < n; i++) {
          g_live_on[i] = 0;
        }
        g_live_n = n;
        write_live_snapshot(&live, n);
        g_live_cycle++;
        pthread_mutex_unlock(&g_live_lock);
        usleep(500000);
        continue;
      }

      pthread_mutex_lock(&g_live_lock);
      if(vg_live_points_gen() == poll_gen) {
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
        g_live_n = n;
        write_live_snapshot(&live, n);
        g_live_cycle++;
      }
      pthread_mutex_unlock(&g_live_lock);
    }

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
  int local_n;
  float local_v[VG_LIVE_MAX];
  uint8_t local_on[VG_LIVE_MAX];
  uint32_t cur_cycle;
  bool changed = false;
  static uint32_t applied_cycle;

  if(vg_live_points_gen() != g_imported_gen) {
    import_live_to_model();
    changed = true;
  }

  pthread_mutex_lock(&g_live_lock);
  cur_cycle = g_live_cycle;
  if(cur_cycle == applied_cycle) {
    pthread_mutex_unlock(&g_live_lock);
    return changed;
  }
  applied_cycle = cur_cycle;

  local_n = g_live_n;
  if(local_n > VG_LIVE_MAX) {
    local_n = VG_LIVE_MAX;
  }
  if(local_n > 0) {
    memcpy(local_v, g_live_v, sizeof(float) * local_n);
    memcpy(local_on, g_live_on, sizeof(uint8_t) * local_n);
  }
  pthread_mutex_unlock(&g_live_lock);

  if(local_n <= 0) {
    return changed;
  }

  for(i = 0; i < local_n; i++) {
    if(vg_model_set_live((uint16_t)i, local_v[i], local_on[i] != 0)) {
      changed = true;
    }
  }

#ifdef CONFIG_VG_FRAME_STATS
  {
    uint16_t sn = 0;
    const vg_sensor_t *sensors = vg_model_get_sensors(&sn);
    uint16_t j;

    vg_runtime_init();
    for(j = 0; j < sn; j++) {
      enum vg_runtime_kind kind = VG_RUNTIME_KIND_NONE;
      float thr = 0.0f;

      if(!sensors[j].online) {
        kind = VG_RUNTIME_KIND_OFFLINE;
      }
      else if(sensors[j].al_active &&
              sensors[j].severity == VG_SEV_OFFLINE) {
        kind = VG_RUNTIME_KIND_OFFLINE;
      }
      else if(sensors[j].al_active) {
        kind = VG_RUNTIME_KIND_THRESHOLD;
        if(sensors[j].severity == VG_SEV_CRIT && sensors[j].has_crit) {
          thr = sensors[j].thr_crit;
        }
        else if(sensors[j].has_warn) {
          thr = sensors[j].thr_warn;
        }
        else if(sensors[j].has_crit) {
          thr = sensors[j].thr_crit;
        }
      }

      vg_runtime_note_sample(sensors[j].id, sensors[j].slave_addr,
                             sensors[j].online, sensors[j].value, kind,
                             thr, sensors[j].cmp,
                             sensors[j].severity == VG_SEV_CRIT ? "crit" :
                             sensors[j].severity == VG_SEV_WARN ? "warn" :
                             sensors[j].severity == VG_SEV_OFFLINE ?
                               "offline" : NULL);
    }
  }
#endif

#ifdef CONFIG_VG_AGENT_OPS
  {
    /* pending_alarm.txt mirrors the primary alarm only (Agent contract:
     * single record, format unchanged). Rewrite when the primary flips to
     * a different point/kind; clear only when the LAST alarm resolves so
     * a recovering point never wipes a still-active neighbor's record. */
    static char prev_tag[40];
    const vg_alarm_t *a = vg_model_get_active_alarm();
    const uint16_t n_active = vg_model_active_alarm_count();
    char tag[40];

    if(n_active > 0 && a != NULL && a->active) {
      const vg_sensor_t *s = vg_model_get_sensor(a->sensor_id);
      char buf[256];
      const char *type = (a->severity == VG_SEV_OFFLINE) ? "offline"
                                                        : "threshold";

      snprintf(tag, sizeof(tag), "%s/%s", a->sensor_id, type);
      if(strcmp(tag, prev_tag) != 0) {
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
        ensure_file_worker_started();
        pthread_mutex_lock(&g_file_worker_lock);
        g_alarm_req.op = VG_ASYNC_ALARM_WRITE;
        g_alarm_req.req_id = ++g_alarm_req_id;
        snprintf(g_alarm_req.buf, sizeof(g_alarm_req.buf), "%s", buf);
        pthread_mutex_unlock(&g_file_worker_lock);
        snprintf(prev_tag, sizeof(prev_tag), "%s", tag);
      }
    }
    else if(prev_tag[0] != '\0') {
      ensure_file_worker_started();
      pthread_mutex_lock(&g_file_worker_lock);
      g_alarm_req.op = VG_ASYNC_ALARM_CLEAR;
      g_alarm_req.req_id = ++g_alarm_req_id;
      g_alarm_req.buf[0] = '\0';
      pthread_mutex_unlock(&g_file_worker_lock);
      prev_tag[0] = '\0';
    }
  }
#endif

  /* Hand the alarm set to the advice worker.  This function runs on the UI
   * thread, which owns vg_model; the worker reads the snapshot instead of
   * the model so it cannot race with a live acquisition update. */

  vg_advice_note_alarms();

  return changed;
}

bool vg_ui_backend_poll_net(vg_ui_net_live_t *out)
{
  if(out == NULL) {
    return false;
  }

  memset(out, 0, sizeof(*out));

  /* Alarm buzzer path: /dev/pwm0 (DO1) provisioned by the board pack */

  out->aud_ok = (access("/dev/pwm0", R_OK) == 0);

#ifdef CONFIG_VG_NET_FAILOVER
  {
    struct vg_net_live_status st;

    /* net_mgr caches its latest samples; this read does no AT UART IO */

    if(vg_net_mgr_status(&st) == 0) {
      out->rj45_has_ip = st.rj45_has_ip;
      out->wifi_assoc = st.wifi_assoc;
      out->wifi_has_ip = st.wifi_has_ip;
      out->mqtt_online = st.mqtt_online;
      out->egress = (int)st.egress;
      snprintf(out->ip, sizeof(out->ip), "%s", st.ip);
      return true;
    }
  }
#endif

  return false;
}

const vg_ui_backend_t *vg_ui_backend_get(void)
{
  return &s_board_backend;
}

int vg_ui_report_request(bool allow_generate, uint32_t *request_id)
{
  const vg_ui_backend_t *be = vg_ui_backend_get();
  if(be && be->report_request) {
    return be->report_request(allow_generate, request_id);
  }
  return -1;
}

bool vg_ui_report_snapshot(vg_ui_report_snapshot_t *out)
{
  const vg_ui_backend_t *be = vg_ui_backend_get();
  if(be && be->report_snapshot) {
    return be->report_snapshot(out);
  }
  return false;
}

