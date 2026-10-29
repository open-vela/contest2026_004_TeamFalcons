/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __VG_OTA_MQTT_H
#define __VG_OTA_MQTT_H
#include "vg_ota_config.h"
#include <stddef.h>
#include <sys/types.h>
#define VG_OTA_MQTT_HANDLE 0x20000001
/* 传输层只接收 Offer。没有 install、confirm 或 apply 订阅端口。 */
typedef void (*vg_ota_mqtt_offer_fn)(void *ctx,const char *topic,const void *payload,
                                    size_t length,int retain,int qos);
enum vg_ota_mqtt_kind { VG_OTA_MQTT_STATUS,VG_OTA_MQTT_RESULT,VG_OTA_MQTT_CONFIRM };
int vg_ota_mqtt_open(const struct vg_ota_config *config,const char *device_id,
                      vg_ota_mqtt_offer_fn callback,void *ctx);
int vg_ota_mqtt_poll(void);
int vg_ota_mqtt_publish(enum vg_ota_mqtt_kind kind,const char *json,uint16_t *ticket);
int vg_ota_mqtt_acked(uint16_t ticket);
void vg_ota_mqtt_close(void);
int vg_ota_mqtt_online(void);
int vg_ota_mqtt_pal_send(int fd,const void *bytes,size_t length,ssize_t *out);
int vg_ota_mqtt_pal_recv(int fd,void *bytes,size_t length,ssize_t *out);
#endif
