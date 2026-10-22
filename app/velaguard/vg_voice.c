/****************************************************************************
 * app/velaguard/vg_voice.c
 *
 * Licensed under the Apache License, Version 2.0.
 * BVO-03/05 本地录放诊断。音频状态只由常驻任务修改。
 ****************************************************************************/

#include <nuttx/config.h>
#ifdef CONFIG_VG_VOICE_LOOPBACK

#include "vg_voice.h"
#include "vg_pdm_filter.h"

#include <errno.h>
#include <fcntl.h>
#include <mqueue.h>
#include <pthread.h>
#include <sched.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>
#include <nuttx/clock.h>
#include <nuttx/irq.h>
#include <nuttx/spinlock.h>
#include <nuttx/mutex.h>
#include <nuttx/sched.h>
#include <nuttx/semaphore.h>
#include <nuttx/audio/audio.h>
#include <arch/board/board.h>
#include <system/nxplayer.h>

#define VOICE_HALF_BYTES       (VG_VOICE_RING_BYTES / 2)
#define VOICE_QUEUE_SLOTS      8
#define VOICE_REQUEST_SLOTS    16
#define VOICE_STOP_MS          3000u
#define VOICE_MIN_MS           100u
#define VOICE_PLAYER_IDLE      0

/* NxPlayer 的音量取值是 0..1000，wm8994 驱动按 63 * volume / 1000
 * 换算成 HPOUT1 PGA 寄存器 0..63，寄存器 0 为 -57 dB，63 为 +6 dB。
 * 900 对应寄存器 56，约 -1 dB，与 2026-10-22 听音确认可辨的
 * NxPlayer CLI volume 90 增益一致。原先传 15 只会落到寄存器 0。
 */
#define VOICE_PLAY_VOLUME      900
#ifndef CONFIG_NXPLAYER_MSG_PRIO
#  define CONFIG_NXPLAYER_MSG_PRIO 1
#endif

struct voice_request
{
  enum { REQ_BEGIN, REQ_END, REQ_END_SILENT, REQ_CANCEL, REQ_PLAY,
         REQ_QUIT } type;
  uint32_t generation;
  uint32_t limit_ms;
  char     path[80];      /* REQ_PLAY: file to play, copied by the caller */
};

struct voice_block
{
  uint32_t generation;
  uint32_t sequence;
  size_t bytes;
  uint8_t data[VOICE_HALF_BYTES];
};

static uint8_t g_ring[VG_VOICE_RING_BYTES] __attribute__((aligned(32)));
static struct voice_block *g_blocks;
static volatile unsigned g_produced;
static volatile unsigned g_consumed;
static volatile uint32_t g_sequence;
static volatile uint32_t g_dropped;
static volatile uint32_t g_copy_max_us;
static volatile bool g_capturing;
static uint32_t g_generation;
static uint32_t g_submit_generation;
static uint32_t g_limit_ms;
static uint32_t g_started_ms;
static uint32_t g_play_started_ms;
static int16_t g_wav_frames[4096 * 2];
static uint32_t g_expected_sequence;
static uint32_t g_filter_max_us;
static uint32_t g_drained;
static unsigned g_highwater;
static bool g_bad_stream;
static volatile bool g_request_active;
static bool g_quit;
static bool g_fault;
static bool g_inited;
static volatile bool g_running;
static struct vg_pdm_filter g_filter;
static int16_t *g_pcm;
static size_t g_pcm_bytes;
static struct stm32_pdm_capture_stats g_base;
static struct vg_voice_stats g_stats;
static struct vg_voice_stats g_published;
static struct voice_request g_requests[VOICE_REQUEST_SLOTS];
static unsigned g_req_head;
static unsigned g_req_tail;
static sem_t g_wake = SEM_INITIALIZER(0);
static mutex_t g_init_lock = NXMUTEX_INITIALIZER;
static struct nxplayer_s *g_player;

