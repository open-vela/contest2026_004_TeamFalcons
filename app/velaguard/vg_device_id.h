/****************************************************************************
 * app/velaguard/vg_device_id.h
 *
 * Stable MQTT / topic identity: vg- + STM32 96-bit UID hex.
 * Compile-time CONFIG_VG_MQTT_DEVICE_ID or DEVID overrides (host / test).
 ****************************************************************************/

#ifndef __VG_DEVICE_ID_H
#define __VG_DEVICE_ID_H

#include <stddef.h>
#include <stdint.h>

#define VG_DEVICE_UID_LEN 12
#define VG_DEVICE_ID_MAX  32

int vg_device_id_read_uid(uint8_t uid[VG_DEVICE_UID_LEN]);

int vg_device_id_from_uid(const uint8_t uid[VG_DEVICE_UID_LEN],
                          char *out, size_t n);

/* Cached. First call reads UID (or applies override). Never returns NULL. */
const char *vg_device_id(void);

#endif
