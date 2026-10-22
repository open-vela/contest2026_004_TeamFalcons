/****************************************************************************
 * app/velaguard/host_tests/test_voice_pcm.c
 *
 * BVO-11: the conversions between the recorded WAV, the ASR upload and the
 * TTS reply.  These are checked against known samples, not against the
 * assumption that a successful conversion sounds right.
 ****************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vg_voice_pcm.h"

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

#define TMP_STEREO "/tmp/vg-test-stereo.wav"
#define TMP_MONO   "/tmp/vg-test-mono.wav"
#define TMP_OUT    "/tmp/vg-test-out.wav"

static void write_bytes(const char *path, const uint8_t *data, size_t n)
{
  FILE *f = fopen(path, "wb");

  if (f == NULL)
    {
      printf("cannot create %s\n", path);
      exit(2);
    }

  fwrite(data, 1, n, f);
  fclose(f);
}

static void put16(uint8_t *p, uint16_t v)
{
  p[0] = (uint8_t)v;
  p[1] = (uint8_t)(v >> 8);
}

static void put32(uint8_t *p, uint32_t v)
{
  p[0] = (uint8_t)v;
  p[1] = (uint8_t)(v >> 8);
  p[2] = (uint8_t)(v >> 16);
  p[3] = (uint8_t)(v >> 24);
}

static void make_stereo(const char *path, const int16_t *frames, int n)
{
  uint8_t hdr[44];
  uint8_t *buf = malloc(sizeof(hdr) + (size_t)n * 4);
  int i;

  memset(hdr, 0, sizeof(hdr));
  memcpy(hdr, "RIFF", 4);
  put32(hdr + 4, (uint32_t)n * 4 + 36);
  memcpy(hdr + 8, "WAVEfmt ", 8);
  put32(hdr + 16, 16);
  put16(hdr + 20, 1);
  put16(hdr + 22, 2);
  put32(hdr + 24, 16000);
  put32(hdr + 28, 16000 * 4);
  put16(hdr + 32, 4);
  put16(hdr + 34, 16);
  memcpy(hdr + 36, "data", 4);
  put32(hdr + 40, (uint32_t)n * 4);

  memcpy(buf, hdr, sizeof(hdr));

  for (i = 0; i < n; i++)
    {
      put16(buf + 44 + i * 4, (uint16_t)frames[2 * i]);
      put16(buf + 46 + i * 4, (uint16_t)frames[2 * i + 1]);
    }

  write_bytes(path, buf, sizeof(hdr) + (size_t)n * 4);
  free(buf);
}

static void test_probe_and_downmix(void)
{
  struct vg_pcm_wav info;
  int16_t frames[8] = { 100, -100, 200, -200, 300, -300, 400, -400 };
  int16_t mono[4];
  FILE *f;

  make_stereo(TMP_STEREO, frames, 4);

  CHECK(vg_pcm_wav_probe(TMP_STEREO, &info) == 0);
  CHECK(info.rate == 16000);
  CHECK(info.channels == 2);
  CHECK(info.bits == 16);
  CHECK(info.data_off == 44);
  CHECK(info.data_bytes == 16);

  CHECK(vg_pcm_wav_downmix_mono(TMP_STEREO, TMP_MONO) == 0);
  CHECK(vg_pcm_wav_probe(TMP_MONO, &info) == 0);
  CHECK(info.channels == 1);
  CHECK(info.data_bytes == 8);

  f = fopen(TMP_MONO, "rb");
  CHECK(f != NULL);
  if (f != NULL)
    {
      CHECK(fseek(f, 44, SEEK_SET) == 0);
      CHECK(fread(mono, 2, 4, f) == 4);
      fclose(f);
    }

  CHECK(mono[0] == 100);
  CHECK(mono[1] == 200);
  CHECK(mono[2] == 300);
  CHECK(mono[3] == 400);

  /* A file that is not RIFF is refused rather than half-read. */

  write_bytes(TMP_OUT, (const uint8_t *)"not a wav at all", 16);
  CHECK(vg_pcm_wav_probe(TMP_OUT, &info) != 0);
}