static uint64_t voice_us(void)
{
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint64_t)ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
}

static uint32_t voice_ms(void)
{
  return (uint32_t)(voice_us() / 1000);
}

static void voice_publish(enum vg_voice_state state)
{
  irqstate_t flags;
  g_stats.state = state;
  flags = enter_critical_section();
  g_published = g_stats;
  leave_critical_section(flags);
}

static bool voice_take(struct voice_request *request)
{
  irqstate_t flags = enter_critical_section();
  bool have = g_req_tail != g_req_head;
  if (have)
    {
      *request = g_requests[g_req_tail++ % VOICE_REQUEST_SLOTS];
      g_request_active = true;
    }
  leave_critical_section(flags);
  return have;
}

static bool voice_pending(void)
{
  irqstate_t flags = enter_critical_section();
  bool pending = g_req_tail != g_req_head || g_request_active;
  leave_critical_section(flags);
  return pending;
}

static int voice_post(struct voice_request request)
{
  irqstate_t flags = enter_critical_section();
  if (g_req_head - g_req_tail >= VOICE_REQUEST_SLOTS)
    {
      leave_critical_section(flags);
      return -EAGAIN;
    }

  if (request.type == REQ_BEGIN)
    {
      request.generation = ++g_submit_generation;
      if (request.generation == 0) request.generation = ++g_submit_generation;
    }
  else if (request.generation == 0)
    {
      request.generation = g_submit_generation;
    }

  g_requests[g_req_head++ % VOICE_REQUEST_SLOTS] = request;
  leave_critical_section(flags);
  nxsem_post(&g_wake);
  return (int)request.generation;
}

static bool voice_interrupt_pending(void)
{
  irqstate_t flags = enter_critical_section();
  unsigned i;
  bool interrupt = false;
  for (i = g_req_tail; i != g_req_head; i++)
    {
      struct voice_request *r = &g_requests[i % VOICE_REQUEST_SLOTS];
      if (r->type == REQ_BEGIN || r->type == REQ_QUIT ||
          (r->type == REQ_CANCEL && r->generation == g_generation))
        {
          interrupt = true;
          break;
        }
    }
  leave_critical_section(flags);
  return interrupt;
}

/* ISR 只复制到固定队列，不处理滤波和文件。消费者完成一块后才释放槽位，
 * 所以正在滤波的源数据不会被 DMA 或生产者覆盖。队列满时计数并拒绝整块。
 */

static void voice_capture_cb(void *arg, int half,
                             const uint8_t *data, size_t bytes)
{
  struct voice_block *block;
  unsigned used;
  uint64_t start = voice_us();
  uint32_t elapsed;
  (void)arg;
  (void)half;

  if (!g_capturing)
    {
      return;
    }

  g_sequence++;
  used = g_produced - g_consumed;
  if (bytes > VOICE_HALF_BYTES || used >= VOICE_QUEUE_SLOTS)
    {
      g_dropped++;
      return;
    }

  block = &g_blocks[g_produced % VOICE_QUEUE_SLOTS];
  block->generation = g_generation;
  block->sequence = g_sequence;
  block->bytes = bytes;
  memcpy(block->data, data, bytes);
  __atomic_thread_fence(__ATOMIC_RELEASE);
  g_produced++;
  if (used + 1 > g_highwater)
    {
      g_highwater = used + 1;
    }

  elapsed = (uint32_t)(voice_us() - start);
  if (elapsed > g_copy_max_us)
    {
      g_copy_max_us = elapsed;
    }
  nxsem_post(&g_wake);
}

