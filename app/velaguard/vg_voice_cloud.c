/****************************************************************************
 * app/velaguard/vg_voice_cloud.c
 *
 * BVO-11: ASR upload and TTS download over the agent's HTTPS transport.
 *
 * ASR streams base64 from a bounded PCM recording or diagnostic file. TTS
 * accumulates at most 512 KB of decoded PCM before conversion to a WAV.
 * The original microphone recording is owned and wiped by the round caller.
 ****************************************************************************/

#include <nuttx/config.h>

#if defined(CONFIG_VG_VOICE_LOOPBACK) && defined(CONFIG_EXAMPLES_AI_AGENT_VELA)

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <syslog.h>
#include <unistd.h>

#include <netutils/cJSON.h>

#include <agent_http.h>
#include "vg_voice.h"
#include "vg_voice_cloud.h"
#include "vg_voice_pcm.h"

#define VC_READ_CHUNK   4096
#define VC_B64_CHUNK    6144
#define VC_RESP_MAX     4096
/* One SSE line carries a whole base64 audio chunk.  The service does not
 * promise a chunk size, and a real 24 kHz answer arrived as a single line
 * well over 24 KB, so the buffer starts at 32 KB and doubles up to 512 KB.
 * Beyond that the line is not something this decoder understands. */

#define VC_SSE_LINE_INIT 32768
#define VC_SSE_LINE_MAX  524288
#define VC_PCM_CHUNK    16384
#define VC_PCM_SAMPLES  15360   /* multiple of 3 for 24k->16k */

static const char *TAG = "vgvoice";

const char *vg_vc_result_name(enum vg_vc_result r)
{
  switch (r)
    {
      case VG_VC_OK:        return "ok";
      case VG_VC_EMPTY:     return "empty";
      case VG_VC_CANCELLED: return "cancelled";
      case VG_VC_TIMEOUT:   return "timeout";
      case VG_VC_NET:       return "net";
      case VG_VC_HTTP:      return "http";
      case VG_VC_FORMAT:    return "format";
      case VG_VC_IO:        return "io";
      case VG_VC_NO_CRED:   return "no_cred";
      case VG_VC_NO_TEXT:   return "no_text";
      default:              return "?";
    }
}

static enum vg_vc_result vc_from_transport(int ret)
{
  switch (ret)
    {
      case AGENT_HTTP_ERR_CANCELLED: return VG_VC_CANCELLED;
      case AGENT_HTTP_ERR_TIMEOUT:   return VG_VC_TIMEOUT;
      default:                     return VG_VC_NET;
    }
}

/* ── request body: prefix, base64 of a file, suffix ─────────────── */

enum vc_src_state
{
  VC_SRC_PREFIX = 0,
  VC_SRC_HEADER,
  VC_SRC_BODY,
  VC_SRC_FLUSH,
  VC_SRC_SUFFIX,
  VC_SRC_DONE
};

/* The request body carries one WAV file as base64.  The WAV header is
 * generated here and the samples come either from a file or from the
 * recording that is still in RAM, so an upload never needs a second copy
 * of the audio on the file system. */

struct vc_body_src
{
  const char *prefix;
  const char *suffix;
  FILE       *f;                /* file source, or NULL */
  const uint8_t *mem;           /* memory source, or NULL */
  size_t     mem_len;
  size_t     mem_off;
  uint16_t   src_channels;      /* 1 or 2 */
  uint32_t   rate;
  uint32_t   out_samples;       /* mono samples in the uploaded WAV */
  struct vg_b64_stream b64;
  enum vc_src_state state;
  uint8_t in[VC_READ_CHUNK];
  uint8_t mono[VC_READ_CHUNK / 2];
  char    out[VC_B64_CHUNK];
  size_t  out_len;
  size_t  out_off;
  size_t  emitted;
};

