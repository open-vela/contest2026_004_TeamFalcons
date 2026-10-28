/****************************************************************************
 * Read-only eMMC and FAT readiness helpers for the OTA probe.
 *
 * The parsers have no NuttX dependencies so host_tests can exercise them
 * without a board.  vgota.c supplies the raw registers from /dev/mmcsd0.
 ****************************************************************************/

#include <errno.h>
#include <string.h>

#include "vg_ota_probe.h"

static uint32_t rd32(const uint8_t *p)
{
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
         ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

int vg_ota_parse_cid(const uint32_t cid[4], struct vg_ota_cid_info *out)
{
  int i;
  uint32_t psn;

  if (cid == NULL || out == NULL)
    {
      return -EINVAL;
    }

  memset(out, 0, sizeof(*out));

  for (i = 0; i < 4; i++)
    {
      out->raw[i * 4 + 0] = (uint8_t)(cid[i] >> 24);
      out->raw[i * 4 + 1] = (uint8_t)(cid[i] >> 16);
      out->raw[i * 4 + 2] = (uint8_t)(cid[i] >> 8);
      out->raw[i * 4 + 3] = (uint8_t)cid[i];
    }

  out->mid = (uint8_t)(cid[0] >> 24);
  out->oid = (uint8_t)((cid[0] >> 8) & 0xff);
  out->pnm[0] = (char)(cid[0] & 0xff);
  out->pnm[1] = (char)(cid[1] >> 24);
  out->pnm[2] = (char)(cid[1] >> 16);
  out->pnm[3] = (char)(cid[1] >> 8);
  out->pnm[4] = (char)(cid[1] & 0xff);
  out->pnm[5] = (char)(cid[2] >> 24);
  out->pnm[6] = '\0';

  for (i = 0; i < 6; i++)
    {
      if (out->pnm[i] < 0x20 || out->pnm[i] > 0x7e)
        {
          out->pnm[i] = '.';
        }
    }

  out->prv = (uint8_t)((cid[2] >> 16) & 0xff);
  psn = (cid[2] << 16) | (cid[3] >> 16);
  out->psn = psn;
  out->mdt = (uint8_t)((cid[3] >> 8) & 0xff);
  out->crc = (uint8_t)((cid[3] >> 1) & 0x7f);

  return 0;
}

int vg_ota_parse_csd(const uint32_t csd[4], struct vg_ota_csd_info *out)
{
  uint32_t csize;
  uint32_t csizemult;
  uint64_t blocks;

  if (csd == NULL || out == NULL)
    {
      return -EINVAL;
    }

  memset(out, 0, sizeof(*out));

  out->raw[0]  = (uint8_t)(csd[0] >> 24);
  out->raw[1]  = (uint8_t)(csd[0] >> 16);
  out->raw[2]  = (uint8_t)(csd[0] >> 8);
  out->raw[3]  = (uint8_t)csd[0];
  out->raw[4]  = (uint8_t)(csd[1] >> 24);
  out->raw[5]  = (uint8_t)(csd[1] >> 16);
  out->raw[6]  = (uint8_t)(csd[1] >> 8);
  out->raw[7]  = (uint8_t)csd[1];
  out->raw[8]  = (uint8_t)(csd[2] >> 24);
  out->raw[9]  = (uint8_t)(csd[2] >> 16);
  out->raw[10] = (uint8_t)(csd[2] >> 8);
  out->raw[11] = (uint8_t)csd[2];
  out->raw[12] = (uint8_t)(csd[3] >> 24);
  out->raw[13] = (uint8_t)(csd[3] >> 16);
  out->raw[14] = (uint8_t)(csd[3] >> 8);
  out->raw[15] = (uint8_t)csd[3];

  out->structure   = (uint8_t)(csd[0] >> 30);
  out->spec_vers   = (uint8_t)((csd[0] >> 26) & 0x0f);
  out->read_bl_len = (uint8_t)((csd[1] >> 16) & 0x0f);

  csize = ((csd[1] & 0x03ff) << 2) | ((csd[2] >> 30) & 3);
  csizemult = (csd[2] >> 15) & 7;
  out->csize = (uint16_t)csize;
  out->csize_mult = (uint8_t)csizemult;

  /* eMMC devices above 2 GB use SEC_COUNT in EXT_CSD and set C_SIZE to
   * 0xfff.  In that case the CSD itself does not carry the capacity.
   */

  if (csize == 0x0fff)
    {
      return 0;
    }

  blocks = (uint64_t)(csize + 1) * (1u << (csizemult + 2));
  if (out->read_bl_len > 9)
    {
      blocks <<= (out->read_bl_len - 9);
    }

  out->capacity_bytes = blocks << 9;
  return 0;
}

int vg_ota_parse_extcsd(const uint8_t extcsd[VG_OTA_EXT_CSD_BYTES],
                        struct vg_ota_extcsd_info *out)
{
  if (extcsd == NULL || out == NULL)
    {
      return -EINVAL;
    }

  memset(out, 0, sizeof(*out));
  out->rev = extcsd[192];
  out->sec_count = rd32(&extcsd[212]);
  out->capacity_bytes = (uint64_t)out->sec_count * 512u;
  return 0;
}
