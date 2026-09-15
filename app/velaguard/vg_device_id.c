/****************************************************************************
 * app/velaguard/vg_device_id.c
 *
 * device_id / MQTT client_id: vg- + 24 lowercase hex of the 96-bit UID.
 ****************************************************************************/

#include "vg_device_id.h"

#ifdef __NuttX__
#  include <nuttx/config.h>
#endif

#include <stdio.h>
#include <string.h>

int vg_device_id_read_uid(uint8_t uid[VG_DEVICE_UID_LEN])
{
  if (uid == NULL)
    {
      return -1;
    }

#ifdef __NuttX__
  {
    const volatile uint8_t *src = (const volatile uint8_t *)0x1ff1e800;
    int i;

    for (i = 0; i < VG_DEVICE_UID_LEN; i++)
      {
        uid[i] = src[i];
      }
  }
#else
  memset(uid, 0xab, VG_DEVICE_UID_LEN);
#endif
  return 0;
}

int vg_device_id_from_uid(const uint8_t uid[VG_DEVICE_UID_LEN],
                          char *out, size_t n)
{
  int i;
  int off;

  if (uid == NULL || out == NULL || n < 28)
    {
      return -1;
    }

  off = snprintf(out, n, "vg-");
  if (off < 0 || (size_t)off >= n)
    {
      return -1;
    }

  for (i = 0; i < VG_DEVICE_UID_LEN; i++)
    {
      int w = snprintf(out + off, n - (size_t)off, "%02x", uid[i]);
      if (w != 2)
        {
          return -1;
        }

      off += 2;
    }

  return 0;
}

const char *vg_device_id(void)
{
  static char id[VG_DEVICE_ID_MAX];
  static int ready;
  uint8_t uid[VG_DEVICE_UID_LEN];

  if (ready)
    {
      return id;
    }

#ifdef CONFIG_VG_MQTT_DEVICE_ID
  if (CONFIG_VG_MQTT_DEVICE_ID[0] != '\0')
    {
      snprintf(id, sizeof(id), "%s", CONFIG_VG_MQTT_DEVICE_ID);
      ready = 1;
      return id;
    }
#endif

#ifdef DEVID
  snprintf(id, sizeof(id), "%s", DEVID);
  ready = 1;
  return id;
#else
  if (vg_device_id_read_uid(uid) != 0 ||
      vg_device_id_from_uid(uid, id, sizeof(id)) != 0)
    {
      snprintf(id, sizeof(id), "vg-unknown");
    }

  ready = 1;
  return id;
#endif
}