static int vc_src_fill(struct vc_body_src *s)
{
  s->out_len = 0;
  s->out_off = 0;

  switch (s->state)
    {
      case VC_SRC_PREFIX:
        s->out_len = strlen(s->prefix);
        if (s->out_len > sizeof(s->out))
          {
            return -1;
          }

        memcpy(s->out, s->prefix, s->out_len);
        s->state = VC_SRC_HEADER;
        break;

      case VC_SRC_HEADER:
        {
          uint8_t hdr[VG_PCM_WAV_HDR];
          size_t n;

          /* The uploaded file is always 16-bit mono, whatever the
           * recording format was. */

          vg_pcm_wav_header_mono(hdr, s->rate, s->out_samples * 2u);
          n = vg_b64_stream_feed(&s->b64, hdr, sizeof(hdr), s->out,
                                 sizeof(s->out));
          s->state = VC_SRC_BODY;
          if (n == 0)
            {
              return vc_src_fill(s);
            }

          s->out_len = n;
        }
        break;

      case VC_SRC_BODY:
        {
          size_t got = 0;

          if (s->mem != NULL)
            {
              size_t left = s->mem_len - s->mem_off;

              got = (left > sizeof(s->in)) ? sizeof(s->in) : left;
              memcpy(s->in, s->mem + s->mem_off, got);
              s->mem_off += got;
            }
          else if (s->f != NULL)
            {
              got = fread(s->in, 1, sizeof(s->in), s->f);
            }

          if (got == 0)
            {
              s->state = VC_SRC_FLUSH;
              return vc_src_fill(s);
            }

          if (s->src_channels == 2)
            {
              /* Keep the first channel of every frame, so a stereo
               * recording is uploaded as the mono the service expects
               * without a second file. */

              size_t frames = got / 4u;
              size_t i;

              for (i = 0; i < frames; i++)
                {
                  s->mono[i * 2] = s->in[i * 4];
                  s->mono[i * 2 + 1] = s->in[i * 4 + 1];
                }

              s->out_len = vg_b64_stream_feed(&s->b64, s->mono, frames * 2u,
                                              s->out, sizeof(s->out));
            }
          else
            {
              s->out_len = vg_b64_stream_feed(&s->b64, s->in, got, s->out,
                                              sizeof(s->out));
            }

          if (s->out_len == 0)
            {
              /* Not an error: the encoder is carrying a partial group. */

              return vc_src_fill(s);
            }
        }
        break;

      case VC_SRC_FLUSH:
        s->out_len = vg_b64_stream_finish(&s->b64, s->out, sizeof(s->out));
        s->state = VC_SRC_SUFFIX;

        if (s->out_len == 0)
          {
            return vc_src_fill(s);
          }
        break;

      case VC_SRC_SUFFIX:
        s->out_len = strlen(s->suffix);
        if (s->out_len > sizeof(s->out))
          {
            return -1;
          }

        memcpy(s->out, s->suffix, s->out_len);
        s->state = VC_SRC_DONE;
        break;

      case VC_SRC_DONE:
      default:
        s->out_len = 0;
        return 0;
    }

  return 1;
}

static int vc_src_read(void *ctx, char *buf, size_t cap)
{
  struct vc_body_src *s = (struct vc_body_src *)ctx;
  size_t n;

  while (s->out_off >= s->out_len)
    {
      int rc = vc_src_fill(s);

      if (rc < 0)
        {
          return -1;
        }

      if (rc == 0)
        {
          return 0;
        }
    }

  n = s->out_len - s->out_off;
  if (n > cap)
    {
      n = cap;
    }

  memcpy(buf, s->out + s->out_off, n);
  s->out_off += n;
  s->emitted += n;
  return (int)n;
}

/* ── JSON helpers ───────────────────────────────────────────────── */

static int vc_json_escape(const char *in, char *out, size_t cap)
{
  size_t o = 0;

  if (in == NULL || out == NULL || cap == 0)
    {
      return -1;
    }

  for (; *in != '\0'; in++)
    {
      unsigned char c = (unsigned char)*in;

      if (c == '"' || c == '\\')
        {
          if (o + 2 >= cap)
            {
              return -1;
            }

          out[o++] = '\\';
          out[o++] = (char)c;
        }
      else if (c == '\n' || c == '\r' || c == '\t')
        {
          if (o + 2 >= cap)
            {
              return -1;
            }

          out[o++] = '\\';
          out[o++] = (c == '\n') ? 'n' : (c == '\r') ? 'r' : 't';
        }
      else if (c < 0x20)
        {
          if (o + 6 >= cap)
            {
              return -1;
            }

          snprintf(out + o, cap - o, "\\u%04x", c);
          o += 6;
        }
      else
        {
          if (o + 1 >= cap)
            {
              return -1;
            }

          out[o++] = (char)c;
        }
    }

  out[o] = '\0';
  return (int)o;
}

