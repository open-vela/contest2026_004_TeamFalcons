/* SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include "vg_voice_cloud.h"
#include "vg_voice_pcm.h"
#include <netutils/cJSON.h>

static int mode;
static volatile int cancelled;
static uint32_t now_ms(void)
{
  struct timespec t;
  clock_gettime(CLOCK_MONOTONIC, &t);
  return (uint32_t)((uint64_t)t.tv_sec * 1000 + t.tv_nsec / 1000000);
}

/* Test the real adapter without credentials, DNS changes or cloud quotas. */
static int skip_timing;
static uint8_t stream_cap[65536];
static size_t stream_n;
static int stream_aborted;   /* the consumer sees the abort path via CANCELLED */

static int stream_push(void *ctx, const uint8_t *pcm, size_t bytes)
{
  (void)ctx;
  assert(stream_n + bytes <= sizeof(stream_cap));
  memcpy(stream_cap + stream_n, pcm, bytes);
  stream_n += bytes;
  return 0;
}
int agent_http_post_stream(const char *post, const vela_body_src_t *body,
    const vela_resp_sink_t *sink, char *resp, size_t cap, size_t *out,
    int *status, const vela_req_ctl_t *ctl)
{
  return agent_http_post_stream_timed(post, body, sink, resp, cap, out,
      status, NULL, ctl);
}

int agent_http_post_stream_timed(const char *post, const vela_body_src_t *body,
    const vela_resp_sink_t *sink, char *resp, size_t cap, size_t *out,
    int *status, agent_llm_timing_t *timing, const vela_req_ctl_t *ctl)
{
  (void)post;
  (void)ctl;
  if (timing != NULL && !skip_timing)
    {
      /* Mirror what the transport records now, so the sink's send-done
       * anchor runs against a populated struct; skip_timing leaves it
       * as vc_timing_init prepared it and exercises the fallback
       * anchor instead. */
      timing->send_ms = 1;
      timing->send_done_mono = now_ms();
      timing->ttfb_ms = 2;
      timing->body_ms = 3;
      timing->http_total_ms = 7;
      timing->streamed = true;
    }
  *out = 0;
  *status = 200;
  if (mode == 5) return AGENT_HTTP_ERR_TIMEOUT;
  if (mode == 6) { *status = 429; return 0; }
  if (body)
    {
      char request[2048], part[17];
      size_t n = 0;
      int got;
      while ((got = body->read(body->ctx, part, sizeof(part))) > 0)
        {
          assert(n + got < sizeof(request));
          memcpy(request + n, part, got);
          n += got;
        }
      assert(got == 0 && n == body->len);
      request[n] = 0;
      cJSON *root = cJSON_Parse(request);
      assert(root);
      cJSON *m = cJSON_GetArrayItem(cJSON_GetObjectItem(root, "messages"), 0);
      cJSON *c = cJSON_GetArrayItem(cJSON_GetObjectItem(m, "content"), 0);
      const char *s = cJSON_GetObjectItem(cJSON_GetObjectItem(c, "input_audio"), "data")->valuestring;
      s = strchr(s, ',') + 1;
      unsigned char wav[128];
      int bytes = vg_b64_decode(s, strlen(s), wav, sizeof(wav));
      assert(bytes == 56 && !memcmp(wav, "RIFF", 4));
      assert(wav[22] == 1 && wav[40] == 12);
      for (int i = 0; i < 6; i++) assert(wav[44 + i * 2] == i && wav[45 + i * 2] == 0);
      cJSON_Delete(root);
      snprintf(resp, cap, "{\"choices\":[{\"message\":{\"content\":\"%s\"}}]}",
               mode == 7 ? "" : "UPS负载是多少");
      return 0;
    }
  const char *audio = mode == 4 ? "AA==" : "AAABAAIAAwAEAAUA";
  char line[256];
  snprintf(line, sizeof(line), "data: {\"choices\":[{\"delta\":{\"audio\":{\"data\":\"%s\"}}}]}\n\n", audio);
  if (mode == 2) snprintf(line, sizeof(line), "data: {broken\n\n");
  size_t len = strlen(line);
  if (mode == 3) len -= 2; /* unfinished final SSE line */
  for (size_t i = 0; i < len; i++)
    if (sink->write(sink->ctx, line + i, 1) < 0) return AGENT_HTTP_ERR_SINK;
  if (mode != 1 && mode != 3)
    if (sink->write(sink->ctx, "data: [DONE]\n\n", 14) < 0) return AGENT_HTTP_ERR_SINK;
  if (mode == 8) cancelled = 1;
  return 0;
}

