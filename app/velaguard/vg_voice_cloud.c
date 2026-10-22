/****************************************************************************
 * app/velaguard/vg_voice_cloud.c
 *
 * BVO-11: ASR upload and TTS download over the agent's HTTPS transport.
 *
 * Neither direction ever holds a whole recording or a whole reply in RAM.
 * The request body is produced 4 KB at a time from the file, and the TTS
 * reply is consumed one SSE line at a time, so the peak is one chunk rather
 * than the 640 KB a 15 s recording becomes after base64.
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
#include "vg_voice_cloud.h"
#include "vg_voice_pcm.h"

#define VC_READ_CHUNK   4096
#define VC_B64_CHUNK    6144
#define VC_RESP_MAX     4096
#define VC_SSE_LINE_MAX 24576
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
  VC_SRC_BODY,
  VC_SRC_FLUSH,
  VC_SRC_SUFFIX,
  VC_SRC_DONE
};

struct vc_body_src
{
  const char *prefix;
  const char *suffix;
  FILE       *f;
  struct vg_b64_stream b64;
  enum vc_src_state state;
  uint8_t in[VC_READ_CHUNK];
  char    out[VC_B64_CHUNK];
  size_t  out_len;
  size_t  out_off;
  size_t  file_bytes;
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
        s->state = VC_SRC_BODY;
        break;

      case VC_SRC_BODY:
        {
          size_t got = fread(s->in, 1, sizeof(s->in), s->f);

          if (got == 0)
            {
              s->state = VC_SRC_FLUSH;
              return vc_src_fill(s);
            }

          s->out_len = vg_b64_stream_feed(&s->b64, s->in, got, s->out,
                                          sizeof(s->out));
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

enum vg_vc_result vg_voice_cloud_asr(const char *clip_path,
                                     const vela_req_ctl_t *ctl,
                                     char *text, size_t cap,
                                     struct vg_vc_info *info)
{
  static const char prefix[] =
      "{\"model\":\"" CONFIG_VG_VOICE_ASR_MODEL "\","
      "\"messages\":[{\"role\":\"user\",\"content\":[{\"type\":\"input_audio\","
      "\"input_audio\":{\"data\":\"data:audio/wav;base64,";
  static const char suffix[] =
      "\"}}]}],\"asr_options\":{\"language\":\"zh\"}}";

  struct vg_pcm_wav wav;
  struct vc_body_src src;
  struct vg_vc_info local;
  char resp[VC_RESP_MAX];
  char mono_path[64];
  size_t body_len = 0;
  size_t body_out = 0;
  size_t b64_len;
  int status = 0;
  int ret;
  enum vg_vc_result rc;
  struct stat st;

  if (info == NULL)
    {
      info = &local;
    }

  memset(info, 0, sizeof(*info));

  if (text == NULL || cap == 0)
    {
      return VG_VC_IO;
    }

  text[0] = '\0';

  if (clip_path == NULL)
    {
      clip_path = VG_VC_CLIP_PATH;
    }

  snprintf(mono_path, sizeof(mono_path), "%s", VG_VC_CLIP_MONO_PATH);

  ret = vg_pcm_wav_downmix_mono(clip_path, mono_path);
  if (ret != 0)
    {
      syslog(LOG_ERR, "[%s] asr: cannot prepare upload wav: %d\n", TAG, ret);
      vg_voice_cloud_cleanup();
      return VG_VC_IO;
    }

  if (vg_pcm_wav_probe(mono_path, &wav) != 0)
    {
      vg_voice_cloud_cleanup();
      return VG_VC_IO;
    }

  if (stat(mono_path, &st) != 0 || st.st_size <= 0)
    {
      vg_voice_cloud_cleanup();
      return VG_VC_IO;
    }

  src.file_bytes = (size_t)st.st_size;
  b64_len = 4u * ((src.file_bytes + 2u) / 3u);

  src.prefix = prefix;
  src.suffix = suffix;
  src.f = fopen(mono_path, "rb");
  if (src.f == NULL)
    {
      vg_voice_cloud_cleanup();
      return VG_VC_IO;
    }

  memset(&src.b64, 0, sizeof(src.b64));
  src.state = VC_SRC_PREFIX;
  src.out_len = 0;
  src.out_off = 0;
  src.emitted = 0;

  body_len = strlen(prefix) + b64_len + strlen(suffix);

  {
    vela_body_src_t body;

    body.len = body_len;
    body.read = vc_src_read;
    body.ctx = &src;

    ret = agent_http_post_stream(NULL, &body, NULL, resp, sizeof(resp),
                               &body_out, &status, ctl);
  }

  fclose(src.f);
  src.f = NULL;

  info->status = status;
  info->resp_bytes = (uint32_t)body_out;
  info->req_bytes = (uint32_t)body_len;

  if (ret != 0)
    {
      rc = vc_from_transport(ret);
      syslog(LOG_ERR, "[%s] asr: transport failed: %d (%s)\n", TAG, ret,
             vg_vc_result_name(rc));
      goto out;
    }

  if (status != 200)
    {
      syslog(LOG_ERR, "[%s] asr: http %d\n", TAG, status);
      rc = VG_VC_HTTP;
      goto out;
    }

  {
    cJSON *root = cJSON_Parse(resp);
    const char *content;

    if (root == NULL)
      {
        rc = VG_VC_FORMAT;
        goto out;
      }

    content = vc_extract_content(root);

    if (content == NULL)
      {
        cJSON_Delete(root);
        rc = VG_VC_FORMAT;
        goto out;
      }

    if (content[0] == '\0')
      {
        cJSON_Delete(root);
        rc = VG_VC_EMPTY;
        goto out;
      }

    if (strlen(content) >= cap)
      {
        /* A cut transcript could name a different point.  Refuse it
         * instead of sending a truncated question to the agent. */

        cJSON_Delete(root);
        rc = VG_VC_FORMAT;
        goto out;
      }

    snprintf(text, cap, "%s", content);
    cJSON_Delete(root);
  }

  rc = VG_VC_OK;

