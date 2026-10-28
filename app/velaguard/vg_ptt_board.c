/****************************************************************************
 * app/velaguard/vg_ptt_board.c
 *
 * USER1 按住说话板端适配层。
 *
 * 线程模型：
 *   ptt 线程 open("/dev/buttons")，用 poll() 等待按键事件；每次醒来读一次
 *   电平并喂给 vg_ptt。没有按键时 poll 按 VG_PTT_STOP_POLL_MS 到期返回，
 *   因此停止请求有界；有按键时由驱动唤醒。去抖确认与 15 秒上限都靠
 *   vg_ptt_next_timeout_ms() 给出的到期时刻醒来，不做固定周期轮询。
 *
 * 事件回调在本线程执行，不得阻塞，也不得调用 vg_ptt_* 的写接口。
 ****************************************************************************/

#include <nuttx/config.h>

#ifdef CONFIG_VG_PTT

#include "vg_ptt_board.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/* BUTTON_USER and the bit position behind it come from the board header, not
 * from <nuttx/input/buttons.h>; the two must be read together. */

#include <arch/board/board.h>
#include <nuttx/board.h>
#include <nuttx/input/buttons.h>
#include <nuttx/sched.h>

#ifndef CONFIG_VG_PTT_DEVPATH
#  define CONFIG_VG_PTT_DEVPATH "/dev/buttons"
#endif

#ifndef CONFIG_VG_PTT_STACKSIZE
#  define CONFIG_VG_PTT_STACKSIZE 4096
#endif

#ifndef CONFIG_VG_PTT_PRIORITY
#  define CONFIG_VG_PTT_PRIORITY 90
#endif

#define VG_PTT_STOP_DEFAULT_MS 2000

/* 空闲等待的安全窗口。按键边沿由 EXTI 唤醒 poll，板端已实测可唤醒：一次
 * 110 ms 的按下被完整记录成 begin/submit，而 1 Hz 采样不可能量出这个时长。
 *
 * 这个窗口不用于发现按键，只保证两件事有确定上界：停止请求，以及状态机
 * 自己给出的到期时刻（去抖确认、15 秒上限）即使被漏唤醒也能补上。1 Hz
 * 的 poll 超时加一次 read() 不构成对采集或界面有影响的忙轮询。
 */

#define VG_PTT_SAFETY_WINDOW_MS  1000

/* 板端可视反馈：按键事件直接驱动一颗 LED，按住亮、松开灭，便于在没有
 * 音频和界面之前确认按键通路。LD3（NuttX 的绿灯位，实际是 Arduino 连接器
 * 旁的 LD8，PD3）专用于按键，心跳用的是 LD1（PJ2），两者不会混淆。
 */

#ifndef CONFIG_VG_PTT_LED
#  define CONFIG_VG_PTT_LED 2
#endif

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;

/* 线程 detach 后不能 join，只用这个标志表示是否曾经成功启动过 */

static bool g_thread_valid;
static volatile bool g_stop;
static bool g_running;
static bool g_led_ready;
static char g_devpath[32] = CONFIG_VG_PTT_DEVPATH;

static struct vg_ptt g_ptt;
static struct vg_ptt_stats g_stats;
static vg_ptt_event_fn g_callback;
static void *g_callback_arg;

/**
  * @brief  读取单调时钟毫秒。
  * @retval 自开机起的毫秒数，截断到 32 位；回绕由 vg_ptt 的无符号相减处理。
  */
static uint32_t now_ms(void)
{
  struct timespec ts;

  if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
    {
      return 0;
    }

  return (uint32_t)((uint64_t)ts.tv_sec * 1000ull +
                    (uint64_t)(ts.tv_nsec / 1000000ull));
}

/**
  * @brief  把一次按键读取结果换算成电平。
  * @note   board_buttons() 的位含义来自 board.h：位为 1 表示按下。
  * @param  sample  驱动返回的位集合。
  * @retval true 表示 USER1 处于按下状态。
  */
static bool sample_pressed(btn_buttonset_t sample)
{
  return (sample & (1u << BUTTON_USER)) != 0;
}

/**
  * @brief  点亮或熄灭按键反馈 LED。
  * @param  on  true 点亮。
  * @retval None
  */
static void led_set(bool on)
{
  if (g_led_ready)
    {
      board_userled(CONFIG_VG_PTT_LED, on);
    }
}

/**
  * @brief  默认事件处理：没有音频层时做板端可视反馈与可追踪输出。
  * @note   录音中 LED 常亮，提交后熄灭，因此按键是否被受理可以直接看板子。
  *         BVO-03/05 接入录音后由上层替换，这里不保存任何音频数据。
  * @param  rec  事件记录。
  * @param  arg  未使用。
  * @retval None
  */
static void default_handler(const struct vg_ptt_event_record *rec, void *arg)
{
  (void)arg;

  if (rec->type == VG_PTT_EV_BEGIN)
    {
      led_set(true);
      printf("vgptt: begin (press_ms=%u)\n", (unsigned)rec->press_ms);
    }
  else if (rec->type == VG_PTT_EV_SUBMIT)
    {
      led_set(false);
      printf("vgptt: submit %s duration_ms=%u\n",
             vg_ptt_reason_name(rec->reason),
             (unsigned)rec->duration_ms);
    }
}

