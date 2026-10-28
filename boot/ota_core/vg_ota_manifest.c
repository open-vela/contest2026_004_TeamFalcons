#include <errno.h>
#include <string.h>
#include "vg_ota_manifest.h"

static uint32_t r32(const uint8_t *p)
{
  return (uint32_t)p[0] | ((uint32_t)p[1]<<8) |
         ((uint32_t)p[2]<<16) | ((uint32_t)p[3]<<24);
}
static void w32(uint8_t *p, uint32_t v)
{
  p[0]=(uint8_t)v; p[1]=(uint8_t)(v>>8);
  p[2]=(uint8_t)(v>>16); p[3]=(uint8_t)(v>>24);
}
static int text_ok(const char *s, uint32_t n)
{
  uint32_t i;
  for (i=0; i<n; i++)
    {
      if (!s[i]) return i != 0;
      if ((uint8_t)s[i]<0x20 || (uint8_t)s[i]>0x7e) return 0;
    }
  return 0;
}
static int valid(const struct vg_ota_manifest *m)
{
  return m->slot==VG_OTA_BCB_SLOT_B && m->image_size>=8 &&
    m->image_size<=VG_OTA_IMAGE_MAX_BYTES &&
    text_ok(m->version,32) && text_ok(m->task_id,40) &&
    text_ok(m->device_id,32) && text_ok(m->model,32) &&
    text_ok(m->key_id,16) && m->expires_at>m->issued_at &&
    (m->flags & ~VG_OTA_MANIFEST_FIXTURE)==0;
}
int vg_ota_manifest_decode(const uint8_t *p, uint32_t size,
                            struct vg_ota_manifest *m)
{
  uint32_t i;
  if (!p || !m || size!=512 || r32(p)!=0x464d4756u ||
      p[4]!=1 || p[5]!=0 || p[6]!=0 || p[7]!=2) return -EINVAL;
  if (vg_ota_crc32(p,508)!=r32(p+508)) return -EBADMSG;
  for (i=284; i<508; i++) if (p[i]) return -EINVAL;
  memset(m,0,sizeof(*m));
  m->slot=r32(p+8); m->image_size=r32(p+12);
  memcpy(m->version,p+16,32); memcpy(m->task_id,p+48,40);
  memcpy(m->sha256,p+88,32); memcpy(m->device_id,p+120,32);
  memcpy(m->model,p+152,32);
  m->issued_at=(uint64_t)r32(p+184)|((uint64_t)r32(p+188)<<32);
  m->expires_at=(uint64_t)r32(p+192)|((uint64_t)r32(p+196)<<32);
  memcpy(m->key_id,p+200,16); memcpy(m->signature,p+220,64);
  m->flags=r32(p+216);
  return valid(m) ? 0 : -EINVAL;
}
int vg_ota_manifest_encode(const struct vg_ota_manifest *m, uint8_t p[512])
{
  if (!m || !p || !valid(m)) return -EINVAL;
  memset(p,0,512);
  w32(p,0x464d4756u); p[4]=1; p[7]=2;
  w32(p+8,m->slot); w32(p+12,m->image_size);
  memcpy(p+16,m->version,32); memcpy(p+48,m->task_id,40);
  memcpy(p+88,m->sha256,32); memcpy(p+120,m->device_id,32);
  memcpy(p+152,m->model,32);
  w32(p+184,(uint32_t)m->issued_at); w32(p+188,(uint32_t)(m->issued_at>>32));
  w32(p+192,(uint32_t)m->expires_at); w32(p+196,(uint32_t)(m->expires_at>>32));
  memcpy(p+200,m->key_id,16); w32(p+216,m->flags);
  memcpy(p+220,m->signature,64); w32(p+508,vg_ota_crc32(p,508));
  return 0;
}
int vg_ota_manifest_bind(const struct vg_ota_manifest *m,
                          const struct vg_ota_bcb *b, const char *id,
                          int allow_fixture)
{
  uint32_t i, nonzero=0;
  if (!m || !b || !id || !valid(m)) return -EINVAL;
  if ((m->flags & VG_OTA_MANIFEST_FIXTURE) && !allow_fixture) return -EPERM;
  for (i=0; i<64; i++) nonzero |= m->signature[i];
  if (!(m->flags & VG_OTA_MANIFEST_FIXTURE) && !nonzero) return -EPERM;
  if (b->flags!=VG_OTA_BCB_LOCAL_CONFIRMED || b->slot!=m->slot ||
      b->image_size!=m->image_size || strcmp(b->version,m->version) ||
      strcmp(b->task_id,m->task_id) || memcmp(b->sha256,m->sha256,32) ||
      strcmp(id,m->device_id) || strcmp(m->model,VG_OTA_MODEL)) return -EINVAL;
  return 0;
}