static const char *vc_extract_content(cJSON *root)
{
  cJSON *choices = cJSON_GetObjectItem(root, "choices");
  cJSON *first;
  cJSON *message;
  cJSON *content;

  if (!cJSON_IsArray(choices))
    {
      return NULL;
    }

  first = cJSON_GetArrayItem(choices, 0);
  if (first == NULL)
    {
      return NULL;
    }

  message = cJSON_GetObjectItem(first, "message");
  if (message == NULL)
    {
      return NULL;
    }

  content = cJSON_GetObjectItem(message, "content");
  if (!cJSON_IsString(content))
    {
      return NULL;
    }

  return content->valuestring;
}

/* ── ASR ────────────────────────────────────────────────────────── */

/* The upload always carries one 16-bit mono WAV.  The samples come from
 * the file that was given or from the recording that is still in RAM, so
 * the recognition never needs a second copy of the audio on the file
 * system. */

static const char vc_asr_prefix[] =
    "{\"model\":\"" CONFIG_VG_VOICE_ASR_MODEL "\","
    "\"messages\":[{\"role\":\"user\",\"content\":[{\"type\":\"input_audio\","
    "\"input_audio\":{\"data\":\"data:audio/wav;base64,";
static const char vc_asr_suffix[] =
    "\"}}]}],\"asr_options\":{\"language\":\"zh\"}}";

#ifdef CONFIG_VG_VOICE_TEST_FIXTURE
#include <pthread.h>
static pthread_mutex_t g_fixture_lock = PTHREAD_MUTEX_INITIALIZER;
static char g_fixture_mode[24];
int vg_voice_cloud_fixture(const char *mode)
{
  static const char *const modes[] = {"off", "asr401", "asr429", "asrempty",
    "asrbad", "asrtruncate", "asrstall", "asrdns", "tts401", "tts429",
    "ttstruncate", "ttsbad", "ttsstall", NULL};
  bool valid = false;
  for (int i=0; mode && modes[i]; i++) if (!strcmp(mode,modes[i])) valid=true;
  if (!valid) return -EINVAL;
  pthread_mutex_lock(&g_fixture_lock);
  snprintf(g_fixture_mode,sizeof(g_fixture_mode),"%s",strcmp(mode,"off")?mode:"");
  pthread_mutex_unlock(&g_fixture_lock);
  return 0;
}
#endif

static int vc_post(bool asr, const char *data, const vela_body_src_t *body,
                    const vela_resp_sink_t *sink, char *response, size_t cap,
                    size_t *bytes, int *status, const vela_req_ctl_t *ctl)
{
#ifdef CONFIG_VG_VOICE_TEST_FIXTURE
  char mode[24], path[40];
  pthread_mutex_lock(&g_fixture_lock);
  memcpy(mode,g_fixture_mode,sizeof(mode));
  /* 只注入一次目标请求；TTS 重试回到真实服务，保留原文字。 */
  if (mode[0] && (!asr || !strncmp(mode,"asr",3))) g_fixture_mode[0] = 0;
  pthread_mutex_unlock(&g_fixture_lock);
  if (mode[0]) {
    const char *profile = asr ? (!strncmp(mode,"asr",3)?mode:"ok_asr") : mode;
    snprintf(path,sizeof(path),"/%s",profile);
    return agent_http_probe_post_stream(!strcmp(mode,"asrdns") ?
        "vg-voice-fixture.invalid":"192.168.137.1", "8443",path,data,body,sink,
        response,cap,bytes,status,ctl);
  }
#else
  (void)asr;
#endif
  return agent_http_post_stream(data,body,sink,response,cap,bytes,status,ctl);
}