/**
  * @brief  记录一次事件到统计并交给上层。
  * @param  rec  事件记录。
  * @retval None
  */
static void publish(const struct vg_ptt_event_record *rec)
{
  vg_ptt_event_fn fn;

  pthread_mutex_lock(&g_lock);

  if (rec->type == VG_PTT_EV_BEGIN)
    {
      g_stats.begins++;
    }
  else if (rec->type == VG_PTT_EV_SUBMIT)
    {
      g_stats.submits++;
      g_stats.last_duration_ms = rec->duration_ms;
      if (rec->reason == VG_PTT_SUBMIT_TIMEOUT)
        {
          g_stats.timeouts++;
        }
    }

  fn = g_callback;
  pthread_mutex_unlock(&g_lock);

  /* 回调在锁外执行，避免上层处理阻塞统计读取 */

  fn(rec, g_callback_arg);
}

/**
  * @brief  读一次按键电平并推进状态机。
  * @param  fd  /dev/buttons。
  * @retval 0   成功（可能没有事件）。
  * @retval <0  读取失败。
  */
static int poll_once(int fd)
{
  struct vg_ptt_event_record rec;
  btn_buttonset_t sample = 0;
  uint32_t now;
  ssize_t n;
  bool raw;

  n = read(fd, &sample, sizeof(sample));
  if (n != (ssize_t)sizeof(sample))
    {
      pthread_mutex_lock(&g_lock);
      g_stats.read_errors++;
      pthread_mutex_unlock(&g_lock);
      return -1;
    }

  raw = sample_pressed(sample);

  /* 原始电平直接跟 LED：不经过去抖与状态机，按键有没有被读到一眼可见 */

  led_set(raw);

  now = now_ms();

  pthread_mutex_lock(&g_lock);
  g_stats.samples++;
  if (raw)
    {
      g_stats.raw_highs++;
    }

  g_stats.last_event_ms = now;
  vg_ptt_update(&g_ptt, raw, now, &rec);
  g_stats.state = g_ptt.state;
  pthread_mutex_unlock(&g_lock);

  if (rec.type != VG_PTT_EV_NONE)
    {
      publish(&rec);
    }

  return 0;
}

/**
  * @brief  ptt 线程主体。
  * @param  arg  未使用。
  * @retval NULL
  */
static int ptt_task(int argc, char *argv[])
{
  struct pollfd pfd;
  int fd;
  uint32_t now;

  (void)argc;
  (void)argv;

  fd = open(g_devpath, O_RDONLY);
  if (fd < 0)
    {
      printf("vgptt: open %s failed: %d\n", g_devpath, errno);
      pthread_mutex_lock(&g_lock);
      g_running = false;
      pthread_mutex_unlock(&g_lock);
      return 1;
    }

  /* 以当前电平作为起点：上电即按住不会被当成一次会话 */

  {
    btn_buttonset_t sample = 0;

    if (read(fd, &sample, sizeof(sample)) == (ssize_t)sizeof(sample))
      {
        now = now_ms();
        pthread_mutex_lock(&g_lock);
        vg_ptt_init(&g_ptt, sample_pressed(sample), now);
        g_stats.state = g_ptt.state;
        g_stats.last_event_ms = now;
        g_stats.running = true;
        pthread_mutex_unlock(&g_lock);
      }
  }

  /* 按键反馈 LED：确认板上有输出控制权再点亮，避免和心跳 LED 抢同一个 */

  if (board_userled_initialize() > (uint32_t)CONFIG_VG_PTT_LED)
    {
      g_led_ready = true;
      led_set(false);
    }

  pthread_mutex_lock(&g_lock);
  g_stats.opened = true;
  g_stats.running = true;
  g_stats.state = g_ptt.state;
  pthread_mutex_unlock(&g_lock);

  printf("vgptt: watching %s (led=%d)\n", g_devpath, CONFIG_VG_PTT_LED);

  while (!g_stop)
    {
      int timeout_ms;
      int ret;

      /* 等待截止取状态机给出的最近到期时刻（去抖确认或 15 秒上限），没有
       * 待办事项时用安全窗口，让停止请求有确定上界。 */

      pthread_mutex_lock(&g_lock);
      timeout_ms = vg_ptt_next_timeout_ms(&g_ptt, now_ms());
      pthread_mutex_unlock(&g_lock);

      if (timeout_ms < 0 || (uint32_t)timeout_ms > VG_PTT_SAFETY_WINDOW_MS)
        {
          timeout_ms = (int)VG_PTT_SAFETY_WINDOW_MS;
        }

      pfd.fd = fd;
      pfd.events = POLLIN;
      pfd.revents = 0;

      ret = poll(&pfd, 1, timeout_ms);
      if (ret < 0)
        {
          if (errno == EINTR)
            {
              continue;
            }

          pthread_mutex_lock(&g_lock);
          g_stats.poll_errors++;
          pthread_mutex_unlock(&g_lock);

          /* poll 失败时退避，避免错误循环占满 CPU */

          usleep(100 * 1000);
          continue;
        }

      if (g_stop)
        {
          break;
        }

      pthread_mutex_lock(&g_lock);
      if (ret == 0)
        {
          g_stats.wake_timeouts++;
        }
      else
        {
          g_stats.wake_events++;
        }

      pthread_mutex_unlock(&g_lock);

      /* 驱动只在状态变化时通知；到期醒来时没有按键事件也要推进状态机，
       * 否则 15 秒上限不会被判定。 */

      (void)poll_once(fd);
    }

  close(fd);

  led_set(false);

  pthread_mutex_lock(&g_lock);
  g_stats.opened = false;
  g_running = false;
  pthread_mutex_unlock(&g_lock);

  printf("vgptt: stopped\n");
  return 0;
}