static void voice_drain(void)
{
  while (g_consumed != g_produced)
    {
      struct voice_block *block = &g_blocks[g_consumed % VOICE_QUEUE_SLOTS];
      __atomic_thread_fence(__ATOMIC_ACQUIRE);
      size_t room = (VG_VOICE_PCM_MAX_BYTES - g_pcm_bytes) / 2;
      size_t expected = (g_filter.sample_phase + block->bytes) / 8;
      uint64_t start = voice_us();
      size_t got;

      if (block->generation != g_generation ||
          block->sequence != ++g_expected_sequence || expected > room)
        {
          g_bad_stream = true;
          g_consumed++;
          continue;
        }

      got = vg_pdm_filter_run(&g_filter, block->data, block->bytes,
                              g_pcm + g_pcm_bytes / 2, room);
      if (got != expected)
        {
          g_bad_stream = true;
        }
      g_pcm_bytes += got * 2;
      g_drained++;
      g_consumed++;
      {
        uint32_t elapsed = (uint32_t)(voice_us() - start);
        if (elapsed > g_filter_max_us)
          {
            g_filter_max_us = elapsed;
          }
      }
    }
}

static void voice_remove_temp(void)
{
  g_stats.temp_files_left =
    unlink(VG_VOICE_WAV_PATH) < 0 && errno != ENOENT ? 1 : 0;
}

static void voice_clear_pcm(void)
{
  if (g_pcm != NULL)
    {
      memset(g_pcm, 0, g_pcm_bytes);
    }
  g_pcm_bytes = 0;
  if (!g_capturing && g_blocks != NULL)
    {
      memset(g_blocks, 0, VOICE_QUEUE_SLOTS * sizeof(*g_blocks));
      memset(g_ring, 0, sizeof(g_ring));
      memset(g_wav_frames, 0, sizeof(g_wav_frames));
    }
  voice_remove_temp();
}

static void voice_measure(void)
{
  uint64_t squares = 0;
  uint32_t peak = 0;
  uint32_t zcr = 0;
  size_t count = g_pcm_bytes / 2;
  size_t i;
  uint64_t mean;
  uint64_t root = 0;
  uint64_t bit = (uint64_t)1 << 62;

  g_stats.clip_samples = 0;
  for (i = 0; i < count; i++)
    {
      int32_t value = g_pcm[i];
      uint32_t absolute = value < 0 ? -value : value;
      if (absolute > peak) peak = absolute;
      if (i && ((g_pcm[i - 1] >= 0) != (value >= 0))) zcr++;
      if (absolute >= 32767) g_stats.clip_samples++;
      squares += (int64_t)value * value;
    }
  mean = count ? squares / count : 0;
  g_stats.zcr = count ? (uint64_t)zcr * 1000 / count : 0;
  g_stats.peak_abs = peak;
  while (bit > mean) bit >>= 2;
  while (bit)
    {
      if (mean >= root + bit)
        {
          mean -= root + bit;
          root = (root >> 1) + bit;
        }
      else root >>= 1;
      bit >>= 2;
    }
  g_stats.rms = root;
}

static int voice_write_all(int fd, const void *data, size_t bytes)
{
  const uint8_t *p = data;
  while (bytes)
    {
      ssize_t n = write(fd, p, bytes);
      if (n < 0 && errno == EINTR) continue;
      if (n <= 0) return n < 0 ? -errno : -EIO;
      bytes -= n;
      p += n;
    }
  return 0;
}

static void voice_le32(uint8_t *out, uint32_t value)
{
  unsigned i;
  for (i = 0; i < 4; i++) out[i] = value >> (i * 8);
}

/* 立体声转换以 16384 字节一批写入，避免每个样本一次文件系统调用。
 * 写文件期间也检查请求；新请求交给任务主循环，不覆盖旧请求。
 */

