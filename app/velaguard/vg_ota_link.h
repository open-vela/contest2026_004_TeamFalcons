/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __VG_OTA_LINK_H
#define __VG_OTA_LINK_H
#include <stdint.h>
struct vg_ota_link_info { uint16_t phy,first,second; };
struct vg_ota_link_ports {
  void *ctx;
  int (*address)(void *,uint16_t *);
  int (*read)(void *,uint16_t,uint16_t,uint16_t *);
  void (*delay_ms)(void *,unsigned);
};
int vg_ota_link_check(const struct vg_ota_link_ports *ports,struct vg_ota_link_info *info);
int vg_ota_link_probe(struct vg_ota_link_info *info);
int vg_ota_link_ready(void);
#endif
