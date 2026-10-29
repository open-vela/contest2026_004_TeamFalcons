/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __VG_OTA_OUTBOX_H
#define __VG_OTA_OUTBOX_H
#define VG_OTA_OUTBOX_SLOTS 8
struct vg_ota_event {
  char task_id[40];
  char version[32];
  char result[24];
  char error[48];
  char self_check[16];
  char slot;
  unsigned acknowledged;
};
void vg_ota_outbox_set_dir(const char *directory);
int vg_ota_outbox_enqueue(const struct vg_ota_event *event);
int vg_ota_outbox_peek(struct vg_ota_event *event,unsigned *slot);
int vg_ota_outbox_ack(unsigned slot,unsigned bit);
int vg_ota_outbox_space(void);
#endif
