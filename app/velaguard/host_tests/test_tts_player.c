/****************************************************************************
 * app/velaguard/host_tests/test_tts_player.c
 *
 * P1: the ring player's state machine against a simulated audio driver.
 * The module is included directly; open/ioctl/close are link-wrapped so
 * the test implements GETBUFFERINFO (4 x 8192), ALLOCBUFFER and an
 * instant-return ENQUEUE feeder that posts AUDIO_MSG_DEQUEUE back
 * through the module's own (real) POSIX mqueue, exactly like wm8994's
 * worker does.  Covered: prebuffer gating, stereo expansion and stream
 * content, full-ring backpressure, deterministic starvation (underrun,
 * stall, resume), abort and cancellation without deadlocks, device
 * failure fallback, prebuffer clamping.
 ****************************************************************************/

#include <errno.h>
#include <fcntl.h>
#include <mqueue.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "vg_tts_player.h"

#include "../vg_tts_player.c"

static int g_fail;

#define CHECK(cond) \
  do \
    { \
      if (!(cond)) \
        { \
          printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
          g_fail++; \
        } \
    } \
  while (0)

/* ── simulated driver ────────────────────────────────────────────── */

static volatile int g_open_fail;
static volatile int g_hold;          /* 1: keep DEQUEUEs back */
static volatile int g_start_count;
static volatile int g_started_flag;
static volatile int g_enqueued;
static mqd_t g_drv_mq = (mqd_t)-1;
static struct ap_buffer_s *g_apbs[8];
static int g_napb;
static struct ap_buffer_s *g_held[8];
static int g_nheld;

static int16_t g_played[200000];     /* mono samples in enqueue order */
static size_t g_played_n;
static int g_final_flags;
static int g_configure_count;
static struct audio_caps_s g_last_caps;

static void post_msg(uint16_t id, void *ptr)
{
  struct audio_msg_s msg;

  memset(&msg, 0, sizeof(msg));
  msg.msg_id = id;
  msg.u.ptr = ptr;

  if (g_drv_mq != (mqd_t)-1 &&
      mq_send(g_drv_mq, (const char *)&msg, sizeof(msg), 0) != 0)
    {
      printf("FAIL driver mq_send: %s\n", strerror(errno));
      g_fail++;
    }
}

int __wrap_open(const char *path, int flags, ...)
{
  (void)flags;

  if (g_open_fail || strcmp(path, "/dev/audio/pcm0p") != 0)
    {
      errno = ENOENT;
      return -1;
    }

  return 42;
}

int __wrap_close(int fd)
{
  CHECK(fd == 42);
  return 0;
}

