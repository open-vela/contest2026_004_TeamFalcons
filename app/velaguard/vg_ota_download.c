#include "vg_ota_download.h"
#include "vg_ota_staging.h"
#include <errno.h>
#include <stdio.h>
#include <string.h>

struct download {
  const struct vg_ota_offer *offer;
  const struct vg_ota_download_ports *ports;
  uint64_t started;
  uint32_t position;
  unsigned retries;
  int connected;
  int restart;
  char etag[VG_OTA_ETAG_MAX];
  const char *failure;
};
static int check_budget(struct download *d)
{
  int egress=d->ports->egress(d->ports->ctx);
  if (egress!=1) { d->failure=egress==2 ? "wifi_not_supported" : "rj45_unavailable"; return -ENETDOWN; }
  if (d->ports->unix_time(d->ports->ctx)>=d->offer->expires_at) { d->failure="offer_expired"; return -ETIME; }
  if (d->ports->monotonic_ms(d->ports->ctx)-d->started>=VG_OTA_DOWNLOAD_TOTAL_MS) { d->failure="download_timeout"; return -ETIMEDOUT; }
  return 0;
}
static int retry(struct download *d)
{
  if (d->retries>=VG_OTA_DOWNLOAD_RETRIES) { d->failure="download_retries_exhausted"; return -EIO; }
  d->ports->delay_ms(d->ports->ctx,1000u<<d->retries++);
  return check_budget(d);
}
static void close_download(struct download *d)
{
  d->ports->close(d->ports->ctx);
  d->connected=0;
}
static int open_image(struct download *d)
{
  struct vg_ota_http_response response;
  int ret=check_budget(d);
  if (ret) return ret;
  ret=d->ports->open(d->ports->ctx,d->offer->image_url,d->position,
                     d->position ? d->etag : NULL,&response);
  if (ret) {
    d->failure=ret==-EKEYREJECTED ? "tls_verification_failed" : ret==-ENOKEY ? "trust_unconfigured" : ret==-EAGAIN ? "entropy_unverified" : "https_connect_failed";
    return ret;
  }
  d->connected=1;
  d->failure=NULL;
  if (d->position) {
    if (response.status==200 || strcmp(response.etag,d->etag)) { d->restart=1; d->failure="resume_changed"; return -ESTALE; }
    if (response.status!=206 || !response.has_range || response.range_start!=d->position ||
        response.range_total!=d->offer->image_size || response.range_end+1!=d->offer->image_size ||
        response.length!=d->offer->image_size-d->position || !vg_ota_etag_strong(response.etag)) {
      d->failure="invalid_content_range"; return -EPROTO;
    }
  } else {
    if (response.status!=200 || response.length!=d->offer->image_size || response.has_range) { d->failure="image_length_invalid"; return -EPROTO; }
    snprintf(d->etag,sizeof(d->etag),"%s",response.etag);
  }
  return 0;
}
static ssize_t source_read(void *ctx, uint32_t offset, uint8_t *buffer, size_t length)
{
  struct download *d=ctx;
  size_t used=0;
  if (offset!=d->position) return -EINVAL;
  while (used<length) {
    int ret=check_budget(d);
    if (ret) return ret;
    if (!d->connected) {
      ret=open_image(d);
      if (ret) {
        close_download(d);
        if (ret==-ESTALE || ret==-EPROTO || ret==-EKEYREJECTED || ret==-ENOKEY || ret==-EAGAIN) return ret;
        if (retry(d)) return -EIO;
        continue;
      }
    }
    ssize_t got=d->ports->read(d->ports->ctx,buffer+used,length-used);
    if (got<=0) {
      close_download(d);
      if (d->position && !vg_ota_etag_strong(d->etag)) { d->restart=1; d->failure="resume_validator_missing"; return -ESTALE; }
      if (retry(d)) return -EIO;
      continue;
    }
    if ((size_t)got>length-used) return -EPROTO;
    d->failure=NULL;
    used+=(size_t)got;
    d->position+=(uint32_t)got;
  }
  return (ssize_t)used;
}
int vg_ota_download_run(const struct vg_ota_offer *offer,
                        const char *const *hosts, size_t count,
                        const struct vg_ota_download_ports *ports,
                        char *error, size_t error_size)
{
  struct vg_ota_manifest manifest;
  struct vg_ota_url url;
  struct vg_ota_http_response response;
  struct vg_ota_staging_info info;
  struct download d;
  uint8_t wire[VG_OTA_MANIFEST_BYTES];
  size_t used;
  int ret;
  if (!offer || !ports || !ports->open || !ports->read || !ports->close ||
      !ports->monotonic_ms || !ports->unix_time || !ports->egress || !ports->delay_ms) return -EINVAL;
  memset(&d,0,sizeof(d));
  d.offer=offer; d.ports=ports; d.started=ports->monotonic_ms(ports->ctx);
  d.failure="offer_invalid";
  if (error && error_size) error[0]=0;
  if (strnlen(offer->task_id,sizeof(offer->task_id))==sizeof(offer->task_id) || !offer->task_id[0] ||
      strnlen(offer->version,sizeof(offer->version))==sizeof(offer->version) ||
      strnlen(offer->image_url,sizeof(offer->image_url))==sizeof(offer->image_url) ||
      strnlen(offer->manifest_url,sizeof(offer->manifest_url))==sizeof(offer->manifest_url) ||
      offer->image_size<8 || offer->image_size>VG_OTA_IMAGE_MAX_BYTES || ports->unix_time(ports->ctx)<1767225600) { ret=-EINVAL; goto fail; }
  vg_ota_staging_get_info(&info);
  if (info.state==VG_OTA_STATE_READY_AWAITING_CONFIRM || info.state==VG_OTA_STATE_INSTALL_PENDING || info.state==VG_OTA_STATE_REBOOTING) { ret=-EBUSY; d.failure="ota_busy"; goto fail; }
  if (vg_ota_staging_is_suppressed(offer->task_id)) { ret=-EALREADY; d.failure="task_suppressed"; goto fail; }
  if (vg_ota_url_parse(offer->manifest_url,&url) || !vg_ota_url_allowed(&url,hosts,count) ||
      vg_ota_url_parse(offer->image_url,&url) || !vg_ota_url_allowed(&url,hosts,count)) { ret=-EPERM; d.failure="ota_host_forbidden"; goto fail; }
  ret=check_budget(&d);
  if (ret) goto fail;
  /* manifest 必须先验签，再开始写大镜像。 */
  for (;;) {
    ret=ports->open(ports->ctx,offer->manifest_url,0,NULL,&response);
    if (ret) {
      ports->close(ports->ctx);
      d.failure=ret==-EKEYREJECTED ? "tls_verification_failed" : ret==-ENOKEY ? "trust_unconfigured" : ret==-EAGAIN ? "entropy_unverified" : "manifest_download_failed";
      goto fail;
    }
    if (response.status!=200 || response.length!=sizeof(wire)) { ret=-EPROTO; d.failure="manifest_length_invalid"; ports->close(ports->ctx); goto fail; }
    used=0;
    while (used<sizeof(wire)) {
      if ((ret=check_budget(&d))!=0) break;
      ssize_t got=ports->read(ports->ctx,wire+used,sizeof(wire)-used);
      if (got<=0 || (size_t)got>sizeof(wire)-used) { ret=-EIO; break; }
      used+=(size_t)got;
    }
    ports->close(ports->ctx);
    if (ret==0 && used==sizeof(wire)) break;
    if ((ret=retry(&d))!=0) goto fail;
  }
  if (vg_ota_manifest_decode(wire,sizeof(wire),&manifest)!=0 || manifest.flags!=0 ||
      strcmp(manifest.task_id,offer->task_id) || strcmp(manifest.version,offer->version) ||
      manifest.image_size!=offer->image_size || manifest.expires_at!=offer->expires_at ||
      memcmp(manifest.sha256,offer->sha256,32)) { ret=-EBADMSG; d.failure="manifest_offer_mismatch"; goto fail; }
  ret=vg_ota_manifest_validate(&manifest,NULL,error,error_size);
  if (ret) { d.failure=NULL; goto fail; }
  for (;;) {
    d.restart=0; d.position=0; d.etag[0]=0;
    d.failure=NULL;
    ret=vg_ota_staging_stage(&manifest,source_read,&d,error,error_size);
    close_download(&d);
    if (!ret) return 0;
    if (!d.restart || retry(&d)) break;
  }
fail:
  if (error && error_size && d.failure) snprintf(error,error_size,"%s",d.failure);
  return ret<0 ? ret : -EIO;
}
