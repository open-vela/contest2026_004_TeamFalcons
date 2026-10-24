/****************************************************************************
 * app/velaguard/vg_voice_pcm.c
 ****************************************************************************/

#include "vg_voice_pcm.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint16_t le16(const uint8_t *p)
{
  return (uint16_t)(p[0] | (p[1] << 8));
}

static uint32_t le32(const uint8_t *p)
{
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
         ((uint32_t)p[3] << 24);
}

static void put_le16(uint8_t *p, uint16_t v)
{
  p[0] = (uint8_t)v;
  p[1] = (uint8_t)(v >> 8);
}

static void put_le32(uint8_t *p, uint32_t v)
{
  p[0] = (uint8_t)v;
  p[1] = (uint8_t)(v >> 8);
  p[2] = (uint8_t)(v >> 16);
  p[3] = (uint8_t)(v >> 24);
}

static void wav_header(uint8_t h[VG_PCM_WAV_HDR], uint16_t channels,
                       uint32_t rate, uint32_t data_bytes)
{
  uint16_t block = (uint16_t)(channels * 2);

  memset(h, 0, VG_PCM_WAV_HDR);
  memcpy(h, "RIFF", 4);
  put_le32(h + 4, data_bytes + 36);
  memcpy(h + 8, "WAVEfmt ", 8);
  put_le32(h + 16, 16);
  put_le16(h + 20, 1);
  put_le16(h + 22, channels);
  put_le32(h + 24, rate);
  put_le32(h + 28, rate * block);
  put_le16(h + 32, block);
  put_le16(h + 34, 16);
  memcpy(h + 36, "data", 4);
  put_le32(h + 40, data_bytes);
}

void vg_pcm_wav_header_mono(uint8_t hdr[VG_PCM_WAV_HDR], uint32_t rate,
                             uint32_t data_bytes)
{
  wav_header(hdr, 1, rate, data_bytes);
}

void vg_pcm_wav_header_stereo(uint8_t hdr[VG_PCM_WAV_HDR], uint32_t rate,
                              uint32_t data_bytes)
{
  wav_header(hdr, 2, rate, data_bytes);
}

int vg_pcm_wav_probe_stream(FILE *f, struct vg_pcm_wav *out)
{
  uint8_t hdr[12];
  bool have_fmt = false;

  if (f == NULL || out == NULL)
    {
      return -EINVAL;
    }

  memset(out, 0, sizeof(*out));

  if (fread(hdr, 1, sizeof(hdr), f) != sizeof(hdr) ||
      memcmp(hdr, "RIFF", 4) != 0 || memcmp(hdr + 8, "WAVE", 4) != 0)
    {
      return -EINVAL;
    }

  for (;;)
    {
      uint8_t ck[8];
      uint32_t size;
      long pos;

      if (fread(ck, 1, sizeof(ck), f) != sizeof(ck))
        {
          break;
        }

      size = le32(ck + 4);
      pos = ftell(f);

      if (memcmp(ck, "fmt ", 4) == 0)
        {
          uint8_t fmt[16];

          if (size < sizeof(fmt) || fread(fmt, 1, sizeof(fmt), f) != sizeof(fmt))
            {
              break;
            }

          out->channels = le16(fmt + 2);
          out->rate = le32(fmt + 4);
          out->bits = le16(fmt + 14);
          have_fmt = true;
        }
      else if (memcmp(ck, "data", 4) == 0)
        {
          if (!have_fmt)
            {
              break;
            }

          out->data_off = (uint32_t)pos;
          out->data_bytes = size;
          return 0;
        }

      if (pos < 0 || fseek(f, pos + (long)size + (long)(size & 1u),
                           SEEK_SET) != 0)
        {
          break;
        }
    }

  return -EINVAL;
}

int vg_pcm_wav_probe(const char *path, struct vg_pcm_wav *out)
{
  FILE *f;
  int ret;

  if (path == NULL || out == NULL)
    {
      return -EINVAL;
    }

  f = fopen(path, "rb");
  if (f == NULL)
    {
      return -errno;
    }

  ret = vg_pcm_wav_probe_stream(f, out);
  fclose(f);
  return ret;
}