/* Post the prepared body and read the transcript out of the reply. */
static enum vg_vc_result vc_asr_post(struct vc_body_src *src, size_t body_len,
                                     const vela_req_ctl_t *ctl, char *text,
                                     size_t cap, struct vg_vc_info *info)
{
  char resp[VC_RESP_MAX];
  size_t body_out = 0;
  int status = 0;
  int ret;
  vela_body_src_t body;

  body.len = body_len;
  body.read = vc_src_read;
  body.ctx = src;

  ret = vc_post(true, NULL, &body, NULL, resp, sizeof(resp),
                               &body_out, &status, ctl);

  info->status = status;
  info->resp_bytes = (uint32_t)body_out;
  info->req_bytes = (uint32_t)body_len;

  if (ret != 0)
    {
      enum vg_vc_result rc = vc_from_transport(ret);

      syslog(LOG_ERR, "[%s] asr: transport failed: %d (%s)\n", TAG, ret,
             vg_vc_result_name(rc));
      return rc;
    }

  if (status != 200)
    {
      syslog(LOG_ERR, "[%s] asr: http %d\n", TAG, status);
      return VG_VC_HTTP;
    }

  {
    cJSON *root = cJSON_Parse(resp);
    const char *content;

    if (root == NULL)
      {
        return VG_VC_FORMAT;
      }

    content = vc_extract_content(root);

    if (content == NULL)
      {
        cJSON_Delete(root);
        return VG_VC_FORMAT;
      }

    if (content[0] == '\0')
      {
        cJSON_Delete(root);
        return VG_VC_EMPTY;
      }

    if (strlen(content) >= cap)
      {
        /* A cut transcript could name a different point.  Refuse it
         * instead of sending a truncated question to the agent. */

        cJSON_Delete(root);
        return VG_VC_FORMAT;
      }

    snprintf(text, cap, "%s", content);
    cJSON_Delete(root);
  }

  return VG_VC_OK;
}

/* The elapsed time covers the whole recognition, file work included. */
static enum vg_vc_result vc_asr_done(enum vg_vc_result rc,
                                     struct vg_vc_info *info,
                                     const struct timespec *mono_start)
{
  struct timespec mono_end;

  clock_gettime(CLOCK_MONOTONIC, &mono_end);
  info->elapsed_ms =
      (uint32_t)((mono_end.tv_sec - mono_start->tv_sec) * 1000
                 + (mono_end.tv_nsec - mono_start->tv_nsec) / 1000000);
  return rc;
}

enum vg_vc_result vg_voice_cloud_asr(const char *clip_path,
                                     const vela_req_ctl_t *ctl,
                                     char *text, size_t cap,
                                     struct vg_vc_info *info)
{
  struct vg_pcm_wav wav;
  struct vc_body_src src;
  struct vg_vc_info local;
  struct timespec mono_start;
  size_t body_len;
  size_t b64_len;
  size_t upload_bytes;
  enum vg_vc_result rc;

  if (info == NULL)
    {
      info = &local;
    }

  memset(info, 0, sizeof(*info));
  clock_gettime(CLOCK_MONOTONIC, &mono_start);

  if (text == NULL || cap == 0)
    {
      return VG_VC_IO;
    }

  text[0] = '\0';

  if (clip_path == NULL)
    {
      clip_path = VG_VC_CLIP_PATH;
    }

  src.f = fopen(clip_path, "rb");
  if (src.f == NULL)
    {
      vg_voice_cloud_cleanup();
      return VG_VC_IO;
    }

  int probe_rc = vg_pcm_wav_probe_stream(src.f, &wav);
  if (probe_rc != 0 || wav.bits != 16 ||
      wav.channels == 0 || wav.channels > 2 || wav.data_bytes == 0)
    {
      syslog(LOG_ERR, "[%s] asr: wav probe failed rc=%d bits=%u channels=%u\n",
             TAG, probe_rc, wav.bits, wav.channels);
      fclose(src.f);
      src.f = NULL;
      vg_voice_cloud_cleanup();
      return VG_VC_IO;
    }

  /* A file that ends before its data chunk claims would make the request
   * shorter than the length it announces, and the server would wait for
   * bytes that never come. */

  if (fseek(src.f, 0, SEEK_END) == 0)
    {
      long fsize = ftell(src.f);
      if (fsize > (long)wav.data_off)
        {
          size_t avail = (size_t)fsize - (size_t)wav.data_off;
          if (avail < wav.data_bytes)
            {
              wav.data_bytes = (uint32_t)avail;
            }
        }
    }