int vg_ptt_start(const char *devpath, vg_ptt_event_fn fn, void *arg)
{
  int task;

  pthread_mutex_lock(&g_lock);

  if (g_running)
    {
      pthread_mutex_unlock(&g_lock);
      return 0;
    }

  if (devpath != NULL && devpath[0] != '\0')
    {
      strncpy(g_devpath, devpath, sizeof(g_devpath) - 1);
      g_devpath[sizeof(g_devpath) - 1] = '\0';
    }

  g_callback = (fn != NULL) ? fn : default_handler;
  g_callback_arg = arg;
  g_stop = false;

  /* 统计清零；状态机在按键任务里按当前电平重新初始化 */

  memset(&g_stats, 0, sizeof(g_stats));
  g_stats.state = g_ptt.state;

  pthread_mutex_unlock(&g_lock);

  /* 用 task_create 而不是 pthread：NSH 里的 builtin 命令跑在自己的任务组
   * 里，命令返回后该组连同组内 pthread 一起被回收，按键线程会随之消失。
   * 任务属于系统任务组，不受单条 NSH 命令的生命周期影响。 */

  task = task_create("vgptt", CONFIG_VG_PTT_PRIORITY,
                     CONFIG_VG_PTT_STACKSIZE, ptt_task, NULL);
  if (task < 0)
    {
      printf("vgptt: task create failed: %d\n", task);
      return task;
    }

  pthread_mutex_lock(&g_lock);
  g_thread_valid = true;
  g_running = true;
  pthread_mutex_unlock(&g_lock);

  return 0;
}

int vg_ptt_stop(int wait_ms)
{
  bool valid;
  int waited = 0;

  if (wait_ms <= 0)
    {
      wait_ms = VG_PTT_STOP_DEFAULT_MS;
    }

  pthread_mutex_lock(&g_lock);
  valid = g_thread_valid;
  g_stop = true;
  pthread_mutex_unlock(&g_lock);

  if (!valid)
    {
      return 0;
    }

  /* 按键任务最多在一个采样周期内看到 g_stop；默认等待 2 秒留有充足余量。 */

  while (waited < wait_ms)
    {
      pthread_mutex_lock(&g_lock);
      if (!g_running)
        {
          g_thread_valid = false;
          pthread_mutex_unlock(&g_lock);
          return 0;
        }

      pthread_mutex_unlock(&g_lock);

      usleep(20 * 1000);
      waited += 20;
    }

  printf("vgptt: stop timed out after %d ms\n", wait_ms);
  return -1;
}

void vg_ptt_get_stats(struct vg_ptt_stats *out)
{
  if (out == NULL)
    {
      return;
    }

  pthread_mutex_lock(&g_lock);
  *out = g_stats;
  pthread_mutex_unlock(&g_lock);
}

void vg_ptt_reset_stats(void)
{
  bool running;
  bool opened;

  pthread_mutex_lock(&g_lock);

  running = g_stats.running;
  opened = g_stats.opened;
  memset(&g_stats, 0, sizeof(g_stats));
  g_stats.running = running;
  g_stats.opened = opened;
  g_stats.state = g_ptt.state;

  pthread_mutex_unlock(&g_lock);
}

int vg_ptt_probe_level(bool *pressed)
{
  btn_buttonset_t sample = 0;
  ssize_t n;
  int fd;

  if (pressed == NULL)
    {
      return -EINVAL;
    }

  fd = open(g_devpath, O_RDONLY);
  if (fd < 0)
    {
      return -errno;
    }

  n = read(fd, &sample, sizeof(sample));
  close(fd);

  if (n != (ssize_t)sizeof(sample))
    {
      return (n < 0) ? (int)n : -EIO;
    }

  *pressed = sample_pressed(sample);
  return 0;
}

void vg_ptt_get_state(struct vg_ptt *out)
{
  if (out == NULL)
    {
      return;
    }

  pthread_mutex_lock(&g_lock);
  *out = g_ptt;
  pthread_mutex_unlock(&g_lock);
}

#endif /* CONFIG_VG_PTT */
