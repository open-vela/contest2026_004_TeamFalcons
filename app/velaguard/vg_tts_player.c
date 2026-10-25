/****************************************************************************
 * app/velaguard/vg_tts_player.c
 *
 * P1: the consumer side of TTS streaming playback.  The sequence the
 * player thread drives mirrors nxplayer's playthread (apps/system/
 * nxplayer/nxplayer.c): RESERVE -> GETCAPS -> CONFIGURE -> GETBUFFERINFO
 * -> ALLOCBUFFER xN -> REGISTERMQ -> fill -> ENQUEUE -> START, then an
 * mqueue loop where AUDIO_MSG_DEQUEUE hands buffers back for refills
 * and the FINAL flag on the last data buffer earns AUDIO_MSG_COMPLETE.
 *
 * 初始预缓冲和欠载后重新缓冲均有明确门槛。环空时仍处理在途缓冲，
 * 只有音频全部耗尽才等待恢复。驱动停滞后明确失败，不重灌不确定已播进度。
 * 取消时只释放驱动已归还的缓冲，避免 DMA 访问已释放内存。
 ****************************************************************************/

#include <errno.h>
#include <fcntl.h>
#include <mqueue.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <syslog.h>
#include <time.h>
#include <unistd.h>

#include <nuttx/audio/audio.h>

#include "vg_tts_player.h"
#include "vg_voice_pcm.h"

#ifndef CONFIG_VG_VOICE_TTS_RING_BYTES
#define CONFIG_VG_VOICE_TTS_RING_BYTES 131072
#endif

#ifndef CONFIG_VG_VOICE_TTS_PREBUF_MS
#define CONFIG_VG_VOICE_TTS_PREBUF_MS 2500
#endif

#if (CONFIG_VG_VOICE_TTS_RING_BYTES & (CONFIG_VG_VOICE_TTS_RING_BYTES - 1)) != 0
#error "CONFIG_VG_VOICE_TTS_RING_BYTES must be a power of two"
#endif

#define VG_TTS_RING_BYTES  CONFIG_VG_VOICE_TTS_RING_BYTES
#define VG_TTS_RING_MASK   (VG_TTS_RING_BYTES - 1u)

#define VG_TTS_DEV_PATH    "/dev/audio/pcm0p"
#define VG_TTS_MQ_NAME     "/vgtts"
/* 16 KB: above the host's PTHREAD_STACK_MIN (the state-machine test
 * runs this module on Linux) and roomy for syslog/ioctl frames on the
 * board; NuttX allocates it from the heap like every other thread. */

#define VG_TTS_STACK       16384
#define VG_TTS_APB_MAX     8
#define VG_TTS_APB_BYTES_MAX 16384
#define VG_TTS_MONO_MAX    (VG_TTS_APB_BYTES_MAX / 4u)

/* 16 kHz mono 16-bit is 32 bytes per ms; prebuffer gating is on ring
 * content, before the stereo expansion. */

#define VG_TTS_BYTES_PER_MS 32u

#define VG_TTS_PUSH_SLICE_MS    20u
#define VG_TTS_MQ_SLICE_MS      20u
#define VG_TTS_LIVENESS_MS      1000u
#define VG_TTS_RECOVER_MS       1000u

struct vg_tts_state
{
  /* ring: rd/wr are free-running byte counters, masked on access */
  uint8_t *ring;
  uint32_t rd;
  uint32_t wr;
  bool eof;
  bool abort_req;
  bool active;

  pthread_mutex_t lock;
  sem_t activity;             /* any-state-change wakeup, sliced waits */

  /* device: owned by start()/the player thread, never both */
  int fd;
  mqd_t mq;
  struct ap_buffer_s *apb[VG_TTS_APB_MAX];
  bool apb_home[VG_TTS_APB_MAX];  /* buffer is ours, driver must not hold it */
  uint32_t apb_payload[VG_TTS_APB_MAX];
  unsigned napb;
  unsigned apb_bytes;

  const volatile int *cancel;
  void (*on_started)(void *arg);
  void *on_started_arg;
  uint32_t prebuf_ms;
  uint32_t prebuf_bytes;
  uint32_t deadline_ms;
  bool header_pending;   /* pcm0p wants a WAV header before the PCM */

  /* Diagnostic bisection of the startup, for the SDRAM heap-writer hunt:
   * 0 = full playback (product behaviour); 1 = ring + thread only, the
   * audio device is never touched; 2 = device session (open/RESERVE/
   * CONFIGURE/ALLOCBUFFER/mq) but no ENQUEUE/START, so no DMA runs.
   * Set from NSH (vgvoice bisect), read once per start(). */
  uint8_t bisect_level;