int __wrap_ioctl(int fd, unsigned long req, ...)
{
  void *arg;
  va_list ap;

  (void)fd;
  va_start(ap, req);
  arg = va_arg(ap, void *);
  va_end(ap);

  switch ((int)req)
    {
      case AUDIOIOC_GETCAPS:
        {
          struct audio_caps_s *caps = arg;
          caps->ac_type = AUDIO_TYPE_OUTPUT;
          return caps->ac_len;
        }

      case AUDIOIOC_RESERVE:
      case AUDIOIOC_RELEASE:
      case AUDIOIOC_UNREGISTERMQ:
      case AUDIOIOC_STOP:
        return 0;

      case AUDIOIOC_CONFIGURE:
        {
          struct audio_caps_desc_s *d = arg;
          g_configure_count++;
          g_last_caps = d->caps;
          return 0;
        }

      case AUDIOIOC_START:
        g_start_count++;
        return 0;

      case AUDIOIOC_REGISTERMQ:
        g_drv_mq = (mqd_t)(intptr_t)arg;
        return 0;

      case AUDIOIOC_GETBUFFERINFO:
        {
          struct ap_buffer_info_s *info = arg;
          info->nbuffers = 4;
          info->buffer_size = 8192;
          return 0;
        }

      case AUDIOIOC_ALLOCBUFFER:
        {
          struct audio_buf_desc_s *d = arg;
          struct ap_buffer_s *apb = calloc(1, sizeof(*apb));

          CHECK(apb != NULL);
          if (apb == NULL)
            {
              return -1;
            }

          apb->nmaxbytes = d->numbytes;
          apb->samp = calloc(1, d->numbytes);
          CHECK(apb->samp != NULL);
          *d->u.pbuffer = apb;
          g_apbs[g_napb++] = apb;
          return sizeof(*d);
        }

      case AUDIOIOC_FREEBUFFER:
        {
          struct audio_buf_desc_s *d = arg;
          struct ap_buffer_s *apb = d->u.buffer;
          int i;

          for (i = 0; i < g_napb; i++)
            {
              if (g_apbs[i] == apb)
                {
                  g_apbs[i] = g_apbs[g_napb - 1];
                  g_napb--;
                  break;
                }
            }

          free(apb->samp);
          free(apb);
          return 0;
        }

      case AUDIOIOC_ENQUEUEBUFFER:
        {
          struct audio_buf_desc_s *d = arg;
          struct ap_buffer_s *apb = d->u.buffer;
          size_t samples = apb->nbytes / 4u;
          const int16_t *s = (const int16_t *)(const void *)apb->samp;
          size_t i;

          g_enqueued++;

          for (i = 0; i < samples; i++)
            {
              /* the expansion duplicates every mono sample */

              CHECK(s[2 * i] == s[2 * i + 1]);
              CHECK(g_played_n < sizeof(g_played) / sizeof(g_played[0]));
              if (g_played_n < sizeof(g_played) / sizeof(g_played[0]))
                {
                  g_played[g_played_n++] = s[2 * i];
                }
            }

          if ((apb->flags & AUDIO_APB_FINAL) != 0)
            {
              g_final_flags++;
            }

          if (g_hold)
            {
              g_held[g_nheld++] = apb;
            }
          else
            {
              post_msg(AUDIO_MSG_DEQUEUE, apb);
              if ((apb->flags & AUDIO_APB_FINAL) != 0)
                {
                  post_msg(AUDIO_MSG_COMPLETE, NULL);
                }
            }

          return 0;
        }

      case AUDIOIOC_RESETSTATUS:
        CHECK(!"RESETSTATUS must never be used (internal-ring only)");
        return -1;

      default:
        printf("FAIL unexpected ioctl %lu\n", req);
        g_fail++;
        return -1;
    }
}

/* ── helpers ─────────────────────────────────────────────────────── */

static uint8_t g_pattern[300000];

static void pattern_fill(void)
{
  size_t i;

  for (i = 0; i < sizeof(g_pattern); i++)
    {
      g_pattern[i] = (uint8_t)(i * 7u + 3u);
    }
}

static void reset_capture(void)
{
  g_played_n = 0;
  g_final_flags = 0;
  g_start_count = 0;
  g_started_flag = 0;
  g_enqueued = 0;
  g_nheld = 0;
  g_napb = 0;
  g_hold = 0;
}

/* Bounded polls.  Wall clock, not clock(): a mostly-sleeping process
 * accumulates almost no CPU time, so clock()-based timeouts never fire. */

static uint32_t wall_ms(void)
{
  struct timespec t;

  clock_gettime(CLOCK_MONOTONIC, &t);
  return (uint32_t)((uint64_t)t.tv_sec * 1000u + (uint64_t)t.tv_nsec / 1000000u);
}

static void wait_flag(volatile int *flag, int timeout_ms)
{
  uint32_t t0 = wall_ms();

  while (!*flag)
    {
      usleep(10000);
      if (wall_ms() - t0 > (uint32_t)timeout_ms)
        {
          break;
        }
    }
}

/* Bounded poll until *flag >= value. */