int vg_pcm_wav_downmix_mono(const char *src, const char *dst)
{
  struct vg_pcm_wav info;
  uint8_t hdr[VG_PCM_WAV_HDR];
  uint8_t in[4096];
  int16_t out[2048];
  uint32_t frame_bytes;
  uint32_t frames;
  uint32_t done = 0;
  FILE *fi = NULL;
  FILE *fo = NULL;
  int ret = 0;

  if (src == NULL || dst == NULL)
    {
      return -EINVAL;
    }

  ret = vg_pcm_wav_probe(src, &info);
  if (ret != 0)
    {
      return ret;
    }

  if (info.bits != 16 || info.channels == 0 || info.channels > 2)
    {
      return -ENOTSUP;
    }

  frame_bytes = (uint32_t)info.channels * 2u;
  frames = info.data_bytes / frame_bytes;

  fi = fopen(src, "rb");
  if (fi == NULL)
    {
      return -errno;
    }

  fo = fopen(dst, "wb");
  if (fo == NULL)
    {
      fclose(fi);
      return -errno;
    }

  wav_header(hdr, 1, info.rate, frames * 2u);
  if (fwrite(hdr, 1, sizeof(hdr), fo) != sizeof(hdr))
    {
      ret = -EIO;
      goto out;
    }

  if (fseek(fi, (long)info.data_off, SEEK_SET) != 0)
    {
      ret = -EIO;
      goto out;
    }

  while (done < frames && ret == 0)
    {
      uint32_t batch = frames - done;
      size_t got;
      uint32_t i;

      if (batch > sizeof(out) / sizeof(out[0]))
        {
          batch = sizeof(out) / sizeof(out[0]);
        }

      got = fread(in, 1, (size_t)batch * frame_bytes, fi);
      if (got < (size_t)batch * frame_bytes)
        {
          ret = -EIO;
          break;
        }

      for (i = 0; i < batch; i++)
        {
          out[i] = (int16_t)le16(in + (size_t)i * frame_bytes);
        }

      if (fwrite(out, 2, batch, fo) != batch)
        {
          ret = -EIO;
          break;
        }

      done += batch;
    }

out:
  if (fo != NULL && fclose(fo) != 0 && ret == 0)
    {
      ret = -EIO;
    }

  if (fi != NULL)
    {
      fclose(fi);
    }

  return ret;
}

int vg_pcm_resample_24k_16k(const int16_t *in, size_t in_n,
                            int16_t *out, size_t out_cap)
{
  size_t out_n;
  size_t j;

  if (in == NULL || out == NULL)
    {
      return -1;
    }

  /* 3 input samples become 2 output samples. */

  out_n = (in_n / 3u) * 2u;

  if (out_n > out_cap)
    {
      out_n = out_cap;
    }

  for (j = 0; j < out_n; j++)
    {
      /* Position j * 3/2 in input samples.  idx is in half-samples so the
       * fractional part is exact: even -> on a sample, odd -> halfway. */

      size_t idx = j * 3u;
      size_t i0 = idx / 2u;
      int32_t frac = (int32_t)(idx % 2u);

      if (i0 + 1 < in_n)
        {
          int32_t a = in[i0];
          int32_t b = in[i0 + 1];

          out[j] = (int16_t)((a * (2 - frac) + b * frac) / 2);
        }
      else if (i0 < in_n)
        {
          out[j] = in[i0];
        }
      else
        {
          out[j] = 0;
        }
    }

  return (int)out_n;
}

/* ── streaming 3:2 resampler ─────────────────────────────────────── */

void vg_pcm_rs24_16_init(struct vg_pcm_rs24_16 *rs)
{
  rs->carry = 0;
  rs->carry_valid = false;
  rs->pend_n = 0;
}