  pthread_t thread;
  bool thread_valid;
  bool thread_done;
  struct vg_tts_player_stats stats;
};

static struct vg_tts_state g_pl =
{
  .fd = -1,
  .mq = (mqd_t)-1,
  .lock = PTHREAD_MUTEX_INITIALIZER,
  .prebuf_ms = CONFIG_VG_VOICE_TTS_PREBUF_MS,
};

static uint32_t now_ms(void)
{
  struct timespec t;

  clock_gettime(CLOCK_MONOTONIC, &t);
  return (uint32_t)((uint64_t)t.tv_sec * 1000u +
                    (uint64_t)t.tv_nsec / 1000000u);
}

static struct timespec slice_from_now(uint32_t ms)
{
  struct timespec t;

  /* POSIX sem_timedwait/mq_timedreceive 使用实时时钟；预算仍使用单调时钟。 */
  clock_gettime(CLOCK_REALTIME, &t);
  t.tv_sec += ms / 1000u;
  t.tv_nsec += (long)(ms % 1000u) * 1000000L;
  if (t.tv_nsec >= 1000000000L)
    {
      t.tv_sec++;
      t.tv_nsec -= 1000000000L;
    }

  return t;
}

/* 调用者持锁。合成截止时间只约束接收阶段，EOF 后按播放时长排空。 */
static int stop_reason_locked(void)
{
  if (g_pl.cancel != NULL && *g_pl.cancel) return -ECANCELED;
  if (g_pl.stats.failed) return g_pl.stats.err;
  if (g_pl.abort_req) return -EPIPE;
  if (!g_pl.eof && g_pl.deadline_ms != 0 &&
      (int32_t)(now_ms() - g_pl.deadline_ms) >= 0)
    {
      g_pl.stats.failed = true;
      g_pl.stats.err = -ETIMEDOUT;
      return -ETIMEDOUT;
    }
  return 0;
}

static bool cancelled(void)
{
  int reason;
  pthread_mutex_lock(&g_pl.lock);
  reason = stop_reason_locked();
  pthread_mutex_unlock(&g_pl.lock);
  return reason != 0;
}

/* ── ring (all under g_pl.lock) ──────────────────────────────────── */

static size_t ring_take(uint8_t *dst, size_t max_bytes)
{
  uint32_t used = g_pl.wr - g_pl.rd;
  size_t take = max_bytes;

  if (take > (size_t)used)
    {
      take = (size_t)used;
    }

  if (take == 0)
    {
      return 0;
    }

  {
    uint32_t off = g_pl.rd & VG_TTS_RING_MASK;
    size_t contig = VG_TTS_RING_BYTES - off;

    if (take > contig)
      {
        take = contig;
      }

    memcpy(dst, g_pl.ring + off, take);
    g_pl.rd += (uint32_t)take;
  }

  return take;
}

/* Track which buffers the driver currently holds.  apb_mark_home(apb,
 * true) on every DEQUEUE (the message carries the buffer pointer), false
 * on every ENQUEUE; teardown frees only the ones back home. */

static void apb_mark_home(const struct ap_buffer_s *apb, bool home)
{
  unsigned i;

  for (i = 0; i < g_pl.napb; i++)
    {
      if (g_pl.apb[i] == apb)
        {
          g_pl.apb_home[i] = home;
          return;
        }
    }
}

/* Fill one driver buffer from the ring and enqueue it.  The very first
 * buffer of a session carries a 44-byte WAV header: /dev/audio/pcm0p is
 * the pcm_decode wrapper, which parses that header to configure the
 * codec (nuttx/audio/pcm_decode.c pcm_enqueuebuffer) and then plays the
 * raw stereo PCM that follows — exactly what nxplayer feeds it when it
 * plays a file.  The data-chunk length is never reconciled with the
 * stream, so a large placeholder stands in for the unknown total.
 * Returns 1 when a buffer was enqueued, 0 when there was nothing to
 * send, -1 on error. */

