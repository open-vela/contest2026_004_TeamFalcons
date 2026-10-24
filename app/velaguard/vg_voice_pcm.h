/****************************************************************************
 * app/velaguard/vg_voice_pcm.h
 *
 * BVO-11: audio helpers that do not touch the network or NuttX.
 *
 * The board records 16 kHz stereo WAV, the ASR endpoint takes a complete
 * file, and TTS returns 24 kHz mono PCM.  These conversions are pure so a
 * host test can check them against known samples instead of trusting that
 * "it sounded fine once".
 ****************************************************************************/

#ifndef __APP_VELAGUARD_VG_VOICE_PCM_H
#define __APP_VELAGUARD_VG_VOICE_PCM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdint.h>

#define VG_PCM_WAV_HDR 44

struct vg_pcm_wav
{
  uint32_t rate;
  uint16_t channels;
  uint16_t bits;
  uint32_t data_off;
  uint32_t data_bytes;
};

/* Build a 44-byte WAV header for 16-bit mono PCM.  The ASR upload
 * streams a header of its own instead of reading one from a file. */
void vg_pcm_wav_header_mono(uint8_t hdr[VG_PCM_WAV_HDR], uint32_t rate,
                             uint32_t data_bytes);

/* Same for stereo.  The pcm_decode wrapper on /dev/audio/pcm0p parses a
 * WAV header out of the first enqueued buffer to configure the codec,
 * so the streaming player prefixes its stream with one. */
void vg_pcm_wav_header_stereo(uint8_t hdr[VG_PCM_WAV_HDR], uint32_t rate,
                              uint32_t data_bytes);

/* Parse the RIFF header and locate the data chunk.  Returns 0 or -errno.
 * Only PCM 16-bit files are accepted by the callers here. */
int vg_pcm_wav_probe(const char *path, struct vg_pcm_wav *out);
int vg_pcm_wav_probe_stream(FILE *f, struct vg_pcm_wav *out);

/* Copy a 16-bit WAV as mono, taking the first channel of every frame.
 * The sample rate is preserved.  Returns 0 or -errno. */
int vg_pcm_wav_downmix_mono(const char *src, const char *dst);

/* 24 kHz to 16 kHz, ratio 3:2, linear interpolation.  Returns the number of
 * output samples written, or -1 on a bad argument.  out_cap is in samples. */
int vg_pcm_resample_24k_16k(const int16_t *in, size_t in_n,
                            int16_t *out, size_t out_cap);

/* Streaming form of the same conversion.  Fed the same samples in any
 * chunking it produces bit-identical output to vg_pcm_resample_24k_16k:
 * each complete group of 3 input samples yields a passthrough sample and
 * a pairwise average, and an incomplete trailing group is dropped at
 * flush — exactly the batch function's (in_n / 3) * 2 truncation.  A
 * sample byte split across feeds is reassembled; flush reports a dangling
 * byte through *odd_total.  feed writes nothing and returns -1 when out
 * cannot hold the whole chunk's output (size out for in_bytes/3*2+2). */
struct vg_pcm_rs24_16
{
  uint8_t carry;     /* dangling low byte when carry_valid */
  bool carry_valid;  /* 0xff is a legal byte value, so no magic sentinel */
  int16_t pend[2];
  uint8_t pend_n;    /* 0..2 samples of an incomplete group */
};

void vg_pcm_rs24_16_init(struct vg_pcm_rs24_16 *rs);
int vg_pcm_rs24_16_feed(struct vg_pcm_rs24_16 *rs, const uint8_t *in,
                        size_t in_bytes, int16_t *out, size_t out_cap);
int vg_pcm_rs24_16_flush(struct vg_pcm_rs24_16 *rs, bool *odd_total);

/* Write mono samples as a 16 kHz stereo 16-bit WAV.  Returns 0 or -errno. */
int vg_pcm_wav_write_16k_stereo(const char *path, const int16_t *mono,
                                size_t samples);

/* Streaming WAV writer: the header needs the total length up front, so the
 * caller states how many mono samples it will write.  Used for TTS, where
 * holding the whole 16 kHz signal would cost hundreds of KB. */
struct vg_pcm_writer
{
  void  *f;
  size_t expected;
  size_t written;
};

int vg_pcm_writer_open(struct vg_pcm_writer *w, const char *path,
                       size_t samples);
int vg_pcm_writer_write(struct vg_pcm_writer *w, const int16_t *mono,
                        size_t n);
int vg_pcm_writer_close(struct vg_pcm_writer *w);

/* Streaming base64.  Feed arbitrary byte counts; each call writes complete
 * groups and carries the remainder.  out_cap must leave room for the final
 * padding produced by vg_b64_stream_finish(). */
struct vg_b64_stream
{
  uint8_t carry[3];
  size_t  carry_n;
};

size_t vg_b64_stream_feed(struct vg_b64_stream *s, const uint8_t *in,
                          size_t n, char *out, size_t out_cap);
size_t vg_b64_stream_finish(struct vg_b64_stream *s, char *out,
                            size_t out_cap);

/* Decode one base64 block.  Returns bytes written, or -1 on bad input. */
int vg_b64_decode(const char *in, size_t in_len, uint8_t *out, size_t out_cap);

/* One line of an SSE stream.  Returns 1 when out holds a data payload,
 * 0 when the line carries no data, -1 for [DONE], -2 when out is too small. */
int vg_sse_line_data(const char *line, size_t len, char *out, size_t out_cap);

#endif /* __APP_VELAGUARD_VG_VOICE_PCM_H */