static int voice_write_wav(void)
{
  uint8_t header[44] = {0};
  int16_t *frames = g_wav_frames;
  size_t samples = g_pcm_bytes / 2;
  size_t off;
  int fd;
  int ret;
  memcpy(header, "RIFF", 4);
  voice_le32(header + 4, samples * 4 + 36);
  memcpy(header + 8, "WAVEfmt ", 8);
  header[16] = 16;
  header[20] = 1;
  header[22] = 2;
  voice_le32(header + 24, VG_VOICE_RATE_HZ);
  voice_le32(header + 28, VG_VOICE_RATE_HZ * 4);
  header[32] = 4;
  header[34] = 16;
  memcpy(header + 36, "data", 4);
  voice_le32(header + 40, samples * 4);
  mkdir("/data/velaguard/voice", 0700);
  fd = open(VG_VOICE_WAV_PATH, O_CREAT | O_TRUNC | O_WRONLY, 0600);
  if (fd < 0) return -errno;
  ret = voice_write_all(fd, header, sizeof(header));
  for (off = 0; off < samples && ret == 0; )
    {
      size_t n = samples - off;
      size_t i;
      if (voice_interrupt_pending())
        {
          ret = -ECANCELED;
          break;
        }
      if (n > 4096) n = 4096;
      for (i = 0; i < n; i++)
        {
          frames[2 * i] = g_pcm[off + i];
          frames[2 * i + 1] = g_pcm[off + i];
        }
      ret = voice_write_all(fd, frames, n * 4);
      off += n;
    }
  if (ret == 0 && fsync(fd) < 0) ret = -errno;
  if (close(fd) < 0 && ret == 0) ret = -errno;
  return ret;
}

/* nxplayer_stop 内部的 mq_send 和 pthread_join 没有截止时间。这里按标准
 * STOP 消息请求停止，以有界等待确认设备关闭及线程退出后才回收 join 信息。
 * 超时时保留播放器和数据，进入 fault，禁止继续开始录音。
 */

static bool voice_player_done(void)
{
  pthread_t id;
  bool idle;
  if (g_player == NULL) return true;
  pthread_mutex_lock(&g_player->mutex);
  idle = g_player->state == VOICE_PLAYER_IDLE && g_player->dev_fd < 0;
  id = g_player->play_id;
  pthread_mutex_unlock(&g_player->mutex);
  if (!idle) return false;
  if (id > 0)
    {
      if (sched_getscheduler(id) >= 0 || errno != ESRCH) return false;
      pthread_join(id, NULL);
      g_player->play_id = 0;
    }
  return true;
}

static int voice_stop_playback(void)
{
  struct audio_msg_s message = {0};
  struct timespec deadline;
  uint32_t start = voice_ms();
  if (voice_player_done()) return 0;
  message.msg_id = AUDIO_MSG_STOP;
  clock_gettime(CLOCK_REALTIME, &deadline);
  deadline.tv_sec += 1;
  if (mq_timedsend(g_player->mq, (const char *)&message, sizeof(message),
                   CONFIG_NXPLAYER_MSG_PRIO, &deadline) < 0)
    {
      return -errno;
    }
  while (!voice_player_done())
    {
      if (voice_ms() - start >= VOICE_STOP_MS) return -ETIMEDOUT;
      usleep(10000);
    }
  return 0;
}

static void voice_fail(enum vg_voice_result result, bool fault)
{
  g_stats.last_result = result;
  g_stats.failures++;
  g_fault = fault;
  if (!fault) voice_clear_pcm();
  voice_publish(fault ? VG_VOICE_FAULT : VG_VOICE_IDLE);
}

static int voice_start(const struct voice_request *request)
{
  int ret;
  if (g_fault) return -EBUSY;
  if (voice_stop_playback() < 0)
    {
      voice_fail(VG_VOICE_ERR_BUSY, true);
      return -EBUSY;
    }
  voice_clear_pcm();
  vg_pdm_filter_init(&g_filter);
  g_produced = g_consumed = 0;
  g_sequence = g_expected_sequence = 0;
  g_dropped = g_drained = g_highwater = 0;
  g_copy_max_us = g_filter_max_us = 0;
  g_bad_stream = false;
  g_generation = request->generation;
  g_limit_ms = request->limit_ms ? request->limit_ms : VG_VOICE_MAX_MS;
  g_stats.generation = g_generation;
  g_stats.round_ms = 0;
  g_stats.stage_ms = 0;
  stm32_pdm_capture_stats(&g_base);
  g_capturing = true;
  ret = stm32_pdm_capture_start(VG_VOICE_RATE_HZ, g_ring, sizeof(g_ring),
                                voice_capture_cb, NULL);
  if (ret < 0)
    {
      g_capturing = false;
      voice_fail(VG_VOICE_ERR_START, false);
      return ret;
    }
  g_started_ms = voice_ms();
  voice_publish(VG_VOICE_RECORDING);
  return 0;
}

