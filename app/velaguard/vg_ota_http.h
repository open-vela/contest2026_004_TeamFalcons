#ifndef __VG_OTA_HTTP_H
#define __VG_OTA_HTTP_H
#include <stddef.h>
#include <stdint.h>
#define VG_OTA_HOST_MAX 96
#define VG_OTA_URL_MAX 384
#define VG_OTA_ETAG_MAX 96
#define VG_OTA_HTTP_HEADER_MAX 2048
struct vg_ota_url {
  char host[VG_OTA_HOST_MAX];
  char port[6];
  char target[VG_OTA_URL_MAX];
};
struct vg_ota_http_response {
  unsigned status;
  uint64_t length;
  uint64_t range_start;
  uint64_t range_end;
  uint64_t range_total;
  int has_range;
  char etag[VG_OTA_ETAG_MAX];
};
int vg_ota_url_parse(const char *url, struct vg_ota_url *out);
int vg_ota_url_allowed(const struct vg_ota_url *url, const char *const *hosts, size_t count);
int vg_ota_http_parse_response(const char *header, struct vg_ota_http_response *out);
int vg_ota_http_request(const struct vg_ota_url *url, uint32_t offset, const char *etag,
                         char *out, size_t capacity);
int vg_ota_etag_strong(const char *etag);
#endif