  FILE *f = src.f;
  memset(&src, 0, sizeof(src));
  src.f = f;
  src.prefix = vc_asr_prefix;
  src.suffix = vc_asr_suffix;
  src.src_channels = wav.channels;
  src.rate = wav.rate;
  src.out_samples = wav.data_bytes / ((uint32_t)wav.channels * 2u);

  if (fseek(src.f, (long)wav.data_off, SEEK_SET) != 0)
    {
      fclose(src.f);
      vg_voice_cloud_cleanup();
      return VG_VC_IO;
    }

  upload_bytes = VG_PCM_WAV_HDR + (size_t)src.out_samples * 2u;
  b64_len = 4u * ((upload_bytes + 2u) / 3u);
  body_len = strlen(vc_asr_prefix) + b64_len + strlen(vc_asr_suffix);

  src.state = VC_SRC_PREFIX;

  rc = vc_asr_post(&src, body_len, ctl, text, cap, info);

  fclose(src.f);
  src.f = NULL;

  rc = vc_asr_done(rc, info, &mono_start);

  /* The recording exists only for this recognition; neither the file nor
   * a mono copy is kept for a retry. */

  vg_voice_cloud_cleanup();
  return rc;
}

/* Recognise a recording that is still in RAM.  The board keeps the
 * captured samples until the next recording, so the upload needs neither
 * a WAV file nor a mono copy of it on the eMMC. */
enum vg_vc_result vg_voice_cloud_asr_buffer(const int16_t *mono,
                                            size_t mono_bytes,
                                            const vela_req_ctl_t *ctl,
                                            char *text, size_t cap,
                                            struct vg_vc_info *info)
{
  struct vc_body_src src;
  struct vg_vc_info local;
  struct timespec mono_start;
  size_t body_len;
  size_t b64_len;
  size_t upload_bytes;
  enum vg_vc_result rc;

  if (info == NULL)
    {
      info = &local;
    }

  memset(info, 0, sizeof(*info));
  clock_gettime(CLOCK_MONOTONIC, &mono_start);

  if (text == NULL || cap == 0 || mono == NULL || mono_bytes < 2 ||
      mono_bytes > VG_VOICE_PCM_MAX_BYTES || (mono_bytes & 1u) != 0)
    {
      return VG_VC_IO;
    }

  text[0] = '\0';

  memset(&src, 0, sizeof(src));
  src.prefix = vc_asr_prefix;
  src.suffix = vc_asr_suffix;
  src.mem = (const uint8_t *)mono;
  src.mem_len = mono_bytes;
  src.src_channels = 1;
  src.rate = VG_VOICE_RATE_HZ;
  src.out_samples = (uint32_t)(mono_bytes / 2u);
  src.state = VC_SRC_PREFIX;

  upload_bytes = VG_PCM_WAV_HDR + mono_bytes;
  b64_len = 4u * ((upload_bytes + 2u) / 3u);
  body_len = strlen(vc_asr_prefix) + b64_len + strlen(vc_asr_suffix);

  rc = vc_asr_post(&src, body_len, ctl, text, cap, info);
  rc = vc_asr_done(rc, info, &mono_start);
  return rc;
}

/* ── TTS ────────────────────────────────────────────────────────── */

/* The synthesised samples are kept in RAM until the reply is complete
 * and are then resampled into the WAV the player takes.  An answer is a
 * few seconds of speech, so the buffer stays far below the limit below,
 * and no intermediate file is written to the eMMC. */
#define VC_PCM_MAX_BYTES (512u * 1024u)

struct vc_tts_sink
{
  uint8_t *pcm;
  size_t   pcm_len;
  size_t   pcm_cap;
  char  *line;
  size_t line_len;
  size_t line_cap;
  char  *payload;
  size_t pcm_bytes;
  bool   done;
  int    error;
};