static void wait_flag_value(volatile int *flag, int value, int timeout_ms)
{
  uint32_t t0 = wall_ms();

  while (*flag < value)
    {
      usleep(10000);
      if (wall_ms() - t0 > (uint32_t)timeout_ms)
        {
          break;
        }
    }
}

static void on_started_cb(void *arg)
{
  (void)arg;
  g_started_flag = 1;
}

static volatile int g_helper_done;

static void *push_helper(void *arg)
{
  size_t bytes = (size_t)(intptr_t)arg;

  CHECK(vg_tts_player_push(g_pattern, bytes) == 0);
  g_helper_done = 1;
  return NULL;
}

/* ── tests ───────────────────────────────────────────────────────── */

static void test_device_failure(void)
{
  struct vg_tts_player_stats st;
  uint8_t two[2] = { 0, 0 };

  g_open_fail = 1;
  memset(&st, 0, sizeof(st));
  CHECK(vg_tts_player_start(200, NULL, NULL, NULL) < 0);
  CHECK(vg_tts_player_push(two, sizeof(two)) == -EPIPE);
  CHECK(vg_tts_player_wait(100, &st) == -EPIPE);
  g_open_fail = 0;
}

static void test_prebuf_clamp(void)
{
  CHECK(vg_tts_player_set_prebuf_ms(10) == 50);
  CHECK(vg_tts_player_set_prebuf_ms(5000) == 1000);
  CHECK(vg_tts_player_prebuf_ms() == 1000);
  CHECK(vg_tts_player_set_prebuf_ms(200) == 200);
}

static void test_basic_play(void)
{
  struct vg_tts_player_stats st;
  int cancel = 0;

  reset_capture();
  CHECK(vg_tts_player_set_prebuf_ms(50) == 50);  /* 1600 mono bytes */
  CHECK(vg_tts_player_start(50, &cancel, on_started_cb, NULL) == 0);

  /* configure shape: 16 kHz stereo 16-bit PCM */

  CHECK(g_configure_count == 1);
  CHECK(g_last_caps.ac_channels == 2);
  CHECK(g_last_caps.ac_subtype == AUDIO_FMT_PCM);
  CHECK(g_last_caps.ac_controls.hw[0] == 16000);
  CHECK(g_last_caps.ac_controls.b[2] == 16);

  /* below the threshold nothing starts */

  CHECK(vg_tts_player_push(g_pattern, 800) == 0);
  usleep(150000);
  CHECK(g_started_flag == 0);
  CHECK(g_start_count == 0);

  /* crossing it starts the stream */

  CHECK(vg_tts_player_push(g_pattern + 800, 800) == 0);
  wait_flag(&g_started_flag, 2000);
  CHECK(g_started_flag == 1);
  CHECK(g_start_count >= 1);

  CHECK(vg_tts_player_push(g_pattern + 1600, 3200) == 0);
  CHECK(vg_tts_player_eof() == 0);
  CHECK(vg_tts_player_wait(3000, &st) == 0);

  CHECK(st.complete);
  CHECK(!st.failed);
  CHECK(st.pushed_bytes == 4800);
  CHECK(st.played_bytes == 9600);
  CHECK(st.played_ms == 150);
  CHECK(g_played_n == 2400);
  CHECK(g_final_flags >= 1);
  CHECK(memcmp(g_played, g_pattern, 4800) == 0);

  /* the player is free for the next round */

  CHECK(vg_tts_player_push(g_pattern, 2) == -EPIPE);
}