static int fill_and_enqueue(struct ap_buffer_s *apb)
{
  uint8_t mono[VG_TTS_MONO_MAX];
  size_t mono_bytes;
  size_t samples;
  bool final;
  bool with_header = false;
  struct audio_buf_desc_s desc;
  int16_t *s;
  const int16_t *m;
  size_t i;

  pthread_mutex_lock(&g_pl.lock);

  if (g_pl.header_pending)
    {
      mono_bytes = ring_take(mono, g_pl.apb_bytes / 4u);
    }
  else
    {
      /* Leave room for the header; whole samples only. */

      size_t cap = (g_pl.apb_bytes - VG_PCM_WAV_HDR) / 4u;
      cap &= ~(size_t)1u;
      mono_bytes = ring_take(mono, cap);
    }

  if (mono_bytes == 0)
    {
      pthread_mutex_unlock(&g_pl.lock);
      return 0;
    }

  with_header = !g_pl.header_pending;
  g_pl.header_pending = true;
  final = g_pl.eof && (g_pl.wr == g_pl.rd);
  pthread_mutex_unlock(&g_pl.lock);
  sem_post(&g_pl.activity);

  samples = mono_bytes / 2u;
  s = (int16_t *)(void *)apb->samp;
  m = (const int16_t *)(const void *)mono;

  if (with_header)
    {
      vg_pcm_wav_header_stereo(apb->samp, 16000, 0x3ffffff0u);
      s = (int16_t *)(void *)(apb->samp + VG_PCM_WAV_HDR);
    }

  for (i = 0; i < samples; i++)
    {
      s[2 * i] = m[i];
      s[2 * i + 1] = m[i];
    }

  apb->nbytes = (apb_samp_t)((size_t)samples * 4u +
                              (with_header ? VG_PCM_WAV_HDR : 0u));
  apb->curbyte = 0;
  apb->flags = final ? AUDIO_APB_FINAL : 0;

  memset(&desc, 0, sizeof(desc));
  desc.numbytes = apb->nbytes;
  desc.u.buffer = apb;

  if (ioctl(g_pl.fd, AUDIOIOC_ENQUEUEBUFFER, (unsigned long)&desc) < 0)
    {
      pthread_mutex_lock(&g_pl.lock);
      g_pl.stats.failed = true;
      g_pl.stats.err = errno != 0 ? -errno : -EIO;
      pthread_mutex_unlock(&g_pl.lock);
      return -1;
    }

  /* The driver owns the buffer from here until its DEQUEUE comes back;
   * teardown must not free it in between (see dev_teardown). */

  apb_mark_home(apb, false);
  for (i = 0; i < g_pl.napb; i++)
    if (g_pl.apb[i] == apb) g_pl.apb_payload[i] = (uint32_t)(samples * 4u);

  pthread_mutex_lock(&g_pl.lock);
  g_pl.stats.played_bytes += (uint32_t)(samples * 4u);
  pthread_mutex_unlock(&g_pl.lock);
  return 1;
}

/* ── device session ──────────────────────────────────────────────── */

static void dev_teardown(void)
{
  struct audio_buf_desc_s desc;
  unsigned i;
  unsigned leaked = 0;

  if (g_pl.fd >= 0)
    {
      memset(&desc, 0, sizeof(desc));

      /* FREEBUFFER only buffers the driver has handed back: freeing one
       * it still holds (or whose SAI DMA is still running) turns the
       * subsequent transfer into a write-after-free into the SDRAM heap.
       * A wedged driver means a few 8 KB buffers stay allocated — the
       * corruption risk is strictly worse than the leak, so those are
       * counted and abandoned, not freed. */

      for (i = 0; i < g_pl.napb; i++)
        {
          if (g_pl.apb[i] == NULL)
            {
              continue;
            }

          if (g_pl.apb_home[i])
            {
              desc.u.buffer = g_pl.apb[i];
              ioctl(g_pl.fd, AUDIOIOC_FREEBUFFER, (unsigned long)&desc);
            }
          else
            {
              leaked++;
            }

          g_pl.apb[i] = NULL;
        }

      g_pl.stats.leaked_buffers = (uint8_t)leaked;

      if (g_pl.mq != (mqd_t)-1)
        {
          ioctl(g_pl.fd, AUDIOIOC_UNREGISTERMQ, (unsigned long)g_pl.mq);
        }

      ioctl(g_pl.fd, AUDIOIOC_RELEASE, 0);
      close(g_pl.fd);
      g_pl.fd = -1;
    }

  if (g_pl.mq != (mqd_t)-1)
    {
      mq_close(g_pl.mq);
      mq_unlink(VG_TTS_MQ_NAME);
      g_pl.mq = (mqd_t)-1;
    }

  g_pl.napb = 0;
}

