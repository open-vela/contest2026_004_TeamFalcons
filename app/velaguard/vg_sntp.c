/****************************************************************************
 * app/velaguard/vg_sntp.c
 *
 * SNTPv4（RFC 4330）报文纯函数。无 NuttX/POSIX 依赖，可 host 测试。
 ****************************************************************************/

#include "vg_sntp.h"

#include <string.h>

#define VG_SNTP_LI_ALARM       3
#define VG_SNTP_MODE_CLIENT    3
#define VG_SNTP_MODE_SERVER    4
#define VG_SNTP_MODE_BCAST     5

#define VG_SNTP_POS_TX_SEC     32

static uint32_t vg_be32(const uint8_t *p)
{
  return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
         ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

int vg_sntp_build_query(uint8_t buf[48])
{
  if (buf == NULL)
    {
      return -1;
    }

  memset(buf, 0, 48);
  /* LI=0, VN=4, Mode=3(client) */
  buf[0] = (uint8_t)((0u << 6) | (4u << 3) | VG_SNTP_MODE_CLIENT);
  return 0;
}

int vg_sntp_parse_response(const uint8_t buf[48], int64_t *utc_out)
{
  uint8_t li;
  uint8_t mode;
  uint32_t tx_sec;

  if (buf == NULL || utc_out == NULL)
    {
      return -1;
    }

  li = (uint8_t)(buf[0] >> 6);
  mode = (uint8_t)(buf[0] & 0x07);

  if (li == VG_SNTP_LI_ALARM)
    {
      return -1;
    }

  if (mode != VG_SNTP_MODE_SERVER && mode != VG_SNTP_MODE_BCAST)
    {
      return -1;
    }

  tx_sec = vg_be32(&buf[VG_SNTP_POS_TX_SEC]);
  if (tx_sec <= VG_SNTP_ERA_OFFSET)
    {
      return -1;
    }

  *utc_out = (int64_t)tx_sec - (int64_t)VG_SNTP_ERA_OFFSET;
  return 0;
}

int64_t vg_time_utc_to_wall(int64_t utc_sec)
{
  return utc_sec + VG_TIME_TZ_OFFSET_SEC;
}

bool vg_time_wall_plausible(int64_t wall_sec)
{
  return wall_sec >= VG_TIME_WALL_MIN && wall_sec < VG_TIME_WALL_MAX;
}