static void test_abort_and_cancel(void)
{
  struct vg_tts_player_stats st;
  int cancel = 0;

  /* abort mid-stream: buffered audio is discarded, no completion */

  reset_capture();
  CHECK(vg_tts_player_start(50, &cancel, on_started_cb, NULL) == 0);
  CHECK(vg_tts_player_push(g_pattern, 1600) == 0);
  wait_flag(&g_started_flag, 2000);
  CHECK(g_started_flag == 1);
  usleep(100000);                     /* parked in the underrun wait */
  vg_tts_player_abort();
  CHECK(vg_tts_player_wait(2000, &st) == 0);
  CHECK(!st.complete);
  CHECK(vg_tts_player_start(50, &cancel, on_started_cb, NULL) == 0);
  vg_tts_player_abort();
  CHECK(vg_tts_player_wait(2000, &st) == 0);

  /* cancellation from the round: the transport's ctl.cancel flag */

  reset_capture();
  CHECK(vg_tts_player_start(50, &cancel, on_started_cb, NULL) == 0);
  CHECK(vg_tts_player_push(g_pattern, 1600) == 0);
  wait_flag(&g_started_flag, 2000);
  usleep(100000);
  cancel = 1;
  CHECK(vg_tts_player_wait(2000, &st) == 0);
  CHECK(!st.complete);
  cancel = 0;
}

static void test_backpressure_and_starvation(void)
{
  struct vg_tts_player_stats st;
  pthread_t helper;
  int cancel = 0;
  int i;

  reset_capture();
  g_hold = 1;
  g_helper_done = 0;
  CHECK(vg_tts_player_set_prebuf_ms(50) == 50);
  CHECK(vg_tts_player_start(50, &cancel, on_started_cb, NULL) == 0);

  /* 140000 bytes into a 131072-byte ring: the producer must block on
   * the full ring while the driver holds every buffer. */

  CHECK(pthread_create(&helper, NULL, push_helper,
                       (void *)(intptr_t)140000) == 0);
  wait_flag_value(&g_enqueued, 4, 4000);
  CHECK(g_enqueued >= 4);

  usleep(200000);
  CHECK(g_helper_done == 0);
  CHECK(g_nheld == 4);

  /* release the held buffers: the drain resumes, the producer unblocks
   * and finishes once the ring empties into the driver. */

  g_hold = 0;
  for (i = 0; i < g_nheld; i++)
    {
      post_msg(AUDIO_MSG_DEQUEUE, g_held[i]);
    }

  wait_flag(&g_helper_done, 4000);
  CHECK(g_helper_done == 1);
  CHECK(pthread_join(helper, NULL) == 0);

  /* every buffer has come home and the ring is empty: a deterministic
   * starvation window while the producer has nothing more. */

  usleep(300000);
  CHECK(vg_tts_player_push(g_pattern + 140000, 2048) == 0);
  usleep(100000);
  CHECK(vg_tts_player_eof() == 0);
  CHECK(vg_tts_player_wait(4000, &st) == 0);

  CHECK(st.complete);
  CHECK(st.pushed_bytes == 140000 + 2048);
  CHECK(st.played_bytes == 2u * (140000 + 2048));
  CHECK(st.underruns >= 1);
  CHECK(st.stall_ms >= 250);
  CHECK(st.restarts >= 1);
  CHECK(g_played_n == (140000 + 2048) / 2);
  CHECK(memcmp(g_played, g_pattern, 140000 + 2048) == 0);
}

int main(void)
{
  setvbuf(stdout, NULL, _IONBF, 0);
  pattern_fill();

  test_device_failure();
  printf("test_tts_player: device failure OK\n");
  test_prebuf_clamp();
  printf("test_tts_player: prebuf clamp OK\n");
  test_basic_play();
  printf("test_tts_player: basic play OK\n");
  test_abort_and_cancel();
  printf("test_tts_player: abort/cancel OK\n");
  test_backpressure_and_starvation();
  printf("test_tts_player: backpressure OK\n");

  if (g_fail == 0)
    {
      printf("test_tts_player: OK (prebuffer gating, expansion, "
             "backpressure, starvation/underrun, abort, cancel, "
             "device failure)\n");
      return 0;
    }

  printf("test_tts_player: %d failure(s)\n", g_fail);
  return 1;
}