static int vc_tts_pcm_reserve(struct vc_tts_sink *s, size_t extra)
{
  size_t need = s->pcm_len + extra;
  size_t ncap;
  uint8_t *buf;

  if (need <= s->pcm_cap)
    {
      return 0;
    }

  if (need > VC_PCM_MAX_BYTES)
    {
      syslog(LOG_ERR, "[%s] tts: reply over %u bytes\n", TAG,
             (unsigned)VC_PCM_MAX_BYTES);
      return -1;
    }

  ncap = (s->pcm_cap == 0) ? 32768 : s->pcm_cap * 2;
  while (ncap < need)
    {
      ncap *= 2;
    }

  if (ncap > VC_PCM_MAX_BYTES)
    {
      ncap = VC_PCM_MAX_BYTES;
    }

  buf = realloc(s->pcm, ncap);
  if (buf == NULL)
    {
      return -1;
    }

  s->pcm = buf;
  s->pcm_cap = ncap;
  return 0;
}

static int vc_tts_emit_pcm(struct vc_tts_sink *s, const char *b64, size_t len)
{
  uint8_t pcm[VC_PCM_CHUNK];
  const size_t step = (sizeof(pcm) / 3) * 4;  /* whole base64 groups */
  size_t off = 0;

  /* The service chooses the chunk size, and a real answer arrived as one
   * 40 KB base64 string.  Decode in pieces that fit the buffer instead of
   * assuming a chunk size. */

  while (off < len)
    {
      size_t take = len - off;
      int n;

      if (take > step)
        {
          take = step;
        }

      n = vg_b64_decode(b64 + off, take, pcm, sizeof(pcm));
      if (n < 0)
        {
          syslog(LOG_ERR, "[%s] tts: bad base64 chunk of %u bytes\n", TAG,
                 (unsigned)take);
          return -1;
        }

      if (n > 0)
        {
          if (vc_tts_pcm_reserve(s, (size_t)n) != 0)
            {
              return -1;
            }

          memcpy(s->pcm + s->pcm_len, pcm, (size_t)n);
          s->pcm_len += (size_t)n;
        }

      s->pcm_bytes += (size_t)n;
      off += take;
    }

  return 0;
}

static int vc_tts_grow(struct vc_tts_sink *s)
{
  size_t ncap = (s->line_cap == 0) ? VC_SSE_LINE_INIT : s->line_cap * 2;
  char *line;
  char *payload;

  if (ncap > VC_SSE_LINE_MAX)
    {
      return -1;
    }

  line = realloc(s->line, ncap);
  if (line == NULL)
    {
      return -1;
    }

  payload = realloc(s->payload, ncap);
  if (payload == NULL)
    {
      s->line = line;
      return -1;
    }

  s->line = line;
  s->payload = payload;
  s->line_cap = ncap;
  return 0;
}

static int vc_tts_line(struct vc_tts_sink *s)
{
  int rc;

  if (s->line_len == 0)
    {
      return 0;
    }

  s->line[s->line_len] = '\0';

  rc = vg_sse_line_data(s->line, s->line_len, s->payload, s->line_cap);
  s->line_len = 0;

  if (rc == -1)
    {
      s->done = true;
      return 0;
    }

  if (rc <= 0)
    {
      return (rc == -2) ? -1 : 0;
    }

  {
    cJSON *root = cJSON_Parse(s->payload);
    cJSON *choices;
    cJSON *first;
    cJSON *delta;
    cJSON *audio;
    cJSON *data;

    if (root == NULL)
      {
        return -1;
      }

    choices = cJSON_GetObjectItem(root, "choices");
    first = cJSON_IsArray(choices) ? cJSON_GetArrayItem(choices, 0) : NULL;
    delta = (first != NULL) ? cJSON_GetObjectItem(first, "delta") : NULL;
    audio = (delta != NULL) ? cJSON_GetObjectItem(delta, "audio") : NULL;
    data = (audio != NULL) ? cJSON_GetObjectItem(audio, "data") : NULL;
    cJSON *finish = first ? cJSON_GetObjectItem(first, "finish_reason") : NULL;
    if (cJSON_IsString(finish) && strcmp(finish->valuestring, "stop") == 0)
      s->done = true;

    if (cJSON_IsString(data) && data->valuestring != NULL)
      {
        rc = vc_tts_emit_pcm(s, data->valuestring,
                             strlen(data->valuestring));
        cJSON_Delete(root);
        return rc;
      }

    cJSON_Delete(root);
  }

  return 0;
}

