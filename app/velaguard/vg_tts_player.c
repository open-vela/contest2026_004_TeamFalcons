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
 * ENQUEUEBUFFER never blocks and a starved stream produces no message,
 * so underruns are detected here (every buffer returned, ring empty,
 * stream not at EOF): the buffer is held, the thread waits for data in
 * cancel-aware slices, then re-enqueues.  A re-START after that is a
 * documented no-op while the device is RUNNING (nuttx/audio/audio.c
 * audio_start) — the wm8994 worker wakes on the new ENQUEUE and re-arms
 * the SAI DMA itself.  If even that stalls, an escalation stops the
 * device, recovers the buffers and restarts.  AUDIOIOC_RESETSTATUS is
 * never used: its handler dereferences the internal-ring buffer array
 * this driver path does not populate.
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
#include <time.h>
#include <unistd.h>

#include <nuttx/audio/audio.h>

#include "vg_tts_player.h"
#include "vg_voice_pcm.h"

#ifndef CONFIG_VG_VOICE_TTS_RING_BYTES
#define CONFIG_VG_VOICE_TTS_RING_BYTES 131072
#endif

#ifndef CONFIG_VG_VOICE_TTS_PREBUF_MS
#define CONFIG_VG_VOICE_TTS_PREBUF_MS 200
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
#define VG_TTS_MQ_SLICE_MS      100u
#define VG_TTS_LIVENESS_MS      400u
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
  unsigned napb;
  unsigned apb_bytes;

  const volatile int *cancel;
  void (*on_started)(void *arg);
  void *on_started_arg;
  uint32_t prebuf_ms;
  uint32_t prebuf_bytes;
  bool header_pending;   /* pcm0p wants a WAV header before the PCM */

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

  clock_gettime(CLOCK_MONOTONIC, &t);
  t.tv_sec += ms / 1000u;
  t.tv_nsec += (long)(ms % 1000u) * 1000000L;
  if (t.tv_nsec >= 1000000000L)
    {
      t.tv_sec++;
      t.tv_nsec -= 1000000000L;
    }

  return t;
}

