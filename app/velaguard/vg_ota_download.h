#ifndef __VG_OTA_DOWNLOAD_H
#define __VG_OTA_DOWNLOAD_H
#include "vg_ota_http.h"
#include <sys/types.h>
#define VG_OTA_DOWNLOAD_TOTAL_MS 900000u
#define VG_OTA_DOWNLOAD_RETRIES 3u
struct vg_ota_offer {
  char task_id[40];
  char version[32];
  uint32_t image_size;
  uint8_t sha256[32];
  uint64_t expires_at;
  char manifest_url[VG_OTA_URL_MAX];
  char image_url[VG_OTA_URL_MAX];
};
struct vg_ota_download_ports {
  void *ctx;
  int (*open)(void *,const char *,uint32_t,const char *,struct vg_ota_http_response *);
  ssize_t (*read)(void *,uint8_t *,size_t);
  void (*close)(void *);
  uint64_t (*monotonic_ms)(void *);
  uint64_t (*unix_time)(void *);
  int (*egress)(void *); /* 1=RJ45，2=Wi-Fi，0=不可用 */
  void (*delay_ms)(void *,uint32_t);
};
int vg_ota_download_run(const struct vg_ota_offer *offer,
                        const char *const *allowed_hosts, size_t host_count,
                        const struct vg_ota_download_ports *ports,
                        char *error, size_t error_size);
#endif