static void test_resample(void)
{
  /* 6 input samples -> 4 output.  The first output lands on input 0, the
   * second halfway between 1 and 2, the third on 3, the fourth halfway
   * between 4 and 5. */

  int16_t in[6] = { 0, 100, 200, 300, 400, 500 };
  int16_t out[8];

  CHECK(vg_pcm_resample_24k_16k(in, 6, out, 8) == 4);
  CHECK(out[0] == 0);
  CHECK(out[1] == 150);
  CHECK(out[2] == 300);
  CHECK(out[3] == 450);

  /* A partial group at the end is dropped, not padded with silence. */

  CHECK(vg_pcm_resample_24k_16k(in, 5, out, 8) == 2);

  /* Output capacity is respected. */

  CHECK(vg_pcm_resample_24k_16k(in, 6, out, 2) == 2);
}

static void test_wav_write(void)
{
  struct vg_pcm_wav info;
  int16_t mono[5] = { 1, -1, 2, -2, 3 };

  CHECK(vg_pcm_wav_write_16k_stereo(TMP_OUT, mono, 5) == 0);
  CHECK(vg_pcm_wav_probe(TMP_OUT, &info) == 0);
  CHECK(info.rate == 16000);
  CHECK(info.channels == 2);
  CHECK(info.bits == 16);
  CHECK(info.data_bytes == 20);
}

static void test_base64(void)
{
  const uint8_t data[10] = { 0, 1, 2, 3, 250, 251, 252, 253, 254, 255 };
  char enc[64];
  uint8_t dec[16];
  struct vg_b64_stream s;
  size_t n = 0;
  int i;

  /* Fed one byte at a time, which is what a file reader does at the end of
   * a chunk. */

  memset(&s, 0, sizeof(s));

  for (i = 0; i < 10; i++)
    {
      n += vg_b64_stream_feed(&s, data + i, 1, enc + n, sizeof(enc) - n);
    }

  n += vg_b64_stream_finish(&s, enc + n, sizeof(enc) - n);
  enc[n] = '\0';

  CHECK(n == 16); /* ceil(10/3)*4 */

  {
    int got = vg_b64_decode(enc, n, dec, sizeof(dec));

    CHECK(got == 10);
    CHECK(memcmp(dec, data, 10) == 0);
  }

  /* Known vector: "Man" -> "TWFu". */

  memset(&s, 0, sizeof(s));
  n = vg_b64_stream_feed(&s, (const uint8_t *)"Man", 3, enc, sizeof(enc));
  n += vg_b64_stream_finish(&s, enc + n, sizeof(enc) - n);
  enc[n] = '\0';
  CHECK(strcmp(enc, "TWFu") == 0);

  /* Padding: "Ma" -> "TWE=". */

  memset(&s, 0, sizeof(s));
  n = vg_b64_stream_feed(&s, (const uint8_t *)"Ma", 2, enc, sizeof(enc));
  n += vg_b64_stream_finish(&s, enc + n, sizeof(enc) - n);
  enc[n] = '\0';
  CHECK(strcmp(enc, "TWE=") == 0);

  CHECK(vg_b64_decode("!!!!", 4, dec, sizeof(dec)) < 0);
}

static void test_sse(void)
{
  char out[64];

  CHECK(vg_sse_line_data("data: {\"a\":1}\r\n", 14, out, sizeof(out)) == 1);
  CHECK(strcmp(out, "{\"a\":1}") == 0);

  CHECK(vg_sse_line_data(": keep-alive", 12, out, sizeof(out)) == 0);
  CHECK(vg_sse_line_data("event: message", 14, out, sizeof(out)) == 0);
  CHECK(vg_sse_line_data("data: [DONE]", 12, out, sizeof(out)) == -1);

  /* A payload that does not fit is reported, not truncated: a cut base64
   * string decodes to garbage PCM. */

  CHECK(vg_sse_line_data("data: 0123456789", 16, out, 4) == -2);
}

int main(void)
{
  test_probe_and_downmix();
  test_resample();
  test_wav_write();
  test_base64();
  test_sse();

  remove(TMP_STEREO);
  remove(TMP_MONO);
  remove(TMP_OUT);

  if (g_fail == 0)
    {
      printf("test_voice_pcm: ok\n");
      return 0;
    }

  printf("test_voice_pcm: %d failure(s)\n", g_fail);
  return 1;
}
