/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __VG_OTA_PROTOCOL_H
#define __VG_OTA_PROTOCOL_H
#include "vg_ota_download.h"
#define VG_OTA_PAYLOAD_MAX 1536
#define VG_OTA_TOPIC_MAX 96
int vg_ota_offer_parse(const char *topic,const void *payload,size_t length,
                       const char *device_id,int secure,int egress,int retain,int qos,
                       struct vg_ota_offer *offer,char *error,size_t error_size);
int vg_ota_status_json(char *out,size_t capacity,const char *device_id,
                       const char *task_id,const char *version,const char *state,
                       const char *slot,uint32_t bytes,uint32_t size,const char *error);
int vg_ota_result_json(char *out,size_t capacity,const char *device_id,
                       const char *task_id,const char *version,const char *result,
                       const char *slot,const char *error,const char *self_check);
int vg_ota_identifier(const char *text,size_t limit);
#endif