static int dev_setup(void)
{
  struct audio_caps_s caps;
  struct audio_caps_desc_s cap_desc;
  struct ap_buffer_info_s info;
  struct audio_buf_desc_s desc;
  struct mq_attr mattr;
  unsigned i;
  int ret = -EIO;

  g_pl.fd = open(VG_TTS_DEV_PATH, O_RDWR | O_CLOEXEC);
  if (g_pl.fd < 0)
    {
      return -errno;
    }

  if (ioctl(g_pl.fd, AUDIOIOC_RESERVE, 0) < 0)
    {
      goto fail;
    }

  /* Output capability is checked softly: a caps length mismatch just
   * means the driver did not answer, and CONFIGURE is the real test. */

  memset(&caps, 0, sizeof(caps));
  caps.ac_len = sizeof(caps);
  caps.ac_type = AUDIO_TYPE_OUTPUT;
  caps.ac_subtype = AUDIO_TYPE_QUERY;

  if (ioctl(g_pl.fd, AUDIOIOC_GETCAPS, (unsigned long)&caps) == caps.ac_len &&
      (caps.ac_type & AUDIO_TYPE_OUTPUT) == 0)
    {
      goto fail;
    }

  /* The format the WAV path was verified with: 16 kHz stereo 16-bit. */

  memset(&cap_desc, 0, sizeof(cap_desc));
  cap_desc.caps.ac_len = sizeof(struct audio_caps_s);
  cap_desc.caps.ac_type = AUDIO_TYPE_OUTPUT;
  cap_desc.caps.ac_subtype = AUDIO_FMT_PCM;
  cap_desc.caps.ac_channels = 2;
  cap_desc.caps.ac_controls.hw[0] = 16000;
  cap_desc.caps.ac_controls.b[3] = 16000 >> 16;
  cap_desc.caps.ac_controls.b[2] = 16;

  if (ioctl(g_pl.fd, AUDIOIOC_CONFIGURE, (unsigned long)&cap_desc) < 0)
    {
      goto fail;
    }

  memset(&info, 0, sizeof(info));
  if (ioctl(g_pl.fd, AUDIOIOC_GETBUFFERINFO, (unsigned long)&info) != 0 ||
      info.nbuffers == 0 || info.buffer_size < 512)
    {
      info.nbuffers = 4;
      info.buffer_size = 8192;
    }

  if (info.nbuffers > VG_TTS_APB_MAX)
    {
      info.nbuffers = VG_TTS_APB_MAX;
    }

  if (info.buffer_size > VG_TTS_APB_BYTES_MAX)
    {
      info.buffer_size = VG_TTS_APB_BYTES_MAX;
    }

  g_pl.napb = info.nbuffers;
  g_pl.apb_bytes = info.buffer_size;

  for (i = 0; i < g_pl.napb; i++)
    {
      memset(&desc, 0, sizeof(desc));
      desc.numbytes = info.buffer_size;
      desc.u.pbuffer = &g_pl.apb[i];

      if (ioctl(g_pl.fd, AUDIOIOC_ALLOCBUFFER,
                (unsigned long)&desc) != sizeof(desc))
        {
          goto fail;
        }

      g_pl.apb_home[i] = true;
    }

  mq_unlink(VG_TTS_MQ_NAME);

  memset(&mattr, 0, sizeof(mattr));

  /* Worst traffic is one DEQUEUE per buffer plus COMPLETE/STOP, i.e.
   * napb + 2; +2 slack.  Capped at 10 because unprivileged Linux (the
   * host test) limits mq_maxmsg to fs.mqueue.msg_max = 10. */

  mattr.mq_maxmsg = g_pl.napb + 4;
  if (mattr.mq_maxmsg > 10)
    {
      mattr.mq_maxmsg = 10;
    }

  mattr.mq_msgsize = sizeof(struct audio_msg_s);

  g_pl.mq = mq_open(VG_TTS_MQ_NAME, O_RDWR | O_CREAT, 0644, &mattr);
  if (g_pl.mq == (mqd_t)-1)
    {
      goto fail;
    }

  if (ioctl(g_pl.fd, AUDIOIOC_REGISTERMQ, (unsigned long)g_pl.mq) < 0)
    {
      goto fail;
    }

  return 0;

fail:
  if (errno != 0)
    {
      ret = -errno;
    }

  dev_teardown();
  return ret;
}

/* ── player thread ───────────────────────────────────────────────── */

/* 只有播放线程操作缓冲所有权；生产者只接触加锁的环与统计。 */
static void player_fail(int error, const char *reason)
{
  pthread_mutex_lock(&g_pl.lock);
  if (!g_pl.stats.failed)
    {
      g_pl.stats.failed = true;
      g_pl.stats.err = error;
    }
  pthread_mutex_unlock(&g_pl.lock);
  syslog(LOG_ERR, "[vgvoice] tts_play event=failed reason=%s err=%d\n",
         reason, error);
  sem_post(&g_pl.activity);
}

