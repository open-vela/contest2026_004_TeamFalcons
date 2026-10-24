/****************************************************************************
 * app/velaguard/host_tests/stubs/nuttx/audio/audio.h
 *
 * Host-test stand-in for nuttx/include/nuttx/audio/audio.h: only the
 * names vg_tts_player.c touches, with the same shapes.  The board build
 * uses the real header; keep both in sync when the player changes.
 ****************************************************************************/

#ifndef __STUB_NUTTX_AUDIO_AUDIO_H
#define __STUB_NUTTX_AUDIO_AUDIO_H

#include <stdint.h>

#define AUDIOIOC_GETCAPS        1
#define AUDIOIOC_RESERVE        2
#define AUDIOIOC_RELEASE        3
#define AUDIOIOC_CONFIGURE      4
#define AUDIOIOC_SHUTDOWN       5
#define AUDIOIOC_START          6
#define AUDIOIOC_STOP           7
#define AUDIOIOC_PAUSE          8
#define AUDIOIOC_RESUME         9
#define AUDIOIOC_GETBUFFERINFO  10
#define AUDIOIOC_ALLOCBUFFER    11
#define AUDIOIOC_FREEBUFFER     12
#define AUDIOIOC_ENQUEUEBUFFER  13
#define AUDIOIOC_REGISTERMQ     14
#define AUDIOIOC_UNREGISTERMQ   15
#define AUDIOIOC_HWRESET        16
#define AUDIOIOC_SETBUFFERINFO  17
#define AUDIOIOC_SETPARAMTER    18
#define AUDIOIOC_GETLATENCY     19
#define AUDIOIOC_FLUSH          20
#define AUDIOIOC_GETPOSITION    21
#define AUDIOIOC_GETAUDIOINFO   22
#define AUDIOIOC_GETSTATUS      23
#define AUDIOIOC_RESETSTATUS    24
#define AUDIOIOC_DUMP           25

#define AUDIO_TYPE_QUERY        0x01
#define AUDIO_TYPE_OUTPUT       0x02
#define AUDIO_TYPE_INPUT        0x04

#define AUDIO_FMT_PCM           0x06

#define AUDIO_MSG_DEQUEUE       1
#define AUDIO_MSG_COMPLETE      8
#define AUDIO_MSG_STOP          4
#define AUDIO_MSG_ENQUEUE       7

#define AUDIO_APB_OUTPUT_ENQUEUED (1 << 0)
#define AUDIO_APB_OUTPUT_PROCESS  (1 << 1)
#define AUDIO_APB_DEQUEUED        (1 << 2)
#define AUDIO_APB_FINAL           (1 << 3)

typedef uint16_t apb_samp_t;

struct audio_caps_s
{
  uint8_t ac_len;
  uint8_t ac_type;
  uint8_t ac_subtype;
  uint8_t ac_channels;
  uint8_t ac_chmap;
  uint8_t reserved;
  union
  {
    uint8_t b[2];
    uint16_t hw;
  } ac_format;
  union
  {
    uint8_t b[4];
    uint16_t hw[2];
    uint32_t w;
  } ac_controls;
};

struct audio_caps_desc_s
{
  struct audio_caps_s caps;
};

struct ap_buffer_info_s
{
  apb_samp_t nbuffers;
  apb_samp_t buffer_size;
};

struct ap_buffer_s
{
  apb_samp_t nmaxbytes;
  apb_samp_t nbytes;
  apb_samp_t curbyte;
  uint16_t flags;
  uint8_t *samp;
};

struct audio_buf_desc_s
{
  apb_samp_t numbytes;
  union
  {
    struct ap_buffer_s *buffer;
    struct ap_buffer_s **pbuffer;
  } u;
};

struct audio_msg_s
{
  uint16_t msg_id;
  union
  {
    void *ptr;
    uint32_t data;
  } u;
};

#endif /* __STUB_NUTTX_AUDIO_AUDIO_H */