int main(void)
{
  int16_t pcm[] = {0, 1, 2, 3, 4, 5};
  char text[128], path[128];
  struct vg_vc_info info;
  vela_req_ctl_t ctl = { .deadline_ms = now_ms() + 5000, .cancel = &cancelled };
  assert(vg_voice_cloud_asr_buffer(pcm, sizeof(pcm), &ctl, text, sizeof(text), &info) == VG_VC_OK);
  assert(!strcmp(text, "UPS负载是多少"));
  assert(vg_voice_cloud_asr_buffer(pcm, 11, &ctl, text, sizeof(text), &info) == VG_VC_IO);
  mode = 7;
  assert(vg_voice_cloud_asr_buffer(pcm, sizeof(pcm), &ctl, text, sizeof(text), &info) == VG_VC_EMPTY);
  snprintf(path, sizeof(path), "/tmp/vg-cloud-%ld.wav", (long)getpid());
  for (mode = 0; mode <= 8; mode++)
    {
      cancelled = 0;
      ctl.deadline_ms = now_ms() + 5000;
      enum vg_vc_result r = vg_voice_cloud_tts("测试", path, &ctl, &info);
      if (mode == 0 || mode == 7)
        {
          struct vg_pcm_wav wav;
          assert(r == VG_VC_OK && vg_pcm_wav_probe(path, &wav) == 0);
          assert(wav.rate == 16000 && wav.channels == 2 && wav.data_bytes == 16);
          unlink(path);
        }
      else
        {
          assert(r != VG_VC_OK && access(path, F_OK) != 0);
          if (mode == 2) assert(r == VG_VC_FORMAT);
          if (mode == 5) assert(r == VG_VC_TIMEOUT);
          if (mode == 6) assert(r == VG_VC_HTTP && info.status == 429);
          if (mode == 8) assert(r == VG_VC_CANCELLED);
        }
    }
  /* Anchor fallback: when the transport never produced a send-done
   * instant, the block observation must still run off its pre-post
   * anchor and the round must complete. */
  mode = 0;
  skip_timing = 1;
  cancelled = 0;
  ctl.deadline_ms = now_ms() + 5000;
  assert(vg_voice_cloud_tts("测试", path, &ctl, &info) == VG_VC_OK);
  unlink(path);

  /* Streaming variant: the same fake transport drives the consumer in
   * place of the WAV file.  The 24 kHz mono pieces must arrive whole and
   * in order, truncation must be a distinct failure with partial set,
   * and cancellation must not report success. */

  {
    static const uint8_t pcm12[12] =
      { 0, 0, 1, 0, 2, 0, 3, 0, 4, 0, 5, 0 };

    for (mode = 0; mode <= 8; mode++)
      {
        enum vg_vc_result r;

        cancelled = 0;
        ctl.deadline_ms = now_ms() + 5000;
        stream_n = 0;
        stream_aborted = 0;
        r = vg_voice_cloud_tts_stream("测试", stream_push, NULL, &ctl,
                                      &info);
        if (mode == 0 || mode == 7)
          {
            assert(r == VG_VC_OK);
            assert(info.streamed == 1 && info.partial == 0);
            assert(stream_n == 12);
            assert(memcmp(stream_cap, pcm12, 12) == 0);
          }
        else
          {
            assert(r != VG_VC_OK);
            if (mode == 1)
              {
                /* audio delivered, then the stream ended without [DONE] */

                assert(r == VG_VC_TRUNCATED);
                assert(info.partial == 1);
                assert(stream_n == 12);
              }
            if (mode == 3)
              {
                /* the final SSE line lost its newline, so it was never
                 * parsed: truncated with nothing delivered */

                assert(r == VG_VC_TRUNCATED);
                assert(info.partial == 0);
                assert(stream_n == 0);
              }
            if (mode == 2) assert(r == VG_VC_FORMAT && info.partial == 0);
            if (mode == 4) assert(r == VG_VC_FORMAT && info.partial == 1);
            if (mode == 5) assert(r == VG_VC_TIMEOUT);
            if (mode == 6) assert(r == VG_VC_HTTP && info.status == 429);
            if (mode == 8) assert(r == VG_VC_CANCELLED);
          }
      }

    /* The anchor fallback applies to the streaming path as well. */

    mode = 0;
    skip_timing = 1;
    cancelled = 0;
    ctl.deadline_ms = now_ms() + 5000;
    stream_n = 0;
    assert(vg_voice_cloud_tts_stream("测试", stream_push, NULL, &ctl,
                                     &info) == VG_VC_OK);
    assert(stream_n == 12);
    skip_timing = 0;
  }

  puts("test_voice_cloud: OK (streamed WAV body, empty ASR, split SSE, completion, malformed/truncated PCM, 429, timeout, cancellation cleanup, timing anchors, streaming consumer, truncated/partial classification)");
  return 0;
}