static int voice_play_path(const char *path)
{
  int ret;

  if (path == NULL || path[0] == '\0')
    {
      return -EINVAL;
    }

  if (g_player == NULL)
    {
      g_player = nxplayer_create();
      if (g_player == NULL)
        {
          return -ENOMEM;
        }
      nxplayer_setdevice(g_player, "/dev/audio/pcm0p");
      nxplayer_setvolume(g_player, VOICE_PLAY_VOLUME);
    }

  ret = nxplayer_playfile(g_player, path, AUDIO_FMT_UNDEF, AUDIO_FMT_UNDEF);
  if (ret < 0)
    {
      printf("vgvoice: nxplayer_playfile failed rc=%d errno=%d\n", ret, errno);
      return ret;
    }

  g_play_started_ms = voice_ms();
  voice_publish(VG_VOICE_PLAYING);
  return 0;
}

static void voice_play(void)
{
  uint32_t stage_start = voice_ms();
  int ret = voice_write_wav();
  g_stats.stage_ms = voice_ms() - stage_start;
  if (ret < 0)
    {
      if (ret == -ECANCELED)
        {
          g_stats.last_result = VG_VOICE_CANCELLED;
          voice_clear_pcm();
          voice_publish(VG_VOICE_IDLE);
        }
      else voice_fail(VG_VOICE_ERR_FILE, false);
      return;
    }

  if (voice_play_path(VG_VOICE_WAV_PATH) < 0)
    {
      voice_fail(VG_VOICE_ERR_PLAY, false);
    }
}

static void voice_finish(bool cancelled, bool play)
{
  struct stm32_pdm_capture_stats cap;
  int ret = stm32_pdm_capture_stop(50);
  g_capturing = false;
  g_stats.rounds++;
  if (ret < 0)
    {
      voice_fail(VG_VOICE_ERR_DMA, true);
      return;
    }
  g_stats.last_ms = voice_ms() - g_started_ms;
  voice_drain();
  stm32_pdm_capture_stats(&cap);
  g_stats.cap_elapsed_ms = g_stats.last_ms;
  g_stats.cap_halves = cap.halves - g_base.halves;
  g_stats.cap_tailbytes = cap.tailbytes - g_base.tailbytes;
  g_stats.cap_wraps = cap.wraps - g_base.wraps;
  g_stats.cap_drained = g_drained;
  g_stats.cap_overwrites = g_dropped;
  g_stats.dma_dropped = cap.dropped - g_base.dropped + g_dropped;
  g_stats.dma_overruns = cap.overruns - g_base.overruns;
  g_stats.dma_clockerrors = cap.clockerrors - g_base.clockerrors;
  g_stats.dma_errors = cap.dmaerrors - g_base.dmaerrors;
  g_stats.cap_rate_bps = g_stats.last_ms ?
    ((uint64_t)g_stats.cap_halves * VOICE_HALF_BYTES +
     g_stats.cap_tailbytes) * 1000 / g_stats.last_ms : 0;
  g_stats.last_bytes = g_pcm_bytes;
  g_stats.last_samples = g_pcm_bytes / 2;
  g_stats.queue_highwater = g_highwater;
  g_stats.copy_max_us = g_copy_max_us;
  g_stats.filter_max_us = g_filter_max_us;
  voice_measure();
  if (cancelled)
    {
      g_stats.cancelled++;
      g_stats.last_result = VG_VOICE_CANCELLED;
      voice_clear_pcm();
      voice_publish(VG_VOICE_IDLE);
      return;
    }
  if (g_bad_stream || g_stats.dma_dropped || g_stats.dma_overruns ||
      g_stats.dma_clockerrors || g_stats.dma_errors ||
      g_drained != g_stats.cap_halves + (g_stats.cap_tailbytes != 0))
    {
      voice_fail(VG_VOICE_ERR_DMA, false);
      return;
    }
  if (g_pcm_bytes < VG_VOICE_RATE_HZ * 2 * VOICE_MIN_MS / 1000)
    {
      g_stats.too_short++;
      g_stats.last_result = VG_VOICE_TOO_SHORT;
      voice_clear_pcm();
      voice_publish(VG_VOICE_IDLE);
      return;
    }
  g_stats.last_result = VG_VOICE_OK;

  if (play)
    {
      voice_play();
    }
  else
    {
      /* The cloud path recognises the file instead of playing it back, so
       * the round ends here and the caller waits for IDLE. */

      voice_publish(VG_VOICE_IDLE);
    }
}