static int vc_tts_write(void *ctx, const char *data, size_t len)
{
  struct vc_tts_sink *s = (struct vc_tts_sink *)ctx;
  size_t i;

  for (i = 0; i < len; i++)
    {
      if (s->error != 0)
        {
          return -1;
        }

      if (data[i] == '\n')
        {
          if (vc_tts_line(s) < 0)
            {
              s->error = -1;
              return -1;
            }

          if (s->done)
            {
              return 1;
            }

          continue;
        }

      if (s->line_len + 1 >= s->line_cap && vc_tts_grow(s) < 0)
        {
          /* A line this long is not a data line we understand; stopping is
           * better than silently dropping audio. */

          syslog(LOG_ERR, "[%s] tts: SSE line over %u bytes\n", TAG,
                 (unsigned)s->line_cap);
          s->error = -1;
          return -1;
        }

      s->line[s->line_len++] = data[i];
    }

  return s->done ? 1 : 0;
}

enum vg_vc_result vg_voice_cloud_tts(const char *text, const char *wav_out,
                                     const vela_req_ctl_t *ctl,
                                     struct vg_vc_info *info)
{
  struct vg_vc_info local;
  struct vc_tts_sink sink;
  char escaped[VG_VC_TEXT_MAX * 2];
  char *body = NULL;
  char resp[VC_RESP_MAX];
  const char *out_path = (wav_out != NULL) ? wav_out : VG_VC_TTS_WAV_PATH;
  size_t body_cap;
  size_t body_out = 0;
  int status = 0;
  int ret;
  enum vg_vc_result rc = VG_VC_OK;
  struct timespec mono_start;
  struct timespec mono_end;

  if (info == NULL)
    {
      info = &local;
    }

  memset(info, 0, sizeof(*info));

  clock_gettime(CLOCK_MONOTONIC, &mono_start);

  if (text == NULL || text[0] == '\0')
    {
      return VG_VC_NO_TEXT;
    }

  if (vc_json_escape(text, escaped, sizeof(escaped)) < 0)
    {
      return VG_VC_FORMAT;
    }

  body_cap = strlen(escaped) + 256;
  body = malloc(body_cap);
  if (body == NULL)
    {
      return VG_VC_IO;
    }

  snprintf(body, body_cap,
           "{\"model\":\"" CONFIG_VG_VOICE_TTS_MODEL "\","
           "\"messages\":[{\"role\":\"assistant\",\"content\":\"%s\"}],"
           "\"audio\":{\"format\":\"pcm16\",\"voice\":\""
           CONFIG_VG_VOICE_TTS_VOICE "\"},\"stream\":true}",
           escaped);

  memset(&sink, 0, sizeof(sink));
  if (vc_tts_grow(&sink) < 0)
    {
      free(body);
      return VG_VC_IO;
    }

  {
    vela_resp_sink_t rs;

    rs.write = vc_tts_write;
    rs.ctx = &sink;

    ret = vc_post(false, body, NULL, &rs, resp, sizeof(resp),
                               &body_out, &status, ctl);
  }

  info->status = status;
  info->resp_bytes = (uint32_t)body_out;
  info->req_bytes = (uint32_t)strlen(body);

  info->audio_bytes = (uint32_t)sink.pcm_bytes;