static bool return_buffer(const struct ap_buffer_s *apb, bool played,
                           unsigned *outstanding)
{
  unsigned i;
  for (i = 0; i < g_pl.napb; i++)
    {
      if (g_pl.apb[i] == apb && !g_pl.apb_home[i] && *outstanding > 0)
        {
          g_pl.apb_home[i] = true;
          (*outstanding)--;
          if (played)
            {
              pthread_mutex_lock(&g_pl.lock);
              g_pl.stats.returned_bytes += g_pl.apb_payload[i];
              pthread_mutex_unlock(&g_pl.lock);
            }
          return true;
        }
    }
  return false;
}

static uint32_t inflight_ms(void)
{
  unsigned i;
  uint32_t bytes = 0;
  for (i = 0; i < g_pl.napb; i++)
    if (!g_pl.apb_home[i]) bytes += g_pl.apb_payload[i];
  return (bytes + 63u) / 64u;
}

static bool fill_home_buffers(unsigned *outstanding)
{
  unsigned i;
  for (i = 0; i < g_pl.napb; i++)
    {
      int r;
      if (!g_pl.apb_home[i]) continue;
      r = fill_and_enqueue(g_pl.apb[i]);
      if (r < 0) return false;
      if (r > 0) (*outstanding)++;
    }
  return true;
}

static uint32_t capacity_ms(uint32_t requested)
{
  uint32_t maximum = VG_TTS_RING_BYTES / VG_TTS_BYTES_PER_MS;
  if (maximum > 4000u) maximum = 4000u;
  return requested < maximum ? requested : maximum;
}

/* 初始缓冲和真正欠载后的重新缓冲共用门槛；完整结束时直接排空尾段。 */
static bool wait_buffer(uint32_t threshold_ms, bool initial)
{
  uint32_t t0 = now_ms();
  for (;;)
    {
      uint32_t used;
      bool eof_l;
      struct timespec ts;
      if (cancelled()) return false;
      pthread_mutex_lock(&g_pl.lock);
      used = g_pl.wr - g_pl.rd;
      eof_l = g_pl.eof;
      pthread_mutex_unlock(&g_pl.lock);
      if (used >= threshold_ms * VG_TTS_BYTES_PER_MS || eof_l)
        {
          uint32_t ready = now_ms();
          pthread_mutex_lock(&g_pl.lock);
          if (initial) g_pl.stats.ready_ms = ready;
          else g_pl.stats.stall_ms += ready - t0;
          pthread_mutex_unlock(&g_pl.lock);
          syslog(LOG_INFO, "[vgvoice] tts_play event=buffer_ready mono_ms=%u "
                 "buffered_ms=%u target_ms=%u eof=%u initial=%u\n",
                 (unsigned)ready, (unsigned)(used / VG_TTS_BYTES_PER_MS),
                 (unsigned)threshold_ms, eof_l, initial);
          return true;
        }
      ts = slice_from_now(VG_TTS_PUSH_SLICE_MS);
      sem_timedwait(&g_pl.activity, &ts);
    }
}