int vg_pcm_rs24_16_feed(struct vg_pcm_rs24_16 *rs, const uint8_t *in,
                        size_t in_bytes, int16_t *out, size_t out_cap)
{
  size_t in_samples;
  size_t need;
  size_t out_n = 0;
  size_t i = 0;

  if (rs == NULL || (in == NULL && in_bytes > 0) || out == NULL)
    {
      return -1;
    }

  /* All-or-nothing: the caller must hand us room for the whole chunk so the
   * state never advances past a partially written output. */

  in_samples = in_bytes / 2;
  need = ((rs->pend_n + in_samples) / 3u) * 2u;

  if (need > out_cap)
    {
      return -1;
    }

  while (i < in_bytes)
    {
      int16_t s;

      if (rs->carry_valid)
        {
          s = (int16_t)((uint16_t)rs->carry | ((uint16_t)in[i] << 8));
          i++;
          rs->carry_valid = false;
        }
      else if (i + 1 < in_bytes)
        {
          s = (int16_t)((uint16_t)in[i] | ((uint16_t)in[i + 1] << 8));
          i += 2;
        }
      else
        {
          /* Low byte of a sample whose high byte is still on the wire. */

          rs->carry = in[i];
          rs->carry_valid = true;
          break;
        }

      /* A group completes on its third sample; it is consumed directly so
       * pend[] only ever holds the 0..2 samples of an open group. */

      if (rs->pend_n == 2)
        {
          out[out_n++] = rs->pend[0];
          out[out_n++] = (int16_t)(((int32_t)rs->pend[1] +
                                    (int32_t)s) / 2);
          rs->pend_n = 0;
        }
      else
        {
          rs->pend[rs->pend_n++] = s;
        }
    }

  return (int)out_n;
}

int vg_pcm_rs24_16_flush(struct vg_pcm_rs24_16 *rs, bool *odd_total)
{
  if (rs == NULL)
    {
      return -1;
    }

  if (odd_total != NULL)
    {
      *odd_total = rs->carry_valid;
    }

  /* The incomplete group is dropped: the batch path floors to whole
   * groups, so keeping it here would diverge from the WAV it writes. */

  rs->carry = 0;
  rs->carry_valid = false;
  rs->pend_n = 0;
  return 0;
}

int vg_pcm_writer_open(struct vg_pcm_writer *w, const char *path,
                       size_t samples)
{
  uint8_t hdr[VG_PCM_WAV_HDR];
  FILE *f;

  if (w == NULL || path == NULL)
    {
      return -EINVAL;
    }

  w->f = NULL;
  w->expected = samples;
  w->written = 0;

  f = fopen(path, "wb");
  if (f == NULL)
    {
      return -errno;
    }

  wav_header(hdr, 2, 16000, (uint32_t)(samples * 4u));
  if (fwrite(hdr, 1, sizeof(hdr), f) != sizeof(hdr))
    {
      fclose(f);
      return -EIO;
    }

  w->f = f;
  return 0;
}

int vg_pcm_writer_write(struct vg_pcm_writer *w, const int16_t *mono, size_t n)
{
  int16_t frames[1024];
  size_t done = 0;
  FILE *f;

  if (w == NULL || mono == NULL || w->f == NULL)
    {
      return -EINVAL;
    }

  f = (FILE *)w->f;

  while (done < n)
    {
      size_t batch = n - done;
      size_t i;

      if (batch > sizeof(frames) / (2 * sizeof(frames[0])))
        {
          batch = sizeof(frames) / (2 * sizeof(frames[0]));
        }

      for (i = 0; i < batch; i++)
        {
          frames[2 * i] = mono[done + i];
          frames[2 * i + 1] = mono[done + i];
        }

      if (fwrite(frames, 4, batch, f) != batch)
        {
          return -EIO;
        }

      done += batch;
    }

  w->written += n;
  return 0;
}

int vg_pcm_writer_close(struct vg_pcm_writer *w)
{
  int ret = 0;

  if (w == NULL || w->f == NULL)
    {
      return -EINVAL;
    }

  if (fclose((FILE *)w->f) != 0)
    {
      ret = -EIO;
    }

  w->f = NULL;

  /* A short file means a chunk was lost; the header already promised the
   * full length, so report it rather than leaving a WAV that plays noise. */

  if (ret == 0 && w->written != w->expected)
    {
      ret = -EIO;
    }

  return ret;
}

int vg_pcm_wav_write_16k_stereo(const char *path, const int16_t *mono,
                                size_t samples)
{
  struct vg_pcm_writer w;
  int ret;

  if (path == NULL || mono == NULL)
    {
      return -EINVAL;
    }

  ret = vg_pcm_writer_open(&w, path, samples);
  if (ret != 0)
    {
      return ret;
    }

  ret = vg_pcm_writer_write(&w, mono, samples);
  if (ret != 0)
    {
      fclose((FILE *)w.f);
      w.f = NULL;
      return ret;
    }

  return vg_pcm_writer_close(&w);
}