static int voice_task(int argc, char **argv)
{
  struct voice_request request;
  (void)argc;
  (void)argv;
  while (!g_quit)
    {
      while (voice_take(&request))
        {
          g_request_active = true;
          if (request.type == REQ_QUIT)
            {
              g_quit = true;
              break;
            }
          if (request.type == REQ_BEGIN)
            {
              if (g_stats.state == VG_VOICE_RECORDING)
                {
                  voice_finish(true, false);
                }
              voice_start(&request);
            }
          else if (request.type == REQ_PLAY)
            {
              if (g_stats.state == VG_VOICE_RECORDING)
                {
                  voice_finish(true, false);
                }

              if (g_stats.state == VG_VOICE_PLAYING &&
                  voice_stop_playback() < 0)
                {
                  voice_fail(VG_VOICE_ERR_BUSY, true);
                }
              else
                {
                  voice_clear_pcm();
                  if (voice_play_path(request.path) < 0)
                    {
                      voice_fail(VG_VOICE_ERR_PLAY, false);
                    }
                }
            }
          else if (request.generation == g_generation)
            {
              if (g_stats.state == VG_VOICE_RECORDING)
                {
                  voice_finish(request.type == REQ_CANCEL,
                               request.type == REQ_END);
                }
              else if (request.type == REQ_CANCEL &&
                       g_stats.state == VG_VOICE_PLAYING)
                {
                  if (voice_stop_playback() < 0)
                    voice_fail(VG_VOICE_ERR_BUSY, true);
                  else
                    {
                      voice_clear_pcm();
                      voice_publish(VG_VOICE_IDLE);
                    }
                }
            }
          g_request_active = false;
        }
      g_request_active = false;
      if (g_quit) break;
      if (g_stats.state == VG_VOICE_RECORDING)
        {
          voice_drain();
          if (g_dropped || g_bad_stream)
            {
              voice_finish(false, true);
            }
          else if (voice_ms() - g_started_ms >= g_limit_ms)
            {
              if (g_limit_ms == VG_VOICE_MAX_MS) g_stats.timeouts++;
              voice_finish(false, true);
            }
        }
      else if (g_stats.state == VG_VOICE_PLAYING)
        {
          if (voice_player_done())
            {
              g_stats.played++;
              g_stats.round_ms = voice_ms() - g_started_ms;
              voice_clear_pcm();
              voice_publish(VG_VOICE_IDLE);
            }
          else if (voice_ms() - g_play_started_ms > g_stats.last_ms + VOICE_STOP_MS)
            {
              bool stopped = voice_stop_playback() == 0;
              voice_fail(VG_VOICE_ERR_PLAY, !stopped);
            }
        }
      if (g_stats.state == VG_VOICE_IDLE || g_stats.state == VG_VOICE_FAULT)
        {
          nxsem_wait_uninterruptible(&g_wake);
        }
      else
        {
          nxsem_tickwait_uninterruptible(&g_wake, MSEC2TICK(10));
        }
    }

  if (g_stats.state == VG_VOICE_RECORDING) voice_finish(true, false);
  if (voice_stop_playback() < 0 || g_fault)
    {
      voice_publish(VG_VOICE_FAULT);
    }
  else
    {
      voice_clear_pcm();
      if (g_player) nxplayer_release(g_player);
      g_player = NULL;
      voice_publish(VG_VOICE_IDLE);
    }
  g_running = false;
  return 0;
}