static void *vg_tts_thread(void *arg)
{
  struct audio_msg_s msg;
  struct vg_tts_player_stats final_stats;
  unsigned prio;
  unsigned outstanding = 0;
  bool complete = false;
  bool running;
  uint32_t last_progress = 0;
  uint32_t recover_ms = 3000u;
  uint32_t t0;
  (void)arg;

  running = wait_buffer(g_pl.prebuf_ms, true);
  if (running && g_pl.bisect_level >= 1)
    {
      complete = true;
      running = false;
    }

  if (running)
    {
      if (!fill_home_buffers(&outstanding)) running = false;
      else if (outstanding == 0)
        {
          complete = true;
          running = false;
        }
      else if (ioctl(g_pl.fd, AUDIOIOC_START, 0) < 0)
        {
          player_fail(errno ? -errno : -EIO, "start");
          running = false;
        }
      else
        {
          last_progress = now_ms();
          pthread_mutex_lock(&g_pl.lock);
          g_pl.stats.started = true;
          g_pl.stats.started_ms = last_progress;
          g_pl.stats.start_delay_ms = last_progress - g_pl.stats.ready_ms;
          pthread_mutex_unlock(&g_pl.lock);
          syslog(LOG_INFO, "[vgvoice] tts_play event=started mono_ms=%u "
                 "ready_delay_ms=%u\n", (unsigned)last_progress,
                 (unsigned)g_pl.stats.start_delay_ms);
          if (g_pl.on_started != NULL) g_pl.on_started(g_pl.on_started_arg);
        }
    }

  while (running)
    {
      uint32_t used;
      bool eof_l;
      struct timespec ts;
      if (cancelled()) break;
      pthread_mutex_lock(&g_pl.lock);
      used = g_pl.wr - g_pl.rd;
      eof_l = g_pl.eof;
      pthread_mutex_unlock(&g_pl.lock);

      if (outstanding == 0 && used == 0 && eof_l)
        {
          pthread_mutex_lock(&g_pl.lock);
          complete = !g_pl.stats.failed &&
                     g_pl.stats.returned_bytes == g_pl.stats.played_bytes &&
                     g_pl.stats.played_bytes == g_pl.stats.pushed_bytes * 2u;
          pthread_mutex_unlock(&g_pl.lock);
          break;
        }

      if (outstanding == 0 && used == 0 && !eof_l)
        {
          uint32_t target = capacity_ms(recover_ms);
          pthread_mutex_lock(&g_pl.lock);
          g_pl.stats.underruns++;
          g_pl.stats.rebuffer_ms = target;
          pthread_mutex_unlock(&g_pl.lock);
          syslog(LOG_INFO, "[vgvoice] tts_play event=underrun mono_ms=%u "
                 "target_ms=%u\n", (unsigned)now_ms(), (unsigned)target);
          if (!wait_buffer(target, false)) break;
          if (recover_ms < 4000u) recover_ms += 500u;
          if (cancelled()) break;
          if (!fill_home_buffers(&outstanding)) break;
          if (outstanding > 0)
            {
              if (ioctl(g_pl.fd, AUDIOIOC_START, 0) < 0)
                {
                  player_fail(errno ? -errno : -EIO, "resume");
                  break;
                }
              last_progress = now_ms();
              pthread_mutex_lock(&g_pl.lock);
              g_pl.stats.restarts++;
              pthread_mutex_unlock(&g_pl.lock);
              syslog(LOG_INFO, "[vgvoice] tts_play event=resumed mono_ms=%u\n",
                     (unsigned)last_progress);
            }
          continue;
        }

      /* 环空但驱动仍有音频时，不等待新数据，继续接收归还消息。 */
      if (used > 0)
        {
          bool was_empty = outstanding == 0;
          if (!fill_home_buffers(&outstanding)) break;
          if (was_empty && outstanding > 0) last_progress = now_ms();
        }

      if (outstanding > 0 &&
          now_ms() - last_progress >= inflight_ms() + VG_TTS_LIVENESS_MS)
        {
          player_fail(-ETIMEDOUT, "driver_stalled");
          break;
        }

      ts = slice_from_now(VG_TTS_MQ_SLICE_MS);
      if (mq_timedreceive(g_pl.mq, (char *)&msg, sizeof(msg), &prio, &ts) < 0)
        {
          if (errno != ETIMEDOUT && errno != EINTR)
            {
              player_fail(-errno, "receive");
              break;
            }
          continue;
        }
      if (msg.msg_id == AUDIO_MSG_DEQUEUE)
        {
          if (!return_buffer(msg.u.ptr, true, &outstanding))
            {
              player_fail(-EIO, "buffer_ownership");
              break;
            }
          last_progress = now_ms();
        }
      else if (msg.msg_id == AUDIO_MSG_COMPLETE)
        {
          pthread_mutex_lock(&g_pl.lock);
          eof_l = g_pl.eof;
          pthread_mutex_unlock(&g_pl.lock);
          if (!eof_l)
            {
              player_fail(-EIO, "premature_complete");
              break;
            }
          /* COMPLETE 不能代替各缓冲的归还；下一轮循环确认全部消费。 */
        }
      else if (msg.msg_id == AUDIO_MSG_STOP)
        {
          if (!cancelled()) player_fail(-EIO, "unexpected_stop");
          break;
        }
    }

  if (g_pl.fd >= 0)
    {
      ioctl(g_pl.fd, AUDIOIOC_STOP, 0);
      t0 = now_ms();
      while (outstanding > 0 && now_ms() - t0 < VG_TTS_RECOVER_MS)
        {
          struct timespec ts = slice_from_now(20);
          if (mq_timedreceive(g_pl.mq, (char *)&msg, sizeof(msg), &prio, &ts) < 0)
            continue;
          if (msg.msg_id == AUDIO_MSG_DEQUEUE)
            (void)return_buffer(msg.u.ptr, false, &outstanding);
        }
    }
  dev_teardown();
  pthread_mutex_lock(&g_pl.lock);
  g_pl.stats.complete = complete;
  g_pl.stats.played_ms = g_pl.stats.played_bytes / 64u;
  final_stats = g_pl.stats;
  g_pl.thread_done = true;
  pthread_mutex_unlock(&g_pl.lock);
  syslog(LOG_INFO, "[vgvoice] tts_play event=done complete=%u failed=%u err=%d "
         "enqueued=%u returned=%u underruns=%u stall_ms=%u leaked=%u\n",
         final_stats.complete, final_stats.failed, final_stats.err,
         (unsigned)final_stats.played_bytes, (unsigned)final_stats.returned_bytes,
         (unsigned)final_stats.underruns, (unsigned)final_stats.stall_ms,
         final_stats.leaked_buffers);
  sem_post(&g_pl.activity);
  return NULL;
}