out:
  /* The recording exists only for this recognition.  The mono copy goes
   * with it; neither is retained for a retry. */

  vg_voice_cloud_cleanup();
  return rc;
}

/* ── TTS ────────────────────────────────────────────────────────── */

struct vc_tts_sink
{
  int    fd;
  char  *line;
  size_t line_len;
  size_t line_cap;
  size_t pcm_bytes;
  bool   done;
  int    error;
};

static int vc_tts_emit_pcm(struct vc_tts_sink *s, const char *b64, size_t len)
{
  uint8_t pcm[VC_PCM_CHUNK];
  int n;

  n = vg_b64_decode(b64, len, pcm, sizeof(pcm));
  if (n < 0)
    {
      syslog(LOG_ERR, "[%s] tts: bad base64 chunk of %u bytes\n", TAG,
             (unsigned)len);
      return -1;
    }

  if (n == 0)
    {
      return 0;
    }

  if (write(s->fd, pcm, (size_t)n) != n)
    {
      return -1;
    }

  s->pcm_bytes += (size_t)n;
  return 0;
}

static int vc_tts_line(struct vc_tts_sink *s)
{
  char *payload;
  int rc;

  if (s->line_len == 0)
    {
      return 0;
    }

  s->line[s->line_len] = '\0';

  payload = malloc(VC_SSE_LINE_MAX);
  if (payload == NULL)
    {
      return -1;
    }

  rc = vg_sse_line_data(s->line, s->line_len, payload, VC_SSE_LINE_MAX);
  s->line_len = 0;

  if (rc == -1)
    {
      s->done = true;
      free(payload);
      return 0;
    }

  if (rc <= 0)
    {
      free(payload);
      return (rc == -2) ? -1 : 0;
    }

  {
    cJSON *root = cJSON_Parse(payload);
    cJSON *choices;
    cJSON *first;
    cJSON *delta;
    cJSON *audio;
    cJSON *data;

    free(payload);

    if (root == NULL)
      {
        /* An SSE keep-alive or an event we do not use is not an error. */

        return 0;
      }

    choices = cJSON_GetObjectItem(root, "choices");
    first = cJSON_IsArray(choices) ? cJSON_GetArrayItem(choices, 0) : NULL;
    delta = (first != NULL) ? cJSON_GetObjectItem(first, "delta") : NULL;
    audio = (delta != NULL) ? cJSON_GetObjectItem(delta, "audio") : NULL;
    data = (audio != NULL) ? cJSON_GetObjectItem(audio, "data") : NULL;

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

          continue;
        }

      if (s->line_len + 1 >= s->line_cap)
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

  return 0;
}

