#include <errno.h>
#include <string.h>
#include "vg_ota_install.h"
#include "vg_ota_sha256.h"

static uint32_t r32(const uint8_t *p)
{
  return (uint32_t)p[0] | ((uint32_t)p[1]<<8) |
         ((uint32_t)p[2]<<16) | ((uint32_t)p[3]<<24);
}

int vg_ota_image_vector_valid(const uint8_t v[8], uint32_t size)
{
  uint32_t sp=r32(v), pc=r32(v+4), addr=pc&~1u;
  int ram=(sp>0x20000000u && sp<=0x20020000u) ||
          (sp>0x24000000u && sp<=0x24080000u) ||
          (sp>0x30000000u && sp<=0x30048000u) ||
          (sp>0x38000000u && sp<=0x38010000u);
  return size>=8 && size<=VG_OTA_IMAGE_MAX_BYTES && ram && !(sp&3u) &&
         (pc&1u) && addr>=VG_OTA_SLOT_B_BASE &&
         addr-VG_OTA_SLOT_B_BASE<size;
}

static void stage(const struct vg_ota_install_ports *p,
                    struct vg_ota_boot_decision *d, uint32_t s, uint32_t n)
{
  d->install_stage=s; d->install_bytes=n;
  if (p->progress) p->progress(p->ctx,s,n);
}

/* 只更新已有 BCB 的首扇区，CRC 覆盖的头部完全位于该扇区。
 * 每次都同步并回读；另一份有效 FAILED 在 trial 提交期间保持不动。
 */
static int store_bcb(const struct vg_ota_install_ports *p,
                      struct vg_ota_fat_volume *v,
                      const struct vg_ota_fat_entry *e,
                      const struct vg_ota_bcb *b)
{
  uint8_t buf[4096], check[512];
  uint32_t sector;
  if (vg_ota_bcb_encode(b,buf,sizeof(buf)) ||
      vg_ota_fat_file_sector(v,e,0,&sector) ||
      p->write_sector(p->ctx,sector,buf) || p->sync(p->ctx) ||
      p->read(p->ctx,sector,check,1) || memcmp(buf,check,512)) return -EIO;
  return 0;
}

static int hash_image(const struct vg_ota_install_ports *p,
                       struct vg_ota_fat_volume *v,
                       const struct vg_ota_fat_entry *e, uint8_t digest[32],
                       int from_flash)
{
  struct vg_ota_sha256 sha;
  uint8_t buf[4096];
  uint32_t off, n;
  vg_ota_sha256_init(&sha);
  for (off=0; off<e->size; off+=n)
    {
      n=e->size-off; if (n>sizeof(buf)) n=sizeof(buf);
      if (from_flash ? p->read_flash(p->ctx,VG_OTA_SLOT_B_BASE+off,buf,n) :
          vg_ota_fat_read(v,e,off,buf,n)) return -EIO;
      vg_ota_sha256_update(&sha,buf,n);
    }
  vg_ota_sha256_final(&sha,digest);
  return 0;
}

static int install(const struct vg_ota_install_ports *p,
                     struct vg_ota_boot_decision *d)
{
  static const char names[2][11]={"BCB0    BIN","BCB1    BIN"};
  struct vg_ota_fat_volume vol;
  struct vg_ota_fat_entry dir, entries[2], image, manifest;
  struct vg_ota_manifest m;
  struct vg_ota_bcb b, changed;
  struct vg_ota_sha256 sha;
  uint8_t blocks[2][4096], buf[4096], digest[32], other_digest[32];
  uint32_t which, off, n, i, lba[2], erase_len;
  int valid0, valid1;

  stage(p,d,VG_OTA_INSTALL_METADATA,0);
  if (vg_ota_fat_mount(&vol,p->read,p->ctx,p->sectors) ||
      vg_ota_fat_find(&vol,"VGOTA      ",0,&dir) || !(dir.attr&0x10))
    return -EINVAL;
  for (i=0; i<2; i++)
    {
      if (vg_ota_fat_find(&vol,names[i],dir.first_cluster,&entries[i]) ||
          entries[i].attr&0x10 || entries[i].size!=4096 ||
          vg_ota_fat_read(&vol,&entries[i],0,blocks[i],4096) ||
          vg_ota_fat_file_sector(&vol,&entries[i],0,&lba[i])) return -EINVAL;
    }
  if (lba[0]==lba[1] ||
      vg_ota_bcb_select(blocks[0],4096,1,blocks[1],4096,1,&b,&which,
                         &valid0,&valid1) ||
      b.seq!=d->bcb_seq || which!=d->bcb_which ||
      b.state!=VG_OTA_BCB_STATE_PENDING) return -EINVAL;
  if (vg_ota_fat_find(&vol,"MANIFESTBIN",dir.first_cluster,&manifest) ||
      manifest.attr&0x10 || manifest.size!=512 ||
      vg_ota_fat_read(&vol,&manifest,0,buf,512) ||
      vg_ota_manifest_decode(buf,512,&m) ||
      vg_ota_manifest_bind(&m,&b,p->device_id,p->allow_fixture)) return -EINVAL;
  if (vg_ota_fat_find(&vol,"IMAGE   BIN",dir.first_cluster,&image) ||
      image.attr&0x10 || image.size!=m.image_size ||
      vg_ota_fat_read(&vol,&image,0,buf,8) ||
      !vg_ota_image_vector_valid(buf,image.size)) return -EINVAL;