/* ── public API ──────────────────────────────────────────────────── */

static uint32_t clamp_prebuf(uint32_t ms)
{
  if (ms < 50u)
    {
      ms = 50u;
    }

  ms = capacity_ms(ms);

  return ms;
}

int vg_tts_player_start(uint32_t prebuf_ms, uint32_t deadline_ms,
                        const volatile int *cancel,
                        void (*on_started)(void *arg), void *arg)
{
  pthread_attr_t attr;
  static bool inited;
  int r;

  pthread_mutex_lock(&g_pl.lock);
  if (g_pl.active)
    {
      pthread_mutex_unlock(&g_pl.lock);
      return -EBUSY;
    }

  g_pl.rd = 0;
  g_pl.wr = 0;
  g_pl.eof = false;
  g_pl.abort_req = false;
  g_pl.header_pending = false;
  g_pl.thread_done = false;
  g_pl.thread_valid = false;
  memset(&g_pl.stats, 0, sizeof(g_pl.stats));
  g_pl.cancel = cancel;
  g_pl.deadline_ms = deadline_ms;
  g_pl.on_started = on_started;
  g_pl.on_started_arg = arg;
  g_pl.prebuf_ms = clamp_prebuf(prebuf_ms);
  g_pl.prebuf_bytes = g_pl.prebuf_ms * VG_TTS_BYTES_PER_MS;
  g_pl.active = true;
  pthread_mutex_unlock(&g_pl.lock);

  if (!inited)
    {
      if (sem_init(&g_pl.activity, 0, 0) != 0)
        {
          g_pl.active = false;
          return -errno;
        }

      inited = true;
    }

  if (g_pl.ring == NULL)
    {
      g_pl.ring = calloc(1, VG_TTS_RING_BYTES);
      if (g_pl.ring == NULL)
        {
          g_pl.active = false;
          return -ENOMEM;
        }
    }

  /* Bisection level 1 skips the whole device session: ring + thread
   * only, the exact allocation footprint without any audio ioctl.
   * Level 2 keeps the session (open/RESERVE/CONFIGURE/ALLOCBUFFER/mq)
   * and only the playback is suppressed, inside the thread. */

  if (g_pl.bisect_level != 1)
    {
      r = dev_setup();
      if (r < 0)
        {
          g_pl.active = false;
          return r;
        }
    }

  pthread_attr_init(&attr);
  pthread_attr_setstacksize(&attr, VG_TTS_STACK);
  r = pthread_create(&g_pl.thread, &attr, vg_tts_thread, NULL);
  pthread_attr_destroy(&attr);

  if (r != 0)
    {
      dev_teardown();
      g_pl.active = false;
      return -r;
    }

  g_pl.thread_valid = true;
  return 0;
}

int vg_tts_player_push(const uint8_t *mono16k, size_t bytes)
{
  size_t done = 0;

  if (bytes == 0)
    {
      return 0;
    }

  if (mono16k == NULL || (bytes & 1u) != 0)
    {
      return -EINVAL;
    }

  if (!g_pl.active)
    {
      return -EPIPE;
    }

  while (done < bytes)
    {
      uint32_t used;
      uint32_t freeb;
      uint32_t off;
      size_t contig;
      size_t take;
      struct timespec ts;

      pthread_mutex_lock(&g_pl.lock);

      int reason = stop_reason_locked();
      if (reason != 0 || g_pl.thread_done || g_pl.eof)
        {
          pthread_mutex_unlock(&g_pl.lock);
          return reason != 0 ? reason : -EPIPE;
        }

      used = g_pl.wr - g_pl.rd;
      freeb = VG_TTS_RING_BYTES - used;

      if (freeb == 0)
        {
          pthread_mutex_unlock(&g_pl.lock);

          if (g_pl.cancel != NULL && *g_pl.cancel)
            {
              return -ECANCELED;
            }

          ts = slice_from_now(VG_TTS_PUSH_SLICE_MS);
          sem_timedwait(&g_pl.activity, &ts);
          continue;
        }

      take = bytes - done;
      if (take > (size_t)freeb)
        {
          take = (size_t)freeb;
        }

      off = g_pl.wr & VG_TTS_RING_MASK;
      contig = VG_TTS_RING_BYTES - off;
      if (take > contig)
        {
          take = contig;
        }

      memcpy(g_pl.ring + off, mono16k + done, take);
      g_pl.wr += (uint32_t)take;
      g_pl.stats.pushed_bytes += (uint32_t)take;
      pthread_mutex_unlock(&g_pl.lock);
      sem_post(&g_pl.activity);
      done += take;
    }

  return 0;
}