enum vg_vc_result vg_voice_cloud_tts(const char *text, const char *wav_out,
                                     const vela_req_ctl_t *ctl,
                                     struct vg_vc_info *info)
{
  struct vg_vc_info local;
  struct vc_tts_sink sink;
  struct stat st;
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
  sink.line_cap = VC_SSE_LINE_MAX;
  sink.line = malloc(sink.line_cap);
  if (sink.line == NULL)
    {
      free(body);
      return VG_VC_IO;
    }

  sink.fd = open(VG_VC_TTS_PCM_PATH, O_CREAT | O_TRUNC | O_WRONLY, 0600);
  if (sink.fd < 0)
    {
      free(sink.line);
      free(body);
      return VG_VC_IO;
    }

  {
    vela_resp_sink_t rs;

    rs.write = vc_tts_write;
    rs.ctx = &sink;

    ret = agent_http_post_stream(body, NULL, &rs, resp, sizeof(resp),
                               &body_out, &status, ctl);
  }

  info->status = status;
  info->resp_bytes = (uint32_t)body_out;
  info->req_bytes = (uint32_t)strlen(body);

  if (close(sink.fd) != 0 && ret == 0)
    {
      ret = ERROR;
    }

  sink.fd = -1;
  info->audio_bytes = (uint32_t)sink.pcm_bytes;

  if (ret != 0 || sink.error != 0)
    {
      rc = (ret != 0) ? vc_from_transport(ret) : VG_VC_FORMAT;
      goto out;
    }

  if (status != 200)
    {
      rc = VG_VC_HTTP;
      goto out;
    }

  if (sink.pcm_bytes == 0)
    {
      rc = VG_VC_FORMAT;
      goto out;
    }

  /* Convert 24 kHz mono to the 16 kHz stereo the player was verified with. */

  if (stat(VG_VC_TTS_PCM_PATH, &st) != 0 || st.st_size < 2)
    {
      rc = VG_VC_IO;
      goto out;
    }

  {
    FILE *f = fopen(VG_VC_TTS_PCM_PATH, "rb");
    int16_t *pcm_in;
    int16_t *pcm_out;
    size_t samples = (size_t)st.st_size / 2;
    size_t done = 0;
    size_t out_total = (samples / 3u) * 2u;
    struct vg_pcm_writer w;

    if (f == NULL)
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
        fclose(f);
        rc = VG_VC_IO;
        goto out;
      }

    if (vg_pcm_writer_open(&w, out_path, out_total) != 0)
      {
        free(pcm_in);
        free(pcm_out);
        fclose(f);
        rc = VG_VC_IO;
        goto out;
      }

    while (done < samples)
      {
        size_t batch = samples - done;
        size_t got;
        int out_n;

        /* 15360 is a multiple of 3, so the 3:2 resampler never drops a
         * partial group in the middle of the signal. */

        if (batch > VC_PCM_SAMPLES)
          {
            batch = VC_PCM_SAMPLES;
          }

        got = fread(pcm_in, 2, batch, f);
        if (got == 0)
          {
            break;
          }

        out_n = vg_pcm_resample_24k_16k(pcm_in, got, pcm_out,
                                        VC_PCM_SAMPLES * 2u);
        if (out_n < 0 || vg_pcm_writer_write(&w, pcm_out,
                                             (size_t)out_n) != 0)
          {
            break;
          }

        done += got;
      }

    if (done != samples || vg_pcm_writer_close(&w) != 0)
      {
        free(pcm_in);
        free(pcm_out);
        fclose(f);
        rc = VG_VC_IO;
        goto out;
      }

    free(pcm_in);
    free(pcm_out);
    fclose(f);
  }

  clock_gettime(CLOCK_MONOTONIC, &mono_end);
  info->elapsed_ms =
      (uint32_t)((mono_end.tv_sec - mono_start.tv_sec) * 1000
                 + (mono_end.tv_nsec - mono_start.tv_nsec) / 1000000);
  rc = VG_VC_OK;

out:
  unlink(VG_VC_TTS_PCM_PATH);

  if (rc != VG_VC_OK)
    {
      unlink(out_path);
    }

  free(sink.line);
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
