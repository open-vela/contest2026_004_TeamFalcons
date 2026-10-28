#include <errno.h>

#include "vg_ota_bcb.h"

#define OFF_MAGIC       0u
#define OFF_FORMAT      4u
#define OFF_HEADER_SIZE 6u
#define OFF_SEQ         8u
#define OFF_STATE       12u
#define OFF_SLOT        16u
#define OFF_IMAGE_SIZE  20u
#define OFF_TRIAL_COUNT 24u
#define OFF_FLAGS       28u
#define OFF_VERSION     32u
#define OFF_TASK_ID     64u
#define OFF_SHA256      104u
#define OFF_CRC32       136u

static uint16_t rd16(const uint8_t *p)
{
  return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t rd32(const uint8_t *p)
{
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
         ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void wr16(uint8_t *p, uint16_t v)
{
  p[0] = (uint8_t)v;
  p[1] = (uint8_t)(v >> 8);
}

static void wr32(uint8_t *p, uint32_t v)
{
  p[0] = (uint8_t)v;
  p[1] = (uint8_t)(v >> 8);
  p[2] = (uint8_t)(v >> 16);
  p[3] = (uint8_t)(v >> 24);
}

static void zero_bytes(uint8_t *p, uint32_t len)
{
  while (len-- > 0)
    {
      *p++ = 0;
    }
}

static void copy_bytes(uint8_t *dst, const uint8_t *src, uint32_t len)
{
  while (len-- > 0)
    {
      *dst++ = *src++;
    }
}

uint32_t vg_ota_crc32(const uint8_t *data, uint32_t len)
{
  uint32_t crc = 0xffffffffu;
  uint32_t i;
  uint32_t bit;

  for (i = 0; i < len; i++)
    {
      crc ^= data[i];
      for (bit = 0; bit < 8; bit++)
        {
          if ((crc & 1u) != 0)
            {
              crc = (crc >> 1) ^ 0xedb88320u;
            }
          else
            {
              crc >>= 1;
            }
        }
    }

  return crc ^ 0xffffffffu;
}

static int string_ok(const uint8_t *field, uint32_t size)
{
  uint32_t i;

  for (i = 0; i < size; i++)
    {
      uint8_t c = field[i];

      if (c == 0)
        {
          return i > 0 ? 0 : -EINVAL;
        }

      if (c < 0x20 || c > 0x7e)
        {
          return -EINVAL;
        }
    }

  return -EINVAL;
}

static int fields_valid(const struct vg_ota_bcb *bcb)
{
  uint32_t i;
  int nonzero = 0;

  if (bcb->state < VG_OTA_BCB_STATE_PENDING ||
      bcb->state > VG_OTA_BCB_STATE_FAILED)
    {
      return -EINVAL;
    }

  if (bcb->slot > VG_OTA_BCB_SLOT_B)
    {
      return -EINVAL;
    }

  if (bcb->image_size == 0 || bcb->image_size > VG_OTA_SLOT_BYTES)
    {
      return -EINVAL;
    }

  if (bcb->trial_count > 0xffffu)
    {
      return -EINVAL;
    }

  if (string_ok((const uint8_t *)bcb->version,
                VG_OTA_BCB_VERSION_BYTES) != 0)
    {
      return -EINVAL;
    }

  if (string_ok((const uint8_t *)bcb->task_id,
                VG_OTA_BCB_TASK_ID_BYTES) != 0)
    {
      return -EINVAL;
    }

  for (i = 0; i < VG_OTA_BCB_SHA256_BYTES; i++)
    {
      if (bcb->sha256[i] != 0)
        {
          nonzero = 1;
        }
    }

  return nonzero ? 0 : -EINVAL;
}

int vg_ota_bcb_decode(const uint8_t *buf, uint32_t len,
                      struct vg_ota_bcb *out)
{
  uint32_t stored_crc;
  uint32_t calc_crc;

  if (buf == 0 || out == 0 || len != VG_OTA_BCB_FILE_BYTES)
    {
      return -EINVAL;
    }

  if (rd32(&buf[OFF_MAGIC]) != VG_OTA_BCB_MAGIC ||
      rd16(&buf[OFF_FORMAT]) != VG_OTA_BCB_FORMAT ||
      rd16(&buf[OFF_HEADER_SIZE]) != VG_OTA_BCB_HEADER_BYTES)
    {
      return -EINVAL;
    }

  stored_crc = rd32(&buf[OFF_CRC32]);
  calc_crc = vg_ota_crc32(buf, OFF_CRC32);
  if (stored_crc != calc_crc)
    {
      return -EBADMSG;
    }

  zero_bytes((uint8_t *)out, sizeof(*out));
  out->seq = rd32(&buf[OFF_SEQ]);
  out->state = rd32(&buf[OFF_STATE]);
  out->slot = rd32(&buf[OFF_SLOT]);
  out->image_size = rd32(&buf[OFF_IMAGE_SIZE]);
  out->trial_count = rd32(&buf[OFF_TRIAL_COUNT]);
  out->flags = rd32(&buf[OFF_FLAGS]);
  copy_bytes((uint8_t *)out->version, &buf[OFF_VERSION],
             VG_OTA_BCB_VERSION_BYTES);
  out->version[VG_OTA_BCB_VERSION_BYTES] = '\0';
  copy_bytes((uint8_t *)out->task_id, &buf[OFF_TASK_ID],
             VG_OTA_BCB_TASK_ID_BYTES);
  out->task_id[VG_OTA_BCB_TASK_ID_BYTES] = '\0';
  copy_bytes(out->sha256, &buf[OFF_SHA256], VG_OTA_BCB_SHA256_BYTES);
  return fields_valid(out);
}

int vg_ota_bcb_encode(const struct vg_ota_bcb *in, uint8_t *buf, uint32_t len)
{
  uint32_t i;
  int ret;

  if (in == 0 || buf == 0 || len != VG_OTA_BCB_FILE_BYTES)
    {
      return -EINVAL;
    }

  ret = fields_valid(in);
  if (ret != 0)
    {
      return ret;
    }

  zero_bytes(buf, len);
  wr32(&buf[OFF_MAGIC], VG_OTA_BCB_MAGIC);
  wr16(&buf[OFF_FORMAT], VG_OTA_BCB_FORMAT);
  wr16(&buf[OFF_HEADER_SIZE], VG_OTA_BCB_HEADER_BYTES);
  wr32(&buf[OFF_SEQ], in->seq);
  wr32(&buf[OFF_STATE], in->state);
  wr32(&buf[OFF_SLOT], in->slot);
  wr32(&buf[OFF_IMAGE_SIZE], in->image_size);
  wr32(&buf[OFF_TRIAL_COUNT], in->trial_count);
  wr32(&buf[OFF_FLAGS], in->flags);

  for (i = 0; i < VG_OTA_BCB_VERSION_BYTES && in->version[i] != '\0'; i++)
    {
      buf[OFF_VERSION + i] = (uint8_t)in->version[i];
    }

  for (i = 0; i < VG_OTA_BCB_TASK_ID_BYTES && in->task_id[i] != '\0'; i++)
    {
      buf[OFF_TASK_ID + i] = (uint8_t)in->task_id[i];
    }

  copy_bytes(&buf[OFF_SHA256], in->sha256, VG_OTA_BCB_SHA256_BYTES);
  wr32(&buf[OFF_CRC32], vg_ota_crc32(buf, OFF_CRC32));
  return 0;
}

int vg_ota_bcb_seq_newer(uint32_t a, uint32_t b)
{
  return (int32_t)(a - b) > 0;
}

int vg_ota_bcb_select(const uint8_t *buf0, uint32_t len0, int have0,
                      const uint8_t *buf1, uint32_t len1, int have1,
                      struct vg_ota_bcb *out, uint32_t *which,
                      int *valid0, int *valid1)
{
  struct vg_ota_bcb b0;
  struct vg_ota_bcb b1;
  int ok0 = 0;
  int ok1 = 0;

  if (out == 0 || which == 0 || valid0 == 0 || valid1 == 0)
    {
      return -EINVAL;
    }

  if (have0 && buf0 != 0 && len0 == VG_OTA_BCB_FILE_BYTES)
    {
      ok0 = vg_ota_bcb_decode(buf0, len0, &b0) == 0;
    }

  if (have1 && buf1 != 0 && len1 == VG_OTA_BCB_FILE_BYTES)
    {
      ok1 = vg_ota_bcb_decode(buf1, len1, &b1) == 0;
    }

  *valid0 = ok0;
  *valid1 = ok1;

  if (!ok0 && !ok1)
    {
      return -ENOENT;
    }

  if (ok0 && ok1)
    {
      /* Exactly half the serial space has no unambiguous ordering. */
      if ((b1.seq - b0.seq) == 0x80000000u)
        {
          return -EINVAL;
        }

      if (vg_ota_bcb_seq_newer(b1.seq, b0.seq))
        {
          *out = b1;
          *which = 1;
        }
      else
        {
          *out = b0;
          *which = 0;
        }
    }
  else if (ok0)
    {
      *out = b0;
      *which = 0;
    }
  else
    {
      *out = b1;
      *which = 1;
    }

  return 0;
}

const char *vg_ota_bcb_state_name(uint32_t state)
{
  switch (state)
    {
      case VG_OTA_BCB_STATE_PENDING:
        return "pending";
      case VG_OTA_BCB_STATE_TRIAL:
        return "trial";
      case VG_OTA_BCB_STATE_CONFIRMED:
        return "confirmed";
      case VG_OTA_BCB_STATE_FAILED:
        return "failed";
      default:
        return "invalid";
    }
}

const char *vg_ota_bcb_slot_name(uint32_t slot)
{
  return slot == VG_OTA_BCB_SLOT_B ? "B" : "A";
}