  /* BCB 首扇区不能与镜像、manifest、目录或另一份 BCB 的任何簇重合。 */
  for (i=0; i<2; i++)
    {
      const struct vg_ota_fat_entry *files[3]={&image,&manifest,&entries[1-i]};
      uint32_t j, step=vol.info.sectors_per_cluster*512u;
      if (entries[i].first_cluster==dir.first_cluster ||
          entries[i].first_cluster==vol.info.root_cluster) return -EINVAL;
      for (j=0; j<3; j++)
        for (off=0; off<files[j]->size; off+=step)
          {
            uint32_t sector;
            if (vg_ota_fat_file_sector(&vol,files[j],off,&sector) ||
                sector==lba[i]) return -EINVAL;
          }
    }
  stage(p,d,VG_OTA_INSTALL_IMAGE,0);
  if (hash_image(p,&vol,&image,digest,0) || memcmp(digest,m.sha256,32))
    return -EBADMSG;

  /* 先持久化更高序号 FAILED，再允许任何 QSPI 擦写。断电后不自动重装。
   * 最终 TRIAL 写回原 pending 文件；撕裂写入时仍有另一份 FAILED 可选。
   */
  stage(p,d,VG_OTA_INSTALL_FENCE,0);
  changed=b; changed.seq=b.seq+1u; changed.state=VG_OTA_BCB_STATE_FAILED;
  if (store_bcb(p,&vol,&entries[1-which],&changed)) return -EIO;
  stage(p,d,VG_OTA_INSTALL_ERASE,0);
  erase_len=(image.size+VG_OTA_ERASE_BYTES-1u)&~(VG_OTA_ERASE_BYTES-1u);
  if (erase_len>VG_OTA_SLOT_BYTES) return -EINVAL;
  for (off=0; off<erase_len; off+=VG_OTA_ERASE_BYTES)
    {
      stage(p,d,VG_OTA_INSTALL_ERASE,off);
      if (p->erase(p->ctx,VG_OTA_SLOT_B_BASE+off,VG_OTA_ERASE_BYTES))
        return -EIO;
    }
  vg_ota_sha256_init(&sha);
  for (off=0; off<image.size; off+=n)
    {
      n=image.size-off; if (n>sizeof(buf)) n=sizeof(buf);
      stage(p,d,VG_OTA_INSTALL_PROGRAM,off);
      if (vg_ota_fat_read(&vol,&image,off,buf,n)) return -EIO;
      vg_ota_sha256_update(&sha,buf,n);
      for (i=0; i<n; i+=VG_OTA_PROGRAM_BYTES)
        {
          uint32_t count=n-i;
          if (count>VG_OTA_PROGRAM_BYTES) count=VG_OTA_PROGRAM_BYTES;
          if (p->program(p->ctx,VG_OTA_SLOT_B_BASE+off+i,buf+i,count))
            return -EIO;
        }
    }
  vg_ota_sha256_final(&sha,other_digest);
  if (memcmp(digest,other_digest,32)) return -EBADMSG;
  stage(p,d,VG_OTA_INSTALL_READBACK,image.size);
  if (hash_image(p,&vol,&image,other_digest,1) || memcmp(digest,other_digest,32))
    return -EBADMSG;
  stage(p,d,VG_OTA_INSTALL_TRIAL,image.size);
  changed=b; changed.seq=b.seq+2u; changed.state=VG_OTA_BCB_STATE_TRIAL;
  changed.trial_count=1;
  if (store_bcb(p,&vol,&entries[which],&changed)) return -EIO;
  d->slot=VG_OTA_BCB_SLOT_B; d->reason=VG_OTA_BOOT_REASON_B_INSTALLED;
  d->bcb_seq=changed.seq; d->bcb_state=changed.state;
  d->bcb_valid0=1; d->bcb_valid1=1;
  stage(p,d,VG_OTA_INSTALL_DONE,image.size);
  return 0;
}

int vg_ota_install_pending(const struct vg_ota_install_ports *p,
                            struct vg_ota_boot_decision *d)
{
  int ret;
  if (!p || !d) return -EINVAL;
  d->slot=VG_OTA_BCB_SLOT_A;
  if (!p->read || !p->write_sector || !p->sync || !p->erase ||
      !p->program || !p->read_flash || !p->device_id ||
      d->reason!=VG_OTA_BOOT_REASON_A_PENDING) ret=-EINVAL;
  else ret=install(p,d);
  if (ret)
    {
      d->reason=VG_OTA_BOOT_REASON_A_INSTALL_FAILED;
      d->install_error=(uint32_t)-ret;
    }
  return ret;
}