int vg_tts_player_eof(void)
{
  pthread_mutex_lock(&g_pl.lock);
  if (!g_pl.active)
    {
      pthread_mutex_unlock(&g_pl.lock);
      return -EPIPE;
    }

  g_pl.eof = true;
  pthread_mutex_unlock(&g_pl.lock);
  sem_post(&g_pl.activity);
  return 0;
}

void vg_tts_player_abort(void)
{
  pthread_mutex_lock(&g_pl.lock);
  if (!g_pl.active)
    {
      pthread_mutex_unlock(&g_pl.lock);
      return;
    }

  g_pl.abort_req = true;
  g_pl.eof = false;
  pthread_mutex_unlock(&g_pl.lock);
  sem_post(&g_pl.activity);
}

int vg_tts_player_wait(uint32_t limit_ms, struct vg_tts_player_stats *out)
{
  uint32_t t0 = now_ms();
  bool timed_out = false;

  pthread_mutex_lock(&g_pl.lock);
  if (!g_pl.active)
    {
      pthread_mutex_unlock(&g_pl.lock);
      return -EPIPE;
    }
  pthread_mutex_unlock(&g_pl.lock);

  for (;;)
    {
      bool done;

      pthread_mutex_lock(&g_pl.lock);
      done = g_pl.thread_done;
      pthread_mutex_unlock(&g_pl.lock);

      if (done)
        {
          break;
        }

      if (now_ms() - t0 >= limit_ms)
        {
          vg_tts_player_abort();
          t0 = now_ms();

          for (;;)
            {
              pthread_mutex_lock(&g_pl.lock);
              done = g_pl.thread_done;
              pthread_mutex_unlock(&g_pl.lock);

              if (done || now_ms() - t0 >= 2000u)
                {
                  break;
                }

              {
                struct timespec ts = slice_from_now(VG_TTS_PUSH_SLICE_MS);
                sem_timedwait(&g_pl.activity, &ts);
              }
            }

          pthread_mutex_lock(&g_pl.lock);
          timed_out = !g_pl.thread_done;
          pthread_mutex_unlock(&g_pl.lock);

          if (timed_out)
            {
              /* The player thread is wedged inside the driver.  Detach
               * it and leave the player state owned by it: every later
               * start() then returns -EBUSY and rounds fall back to
               * the WAV path until reboot.  Joining here could hang
               * the round thread forever, which is worse. */

              if (g_pl.thread_valid)
                {
                  pthread_detach(g_pl.thread);
                  g_pl.thread_valid = false;
                }
            }

          break;
        }

      {
        struct timespec ts = slice_from_now(VG_TTS_PUSH_SLICE_MS);
        sem_timedwait(&g_pl.activity, &ts);
      }
    }

  if (g_pl.thread_valid)
    {
      pthread_join(g_pl.thread, NULL);
      g_pl.thread_valid = false;
    }

  pthread_mutex_lock(&g_pl.lock);
  if (out != NULL)
    {
      *out = g_pl.stats;
    }

  if (!timed_out)
    {
      g_pl.active = false;
    }

  pthread_mutex_unlock(&g_pl.lock);
  return timed_out ? -ETIMEDOUT : 0;
}

uint32_t vg_tts_player_prebuf_ms(void)
{
  uint32_t v;

  pthread_mutex_lock(&g_pl.lock);
  v = g_pl.prebuf_ms;
  pthread_mutex_unlock(&g_pl.lock);
  return v;
}

int vg_tts_player_set_prebuf_ms(uint32_t ms)
{
  uint32_t v = clamp_prebuf(ms);

  pthread_mutex_lock(&g_pl.lock);
  g_pl.prebuf_ms = v;
  pthread_mutex_unlock(&g_pl.lock);
  return (int)v;
}

uint8_t vg_tts_player_bisect_level(void)
{
  uint8_t v;

  pthread_mutex_lock(&g_pl.lock);
  v = g_pl.bisect_level;
  pthread_mutex_unlock(&g_pl.lock);
  return v;
}

int vg_tts_player_set_bisect_level(int level)
{
  if (level < 0 || level > 2)
    {
      return -1;
    }

  pthread_mutex_lock(&g_pl.lock);
  g_pl.bisect_level = (uint8_t)level;
  pthread_mutex_unlock(&g_pl.lock);
  return level;
}
