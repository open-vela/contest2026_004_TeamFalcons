/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __VG_OTA_SERVICE_H
#define __VG_OTA_SERVICE_H
#include <stdio.h>
/* 只启动控制/下载服务，不产生本机安装确认。 */
struct vg_ota_service_status { int running,online,active,downloading; char error[64]; };
int vg_ota_service_start(void);
int vg_ota_service_status(struct vg_ota_service_status *out);
void vg_ota_service_print(FILE *out);
int vg_ota_service_egress(void);
#endif
