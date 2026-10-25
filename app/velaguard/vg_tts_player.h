/****************************************************************************
 * app/velaguard/vg_tts_player.h
 *
 * P1: bounded-ring PCM player that drives /dev/audio/pcm0p directly, so
 * TTS playback starts while the SSE stream is still arriving.  The ring
 * holds 16 kHz mono (the resampler's output); the player thread expands
 * it to the stereo format the driver was verified with.
 *
 * One active player at a time: the voice round is serialized, and the
 * round thread is both the producer (via push, from the SSE sink) and
 * the waiter (wait).  The consumer is the player thread spawned by
 * start() and joined by wait().
 ****************************************************************************/

#ifndef __APP_VELAGUARD_VG_TTS_PLAYER_H
#define __APP_VELAGUARD_VG_TTS_PLAYER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct vg_tts_player_stats
{
  uint32_t pushed_bytes;  /* 16 kHz mono bytes accepted into the ring */
  uint32_t played_bytes;  /* stereo bytes handed to the driver */
  uint32_t returned_bytes; /* stereo bytes normally returned by the driver */
  uint32_t played_ms;     /* queued duration; not a wall-clock measurement */
  uint32_t underruns;     /* driver starved while the stream was open */
  uint32_t stall_ms;      /* time spent waiting for data while starved */
  uint32_t ready_ms;      /* monotonic instant the initial buffer was ready */
  uint32_t started_ms;    /* monotonic instant AUDIOIOC_START succeeded */
  uint32_t start_delay_ms;
  uint32_t rebuffer_ms;   /* last recovery threshold */
  bool     started;
  uint8_t  restarts;      /* resumes after starvation (re-START) */
  uint8_t  leaked_buffers; /* abandoned at teardown: driver never returned
                              them; freeing would risk a DMA write-after-
                              free, so they stay allocated */
  bool     complete;      /* every enqueued byte finished playing */
  bool     failed;        /* device or enqueue error */
  int      err;
};

/* Open the audio device, configure it (16 kHz stereo 16-bit PCM) and
 * start the consumer thread.  The thread waits for prebuf_ms of audio
 * before it starts the stream; on_started runs on the player thread
 * right after AUDIOIOC_START succeeds (the round logs its play markers
 * there). deadline_ms is an absolute monotonic synthesis deadline (0 disables
 * it); EOF drain uses wait()'s separate bound. Returns 0 or -errno. */
int vg_tts_player_start(uint32_t prebuf_ms, uint32_t deadline_ms,
                        const volatile int *cancel,
                        void (*on_started)(void *arg), void *arg);

/* Producer side.  Push 16 kHz mono PCM, whole samples only.  Blocks
 * while the ring is full, in slices that watch the cancel flag, so the
 * transport's TCP backpressure takes care of the rest.  -ECANCELED when
 * the round was cancelled, -ETIMEDOUT on expiry, or the consumer error.
 * -EPIPE after abort, EOF, completion or without a start. */
int vg_tts_player_push(const uint8_t *mono16k, size_t bytes);

/* The producer saw [DONE]: play what is buffered, then finish cleanly. */
int vg_tts_player_eof(void);

/* The producer gave up: stop the device now and discard buffered audio. */
void vg_tts_player_abort(void);

/* Wait for the player thread to exit, at most limit_ms; on expiry abort
 * and re-wait bounded.  Fills *out when non-NULL.  0, or -ETIMEDOUT if
 * even the abort wait expired (thread wedged in the driver). */
int vg_tts_player_wait(uint32_t limit_ms, struct vg_tts_player_stats *out);

uint32_t vg_tts_player_prebuf_ms(void);

/* Clamp to 50..4000 ms and ring capacity; takes effect on the next start(). Returns the
 * value actually applied. */
int vg_tts_player_set_prebuf_ms(uint32_t ms);

/* Startup bisection for the SDRAM heap-writer hunt (diagnostic only,
 * default 0 = full product behaviour): 1 = ring + thread only, the
 * audio device is never touched; 2 = full device session (open/RESERVE/
 * CONFIGURE/ALLOCBUFFER/mq) but no buffer is ever enqueued, so no DMA
 * runs.  Takes effect on the next start(). */
uint8_t vg_tts_player_bisect_level(void);
int vg_tts_player_set_bisect_level(int level);

#endif /* __APP_VELAGUARD_VG_TTS_PLAYER_H */