  if (ret != 0 || sink.error != 0)
    {
      struct timespec now;
      clock_gettime(CLOCK_MONOTONIC, &now);
      uint32_t ms = (uint32_t)((uint64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000);
      if (agent_http_cancelled(ctl)) rc = VG_VC_CANCELLED;
      else if (ret == AGENT_HTTP_ERR_TIMEOUT || agent_http_stop(ctl, ms)) rc = VG_VC_TIMEOUT;
      else if (sink.error != 0) rc = VG_VC_FORMAT;
      else rc = (ret != 0) ? vc_from_transport(ret) : VG_VC_FORMAT;
      goto out;
    }

  if (status != 200)
    {
      rc = VG_VC_HTTP;
      goto out;
    }

  if (sink.pcm_bytes == 0 || (sink.pcm_bytes & 1u) ||
      sink.line_len != 0 || !sink.done)
    {
      rc = VG_VC_FORMAT;
      goto out;
    }

  /* Convert 24 kHz mono to the 16 kHz stereo the player was verified
   * with.  The samples are already in RAM, so this is one pass into the
   * WAV file and no intermediate file is involved. */

  {
    int16_t *pcm_in;
    int16_t *pcm_out;
    size_t samples = sink.pcm_len / 2;
    size_t done = 0;
    size_t out_total = (samples / 3u) * 2u;
    struct vg_pcm_writer w;

    if (samples == 0)
      {
        rc = VG_VC_IO;
        goto out;
      }

    pcm_in = malloc(VC_PCM_SAMPLES * sizeof(int16_t));
    pcm_out = malloc((VC_PCM_SAMPLES * 2u) * sizeof(int16_t));

    if (pcm_in == NULL || pcm_out == NULL)
      {
        free(pcm_in);
        free(pcm_out);
        rc = VG_VC_IO;
        goto out;
      }

    if (vg_pcm_writer_open(&w, out_path, out_total) != 0)
      {
        free(pcm_in);
        free(pcm_out);
        rc = VG_VC_IO;
        goto out;
      }

    while (done < samples)
      {
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        uint32_t now_ms = (uint32_t)((uint64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000);
        if (agent_http_stop(ctl, now_ms))
          {
            rc = agent_http_cancelled(ctl) ? VG_VC_CANCELLED : VG_VC_TIMEOUT;
            break;
          }
        size_t batch = samples - done;
        size_t got;
        int out_n;

        /* 15360 is a multiple of 3, so the 3:2 resampler never drops a
         * partial group in the middle of the signal. */

        if (batch > VC_PCM_SAMPLES)
          {
            batch = VC_PCM_SAMPLES;
          }

        memcpy(pcm_in, sink.pcm + done * 2, batch * 2);
        got = batch;

        out_n = vg_pcm_resample_24k_16k(pcm_in, got, pcm_out,
                                        VC_PCM_SAMPLES * 2u);
        if (out_n < 0 || vg_pcm_writer_write(&w, pcm_out,
                                             (size_t)out_n) != 0)
          {
            break;
          }

        done += got;
      }

    /* Close unconditionally: the writer holds the output FILE, and a short
     * read above would otherwise leave it open. */

    {
      int close_ret = vg_pcm_writer_close(&w);

      if (done != samples || close_ret != 0)
        {
          free(pcm_in);
          free(pcm_out);
          if (rc == VG_VC_OK) rc = VG_VC_IO;
          goto out;
        }
    }

    free(pcm_in);
    free(pcm_out);
  }

  clock_gettime(CLOCK_MONOTONIC, &mono_end);
  info->elapsed_ms =
      (uint32_t)((mono_end.tv_sec - mono_start.tv_sec) * 1000
                 + (mono_end.tv_nsec - mono_start.tv_nsec) / 1000000);
  uint32_t final_ms = (uint32_t)((uint64_t)mono_end.tv_sec * 1000 + mono_end.tv_nsec / 1000000);
  rc = agent_http_stop(ctl, final_ms)
      ? (agent_http_cancelled(ctl) ? VG_VC_CANCELLED : VG_VC_TIMEOUT)
      : VG_VC_OK;

out:
  clock_gettime(CLOCK_MONOTONIC, &mono_end);
  info->elapsed_ms =
      (uint32_t)((mono_end.tv_sec - mono_start.tv_sec) * 1000
                 + (mono_end.tv_nsec - mono_start.tv_nsec) / 1000000);

  unlink(VG_VC_TTS_PCM_PATH);

  if (rc != VG_VC_OK)
    {
      unlink(out_path);
    }

  free(sink.line);
  free(sink.payload);
  free(sink.pcm);
  free(body);
  return rc;
}

void vg_voice_cloud_cleanup(void)
{
  unlink(VG_VC_CLIP_PATH);
  unlink(VG_VC_CLIP_MONO_PATH);
  unlink(VG_VC_TTS_PCM_PATH);
}

#endif /* CONFIG_VG_VOICE_LOOPBACK && CONFIG_EXAMPLES_AI_AGENT_VELA */