int vg_voice_init(void)
{
  int pid;
  nxmutex_lock(&g_init_lock);
  if (g_inited)
    {
      nxmutex_unlock(&g_init_lock);
      return 0;
    }
  g_pcm = calloc(1, VG_VOICE_PCM_MAX_BYTES);
  g_blocks = calloc(VOICE_QUEUE_SLOTS, sizeof(*g_blocks));
  if (!g_pcm || !g_blocks)
    {
      free(g_pcm);
      free(g_blocks);
      g_pcm = NULL;
      g_blocks = NULL;
      nxmutex_unlock(&g_init_lock);
      return -ENOMEM;
    }
  memset(&g_stats, 0, sizeof(g_stats));
  g_quit = g_fault = false;
  g_req_head = g_req_tail = 0;
  g_running = true;
  voice_remove_temp();
  pid = task_create("vgvoice", CONFIG_VG_VOICE_PRIORITY,
                    CONFIG_VG_VOICE_STACKSIZE, voice_task, NULL);
  if (pid < 0)
    {
      g_running = false;
      free(g_pcm);
      free(g_blocks);
      g_pcm = NULL;
      g_blocks = NULL;
      nxmutex_unlock(&g_init_lock);
      return -errno;
    }
  g_stats.worker_pid = pid;
  voice_publish(VG_VOICE_IDLE);
  g_inited = true;
  nxmutex_unlock(&g_init_lock);
  return 0;
}

int vg_voice_shutdown(void)
{
  uint32_t start = voice_ms();
  int ret;
  nxmutex_lock(&g_init_lock);
  if (!g_inited)
    {
      nxmutex_unlock(&g_init_lock);
      return 0;
    }
  ret = voice_post((struct voice_request){ .type = REQ_QUIT });
  if (ret < 0)
    {
      nxmutex_unlock(&g_init_lock);
      return ret;
    }
  while (g_running && voice_ms() - start < VOICE_STOP_MS) usleep(10000);
  if (g_running || g_fault)
    {
      nxmutex_unlock(&g_init_lock);
      return -ETIMEDOUT;
    }
  free(g_pcm);
  free(g_blocks);
  g_pcm = NULL;
  g_blocks = NULL;
  g_inited = false;
  g_stats.worker_pid = 0;
  voice_publish(VG_VOICE_IDLE);
  nxmutex_unlock(&g_init_lock);
  return 0;
}

int vg_voice_record_for(uint32_t ms)
{
  int ret = vg_voice_init();
  if (ret < 0) return ret;
  if (ms < VOICE_MIN_MS || ms > VG_VOICE_MAX_MS) return -EINVAL;
  if (g_fault) return -EBUSY;
  ret = voice_post((struct voice_request){ .type = REQ_BEGIN,
                                          .limit_ms = ms });
  return ret < 0 ? ret : 0;
}

int vg_voice_record_begin_owned(uint32_t *generation)
{
  int ret = vg_voice_init();
  if (ret < 0) return ret;
  if (g_fault || generation == NULL) return -EBUSY;
  ret = voice_post((struct voice_request){ .type = REQ_BEGIN,
                                          .limit_ms = VG_VOICE_MAX_MS });
  if (ret < 0) return ret;
  *generation = (uint32_t)ret;
  return 0;
}