static bool cancelled(void)
{
  return (g_pl.cancel != NULL && *g_pl.cancel) || g_pl.abort_req;
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

  if (g_pl.fd >= 0)
    {
      memset(&desc, 0, sizeof(desc));

      for (i = 0; i < g_pl.napb; i++)
        {
          if (g_pl.apb[i] != NULL)
            {
              desc.u.buffer = g_pl.apb[i];
              ioctl(g_pl.fd, AUDIOIOC_FREEBUFFER, (unsigned long)&desc);
              g_pl.apb[i] = NULL;
            }
        }

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

/* Wait (cancel-aware) until the ring has data or the stream ended.
 * Adds the wait to stall_ms.  Returns false when cancelled/aborted. */

static bool wait_for_data(void)
{
  uint32_t t0 = now_ms();

  for (;;)
    {
      uint32_t used;
      bool eof_l;
      struct timespec ts;

      if (cancelled())
        {
          pthread_mutex_lock(&g_pl.lock);
          g_pl.stats.stall_ms += now_ms() - t0;
          pthread_mutex_unlock(&g_pl.lock);
          return false;
        }

      pthread_mutex_lock(&g_pl.lock);
      used = g_pl.wr - g_pl.rd;
      eof_l = g_pl.eof;
      pthread_mutex_unlock(&g_pl.lock);

      if (used > 0 || eof_l)
        {
          pthread_mutex_lock(&g_pl.lock);
          g_pl.stats.stall_ms += now_ms() - t0;
          pthread_mutex_unlock(&g_pl.lock);
          return true;
        }

      ts = slice_from_now(VG_TTS_PUSH_SLICE_MS);
      sem_timedwait(&g_pl.activity, &ts);
    }
}

/* Stop the device, recover the buffers still in flight through the
 * message queue, refill everything and restart.  Returns the new
 * outstanding count, or -1 when the driver did not give the buffers
 * back in time (caller gives up on the stream). */

static int escalate_restart(unsigned outstanding)
{
  struct audio_msg_s msg;
  unsigned prio;
  bool back[VG_TTS_APB_MAX];
  unsigned recovered = 0;
  unsigned i;
  uint32_t t0 = now_ms();

  ioctl(g_pl.fd, AUDIOIOC_STOP, 0);

  for (i = 0; i < g_pl.napb; i++)
    {
      back[i] = false;
    }

  while (recovered < outstanding)
    {
      struct timespec ts = slice_from_now(50);

      if (mq_timedreceive(g_pl.mq, (char *)&msg, sizeof(msg), &prio,
                          &ts) < 0)
        {
          if (now_ms() - t0 >= VG_TTS_RECOVER_MS)
            {
              return -1;
            }

          continue;
        }

      if (msg.msg_id == AUDIO_MSG_DEQUEUE || msg.msg_id == AUDIO_MSG_COMPLETE)
        {
          for (i = 0; i < g_pl.napb; i++)
            {
              if (g_pl.apb[i] != NULL && g_pl.apb[i] == msg.u.ptr)
                {
                  if (!back[i])
                    {
                      back[i] = true;
                      recovered++;
                    }

                  break;
                }
            }

          if (msg.msg_id == AUDIO_MSG_COMPLETE)
            {
              /* The driver played a FINAL buffer to the end; nothing
               * more will be consumed, but the caller refills anyway
               * and the next completion closes the stream. */

              recovered = outstanding;
              break;
            }
        }
    }

  /* Buffers that never came back still belong to the driver; only the
   * recovered ones may be refilled. */

  if (!wait_for_data())
    {
      return -1;
    }

  if (ioctl(g_pl.fd, AUDIOIOC_START, 0) < 0)
    {
      return -1;
    }

  outstanding = 0;
  for (i = 0; i < g_pl.napb; i++)
    {
      if (back[i] && fill_and_enqueue(g_pl.apb[i]) > 0)
        {
          outstanding++;
        }
    }

  pthread_mutex_lock(&g_pl.lock);
  g_pl.stats.restarts++;
  pthread_mutex_unlock(&g_pl.lock);
  return (int)outstanding;
}

static void *vg_tts_thread(void *arg)
{
  struct audio_msg_s msg;
  unsigned prio;
  unsigned outstanding = 0;
  bool complete = false;
  uint32_t resume_ms = 0;
  uint32_t t0;
  uint32_t used;
  bool eof_l;
  bool running = true;
  unsigned i;
  struct timespec ts;

  (void)arg;

  /* Phase A: wait for the prebuffer threshold (or the stream's end). */

  for (;;)
    {
      pthread_mutex_lock(&g_pl.lock);
      used = g_pl.wr - g_pl.rd;
      eof_l = g_pl.eof;
      pthread_mutex_unlock(&g_pl.lock);

      if (cancelled())
        {
          running = false;
          break;
        }

      if (used >= g_pl.prebuf_bytes || (eof_l && used > 0))
        {
          break;
        }

      if (eof_l && used == 0)
        {
          /* Nothing arrived at all; not an error worth failing the
           * round over — the transport's own classification handles
           * empty streams before we ever get here. */

          complete = true;
          running = false;
          break;
        }

      ts = slice_from_now(VG_TTS_MQ_SLICE_MS);
      sem_timedwait(&g_pl.activity, &ts);
    }

  if (running)
    {
      for (i = 0; i < g_pl.napb; i++)
        {
          if (fill_and_enqueue(g_pl.apb[i]) > 0)
            {
              outstanding++;
            }
        }

      if (g_pl.stats.failed)
        {
          /* The enqueue failed; do not dress that up as completion. */

          running = false;
        }
      else if (outstanding == 0)
        {
          complete = true;
          running = false;
        }
      else if (ioctl(g_pl.fd, AUDIOIOC_START, 0) < 0)
        {
          pthread_mutex_lock(&g_pl.lock);
          g_pl.stats.failed = true;
          g_pl.stats.err = errno != 0 ? -errno : -EIO;
          pthread_mutex_unlock(&g_pl.lock);
          running = false;
        }
      else if (g_pl.on_started != NULL)
        {
          g_pl.on_started(g_pl.on_started_arg);
        }
    }

  /* Phase B: streaming. */

  while (running)
    {
      if (cancelled())
        {
          break;
        }

      ts = slice_from_now(VG_TTS_MQ_SLICE_MS);
      if (mq_timedreceive(g_pl.mq, (char *)&msg, sizeof(msg), &prio,
                          &ts) < 0)
        {
          /* Timeout: if a resume after starvation enqueued audio but
           * nothing came back, the driver is not consuming — escalate. */

          if (resume_ms != 0 && outstanding > 0 &&
              now_ms() - resume_ms >= VG_TTS_LIVENESS_MS)
            {
              int r = escalate_restart(outstanding);

              if (r < 0)
                {
                  pthread_mutex_lock(&g_pl.lock);
                  g_pl.stats.failed = true;
                  if (g_pl.stats.err == 0)
                    {
                      g_pl.stats.err = -EIO;
                    }
                  pthread_mutex_unlock(&g_pl.lock);
                  running = false;
                }
              else if (r == 0)
                {
                  /* Restart recovered the buffers but there was
                   * nothing left to play. */

                  pthread_mutex_lock(&g_pl.lock);
                  used = g_pl.wr - g_pl.rd;
                  eof_l = g_pl.eof;
                  pthread_mutex_unlock(&g_pl.lock);

                  if (eof_l && used == 0)
                    {
                      complete = true;
                      running = false;
                    }

                  resume_ms = 0;
                }
              else
                {
                  outstanding = (unsigned)r;
                  resume_ms = now_ms();
                }
            }

          continue;
        }

      switch (msg.msg_id)
        {
          case AUDIO_MSG_DEQUEUE:
            {
              struct ap_buffer_s *apb = (struct ap_buffer_s *)msg.u.ptr;

              resume_ms = 0;
              outstanding--;

              pthread_mutex_lock(&g_pl.lock);
              used = g_pl.wr - g_pl.rd;
              eof_l = g_pl.eof;
              pthread_mutex_unlock(&g_pl.lock);

              if (used > 0)
                {
                  if (fill_and_enqueue(apb) > 0)
                    {
                      outstanding++;
                    }

                  break;
                }

              if (eof_l)
                {
                  /* Hold the buffer: when every outstanding buffer has
                   * come back, everything enqueued has been played. */

                  if (outstanding == 0 && !g_pl.stats.failed)
                    {
                      complete = true;
                    }

                  break;
                }

              /* Underrun: wait for data (or the end) in cancel-aware
               * slices, then resume with a re-enqueue + re-START. */

              pthread_mutex_lock(&g_pl.lock);
              g_pl.stats.underruns++;
              pthread_mutex_unlock(&g_pl.lock);

              if (!wait_for_data())
                {
                  running = false;
                  break;
                }

              if (fill_and_enqueue(apb) > 0)
                {
                  outstanding++;

                  /* Harmless while RUNNING (audio_start falls through
                   * to OK); re-arms the stream if the upper half had
                   * marked it XRUN. */

                  ioctl(g_pl.fd, AUDIOIOC_START, 0);

                  pthread_mutex_lock(&g_pl.lock);
                  g_pl.stats.restarts++;
                  pthread_mutex_unlock(&g_pl.lock);
                  resume_ms = now_ms();
                }
              else if (g_pl.eof && outstanding == 0)
                {
                  complete = true;
                }
              break;
            }

          case AUDIO_MSG_COMPLETE:
            complete = true;
            running = false;
            break;

          case AUDIO_MSG_STOP:
            /* The driver ended the stream (our STOP or its own). */

            running = false;
            break;

          default:
            break;
        }

      if (complete && outstanding == 0)
        {
          break;
        }
    }

  if (g_pl.fd >= 0)
    {
      ioctl(g_pl.fd, AUDIOIOC_STOP, 0);

      /* Let the in-flight buffers come home; bounded, then teardown
       * proceeds regardless (FREEBUFFER is best effort, like nxplayer's
       * abort path). */

      t0 = now_ms();
      while (outstanding > 0 && now_ms() - t0 < VG_TTS_RECOVER_MS)
        {
          struct timespec ts2 = slice_from_now(50);

          if (mq_timedreceive(g_pl.mq, (char *)&msg, sizeof(msg), &prio,
                              &ts2) < 0)
            {
              continue;
            }

          if (msg.msg_id == AUDIO_MSG_DEQUEUE)
            {
              outstanding--;
            }
          else if (msg.msg_id == AUDIO_MSG_COMPLETE ||
                   msg.msg_id == AUDIO_MSG_STOP)
            {
              break;
            }
        }
    }

  dev_teardown();

  pthread_mutex_lock(&g_pl.lock);
  g_pl.stats.complete = complete;
  g_pl.stats.played_ms = g_pl.stats.played_bytes / 64u;
  g_pl.thread_done = true;
  pthread_mutex_unlock(&g_pl.lock);
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

  if (ms > 1000u)
    {
      ms = 1000u;
    }

  return ms;
}

int vg_tts_player_start(uint32_t prebuf_ms, const volatile int *cancel,
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

  r = dev_setup();
  if (r < 0)
    {
      g_pl.active = false;
      return r;
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

      if (g_pl.abort_req)
        {
          pthread_mutex_unlock(&g_pl.lock);
          return -EPIPE;
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
