#ifndef __VG_OTA_HTTPS_H
#define __VG_OTA_HTTPS_H
#include "vg_ota_http.h"
#include <sys/types.h>
/* 一次只持有一个 TLS 连接；CA 只由生产信任束或主机测试提供。 */
struct vg_ota_https { void *session; const char *ca_pem; };
void vg_ota_https_init(struct vg_ota_https *client, const char *ca_pem);
int vg_ota_https_open(void *client, const char *url, uint32_t offset,
                       const char *etag, struct vg_ota_http_response *response);
ssize_t vg_ota_https_read(void *client, uint8_t *buf, size_t length);
void vg_ota_https_close(void *client);
int vg_ota_tls_entropy_ready(void);
/* MQTTS 与 HTTPS 共用严格握手，保持独立连接和协议状态。 */
int vg_ota_tls_connect(struct vg_ota_https *client,const char *host,const char *port);
ssize_t vg_ota_tls_send(struct vg_ota_https *client,const void *bytes,size_t length);
ssize_t vg_ota_tls_receive(struct vg_ota_https *client,void *bytes,size_t length,uint32_t timeout_ms);
#endif