enum vg_voice_result vg_voice_finish_owned(uint32_t generation, bool cancelled)
{
  int ret;
  if (generation == 0) return VG_VOICE_ERR_BUSY;
  ret = voice_post((struct voice_request){ .generation = generation,
    .type = cancelled ? REQ_CANCEL : REQ_END });
  return ret < 0 ? VG_VOICE_ERR_BUSY : VG_VOICE_OK;
}

enum vg_voice_result vg_voice_finish_owned_silent(uint32_t generation)
{
  int ret;

  if (generation == 0)
    {
      return VG_VOICE_ERR_BUSY;
    }

  ret = voice_post((struct voice_request){ .generation = generation,
    .type = REQ_END_SILENT });

  return ret < 0 ? VG_VOICE_ERR_BUSY : VG_VOICE_OK;
}

int vg_voice_play_file(const char *path)
{
  struct voice_request request;
  int ret;

  if (path == NULL || path[0] == '\0')
    {
      return -EINVAL;
    }

  memset(&request, 0, sizeof(request));
  request.type = REQ_PLAY;
  snprintf(request.path, sizeof(request.path), "%s", path);

  ret = voice_post(request);
  return ret < 0 ? ret : 0;
}

int vg_voice_record_begin(void)
{
  return vg_voice_record_for(VG_VOICE_MAX_MS);
}

enum vg_voice_result vg_voice_finish(bool cancelled)
{
  int ret = voice_post((struct voice_request){
    .type = cancelled ? REQ_CANCEL : REQ_END });
  return ret < 0 ? VG_VOICE_ERR_BUSY : VG_VOICE_OK;
}

enum vg_voice_result vg_voice_record_end(bool cancelled)
{
  return vg_voice_finish(cancelled);
}

enum vg_voice_result vg_voice_cancel(void)
{
  return vg_voice_finish(true);
}

bool vg_voice_wait_idle(uint32_t limit_ms)
{
  uint32_t start = voice_ms();
  for (;;)
    {
      struct vg_voice_stats stats;
      vg_voice_get_stats(&stats);
      if (stats.state == VG_VOICE_FAULT) return false;
      if (stats.state == VG_VOICE_IDLE && !voice_pending()) return true;
      if (voice_ms() - start >= limit_ms) return false;
      usleep(10000);
    }
}

void vg_voice_get_stats(struct vg_voice_stats *out)
{
  irqstate_t flags;
  if (out == NULL) return;
  flags = enter_critical_section();
  *out = g_published;
  leave_critical_section(flags);
}

enum vg_voice_state vg_voice_state(void)
{
  struct vg_voice_stats stats;
  vg_voice_get_stats(&stats);
  return stats.state;
}

const char *vg_voice_state_name(enum vg_voice_state state)
{
  switch (state)
    {
      case VG_VOICE_IDLE: return "idle";
      case VG_VOICE_RECORDING: return "recording";
      case VG_VOICE_PLAYING: return "playing";
      case VG_VOICE_FAULT: return "fault";
      default: return "?";
    }
}

const char *vg_voice_result_name(enum vg_voice_result result)
{
  switch (result)
    {
      case VG_VOICE_OK: return "ok";
      case VG_VOICE_CANCELLED: return "cancelled";
      case VG_VOICE_TOO_SHORT: return "too_short";
      case VG_VOICE_ERR_START: return "err_start";
      case VG_VOICE_ERR_DMA: return "err_dma";
      case VG_VOICE_ERR_ALLOC: return "err_alloc";
      case VG_VOICE_ERR_FILE: return "err_file";
      case VG_VOICE_ERR_PLAY: return "err_play";
      case VG_VOICE_ERR_BUSY: return "err_busy";
      default: return "?";
    }
}
#endif