static const char k_b64[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static size_t b64_group(const uint8_t *p, size_t n, char *out)
{
  uint32_t v = ((uint32_t)p[0] << 16) |
               ((uint32_t)(n > 1 ? p[1] : 0) << 8) |
               ((uint32_t)(n > 2 ? p[2] : 0));

  out[0] = k_b64[(v >> 18) & 0x3f];
  out[1] = k_b64[(v >> 12) & 0x3f];
  out[2] = (n > 1) ? k_b64[(v >> 6) & 0x3f] : '=';
  out[3] = (n > 2) ? k_b64[v & 0x3f] : '=';
  return 4;
}

size_t vg_b64_stream_feed(struct vg_b64_stream *s, const uint8_t *in,
                          size_t n, char *out, size_t out_cap)
{
  size_t written = 0;
  size_t i = 0;

  if (s == NULL || out == NULL)
    {
      return 0;
    }

  while (s->carry_n > 0 && s->carry_n < 3 && i < n)
    {
      s->carry[s->carry_n++] = in[i++];
    }

  if (s->carry_n == 3)
    {
      if (out_cap - written < 4)
        {
          return written;
        }

      written += b64_group(s->carry, 3, out + written);
      s->carry_n = 0;
    }

  while (n - i >= 3)
    {
      if (out_cap - written < 4)
        {
          return written;
        }

      written += b64_group(in + i, 3, out + written);
      i += 3;
    }

  while (i < n && s->carry_n < 3)
    {
      s->carry[s->carry_n++] = in[i++];
    }

  return written;
}

size_t vg_b64_stream_finish(struct vg_b64_stream *s, char *out, size_t out_cap)
{
  size_t n;

  if (s == NULL || out == NULL || s->carry_n == 0)
    {
      return 0;
    }

  if (out_cap < 4)
    {
      return 0;
    }

  n = b64_group(s->carry, s->carry_n, out);
  s->carry_n = 0;
  return n;
}

static int b64_val(char c)
{
  if (c >= 'A' && c <= 'Z') return c - 'A';
  if (c >= 'a' && c <= 'z') return c - 'a' + 26;
  if (c >= '0' && c <= '9') return c - '0' + 52;
  if (c == '+') return 62;
  if (c == '/') return 63;
  return -1;
}

int vg_b64_decode(const char *in, size_t in_len, uint8_t *out, size_t out_cap)
{
  size_t i = 0;
  size_t o = 0;

  if (in == NULL || out == NULL)
    {
      return -1;
    }

  while (i + 3 < in_len)
    {
      int a = b64_val(in[i]);
      int b = b64_val(in[i + 1]);
      int c = (in[i + 2] == '=') ? 0 : b64_val(in[i + 2]);
      int d = (in[i + 3] == '=') ? 0 : b64_val(in[i + 3]);
      uint32_t v;

      if (a < 0 || b < 0 || c < 0 || d < 0)
        {
          return -1;
        }

      if (o >= out_cap)
        {
          return -1;
        }

      v = ((uint32_t)a << 18) | ((uint32_t)b << 12) | ((uint32_t)c << 6) |
          (uint32_t)d;

      out[o++] = (uint8_t)(v >> 16);

      if (in[i + 2] != '=')
        {
          if (o >= out_cap)
            {
              return -1;
            }

          out[o++] = (uint8_t)(v >> 8);
        }

      if (in[i + 3] != '=')
        {
          if (o >= out_cap)
            {
              return -1;
            }

          out[o++] = (uint8_t)v;
        }

      i += 4;
    }

  return (int)o;
}

int vg_sse_line_data(const char *line, size_t len, char *out, size_t out_cap)
{
  size_t start;
  size_t n;

  if (line == NULL || out == NULL)
    {
      return 0;
    }

  while (len > 0 && (line[len - 1] == '\r' || line[len - 1] == '\n'))
    {
      len--;
    }

  if (len < 5 || strncmp(line, "data:", 5) != 0)
    {
      return 0;
    }

  start = 5;
  if (start < len && line[start] == ' ')
    {
      start++;
    }

  n = len - start;

  if (n == 6 && strncmp(line + start, "[DONE]", 6) == 0)
    {
      return -1;
    }

  if (n + 1 > out_cap)
    {
      return -2;
    }

  memcpy(out, line + start, n);
  out[n] = '\0';
  return 1;
}
