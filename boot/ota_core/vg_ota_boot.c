#include <errno.h>

#include "vg_ota_boot.h"

#define DIR_VGOTA     "VGOTA      "
#define FILE_BCB0     "BCB0    BIN"
#define FILE_BCB1     "BCB1    BIN"

static const char g_name_vgota[VG_OTA_FAT_NAME_BYTES] = DIR_VGOTA;
static const char g_name_bcb0[VG_OTA_FAT_NAME_BYTES] = FILE_BCB0;
static const char g_name_bcb1[VG_OTA_FAT_NAME_BYTES] = FILE_BCB1;

static void zero_decision(struct vg_ota_boot_decision *out)
{
  uint8_t *p = (uint8_t *)out;
  uint32_t i;

  for (i = 0; i < sizeof(*out); i++)
    {
      p[i] = 0;
    }

  out->slot = VG_OTA_BCB_SLOT_A;
  out->bcb_which = 0xffu;
}

static void clear_bytes(uint8_t *p, uint32_t len)
{
  while (len-- > 0)
    {
      *p++ = 0;
    }
}

static int read_bcb(struct vg_ota_fat_volume *vol, uint32_t dir_cluster,
                    const char name[11], uint8_t *buf, int *present,
                    uint32_t *error)
{
  struct vg_ota_fat_entry entry;
  int ret;

  *present = 0;
  ret = vg_ota_fat_find(vol, name, dir_cluster, &entry);
  if (ret == -ENOENT)
    {
      return 0;
    }

  if (ret != 0)
    {
      *error = (uint32_t)(-ret);
      return ret;
    }

  *present = 1;

  if ((entry.attr & 0x10) != 0 || entry.size != VG_OTA_BCB_FILE_BYTES)
    {
      clear_bytes(buf, VG_OTA_BCB_FILE_BYTES);
      *error = (uint32_t)EINVAL;
      return 0;
    }

  ret = vg_ota_fat_read(vol, &entry, 0, buf, VG_OTA_BCB_FILE_BYTES);
  if (ret != 0)
    {
      clear_bytes(buf, VG_OTA_BCB_FILE_BYTES);
      *error = (uint32_t)(-ret);
      return 0;
    }

  return 0;
}

int vg_ota_boot_decide(vg_ota_block_read_fn read, void *ctx,
                       uint32_t device_sectors,
                       struct vg_ota_boot_decision *out)
{
  struct vg_ota_fat_volume vol;
  struct vg_ota_fat_entry dir;
  struct vg_ota_bcb bcb;
  uint8_t bcb0[VG_OTA_BCB_FILE_BYTES];
  uint8_t bcb1[VG_OTA_BCB_FILE_BYTES];
  int have0 = 0;
  int have1 = 0;
  int valid0 = 0;
  int valid1 = 0;
  uint32_t which = 0xffu;
  uint32_t err0 = 0;
  uint32_t err1 = 0;
  uint32_t i;
  int ret;

  if (out == 0 || read == 0)
    {
      return -EINVAL;
    }

  zero_decision(out);

  for (i = 0; i < VG_OTA_BCB_FILE_BYTES; i++)
    {
      bcb0[i] = 0;
      bcb1[i] = 0;
    }

  ret = vg_ota_fat_mount(&vol, read, ctx, device_sectors);
  if (ret != 0)
    {
      out->slot = VG_OTA_BCB_SLOT_A;
      if (ret == -EIO)
        {
          out->reason = VG_OTA_BOOT_REASON_A_EMMC_ERROR;
          out->emmc_error = 1;
        }
      else
        {
          out->reason = VG_OTA_BOOT_REASON_A_FAT_ERROR;
          out->fat_error = (uint32_t)(-ret);
        }

      return 0;
    }

  out->fat_type = (uint32_t)vol.info.type;

  ret = vg_ota_fat_find(&vol, g_name_vgota, 0, &dir);
  if (ret != 0)
    {
      out->slot = VG_OTA_BCB_SLOT_A;
      if (ret == -ENOENT)
        {
          out->reason = VG_OTA_BOOT_REASON_A_NO_STAGING;
        }
      else
        {
          out->reason = VG_OTA_BOOT_REASON_A_READ_ERROR;
          out->bcb_error = (uint32_t)(-ret);
        }

      return 0;
    }

  if ((dir.attr & 0x10) == 0 || dir.first_cluster < 2)
    {
      out->slot = VG_OTA_BCB_SLOT_A;
      out->reason = VG_OTA_BOOT_REASON_A_READ_ERROR;
      out->bcb_error = (uint32_t)ENOTDIR;
      return 0;
    }

  ret = read_bcb(&vol, dir.first_cluster, g_name_bcb0, bcb0,
                 &have0, &err0);
  if (ret != 0 && ret != -ENOENT)
    {
      out->slot = VG_OTA_BCB_SLOT_A;
      out->reason = VG_OTA_BOOT_REASON_A_READ_ERROR;
      out->bcb_error = (uint32_t)(-ret);
      return 0;
    }

  ret = read_bcb(&vol, dir.first_cluster, g_name_bcb1, bcb1,
                 &have1, &err1);
  if (ret != 0 && ret != -ENOENT)
    {
      out->slot = VG_OTA_BCB_SLOT_A;
      out->reason = VG_OTA_BOOT_REASON_A_READ_ERROR;
      out->bcb_error = (uint32_t)(-ret);
      return 0;
    }

  ret = vg_ota_bcb_select(bcb0, VG_OTA_BCB_FILE_BYTES, have0,
                          bcb1, VG_OTA_BCB_FILE_BYTES, have1,
                          &bcb, &which, &valid0, &valid1);

  out->bcb_valid0 = (uint32_t)valid0;
  out->bcb_valid1 = (uint32_t)valid1;

  if (ret != 0)
    {
      out->slot = VG_OTA_BCB_SLOT_A;
      out->reason = (have0 || have1) ? VG_OTA_BOOT_REASON_A_BCB_INVALID :
                                       VG_OTA_BOOT_REASON_A_NO_BCB;
      out->bcb_error = err0 != 0 ? err0 : err1;
      if (out->bcb_error == 0 && ret != -ENOENT)
        {
          out->bcb_error = (uint32_t)(-ret);
        }
      return 0;
    }

  out->bcb_which = which;
  out->bcb_seq = bcb.seq;
  out->bcb_state = bcb.state;
  out->bcb_slot = bcb.slot;
  out->bcb_trial_count = bcb.trial_count;

  if (bcb.state == VG_OTA_BCB_STATE_PENDING)
    {
      /* Ticket 04 installs the image into slot B here.  Until that path
       * exists a pending request must not jump into a partially written
       * slot, so the device stays on A.
       */
      out->reason = VG_OTA_BOOT_REASON_A_PENDING;
      return 0;
    }

  if (bcb.state == VG_OTA_BCB_STATE_FAILED)
    {
      out->reason = VG_OTA_BOOT_REASON_A_FAILED;
      return 0;
    }

  if (bcb.slot != VG_OTA_BCB_SLOT_B)
    {
      out->reason = VG_OTA_BOOT_REASON_SLOT_A;
      return 0;
    }

  if (bcb.state == VG_OTA_BCB_STATE_TRIAL && bcb.trial_count >= VG_OTA_MAX_TRIALS)
    {
      out->slot = VG_OTA_BCB_SLOT_A;
      out->reason = VG_OTA_BOOT_REASON_A_FAILED;
      return 0;
    }

  out->slot = VG_OTA_BCB_SLOT_B;
  out->reason = bcb.state == VG_OTA_BCB_STATE_TRIAL ?
                VG_OTA_BOOT_REASON_B_TRIAL : VG_OTA_BOOT_REASON_B_CONFIRMED;
  return 0;
}

const char *vg_ota_boot_reason_name(uint32_t reason)
{
  switch (reason)
    {
      case VG_OTA_BOOT_REASON_SLOT_A:
        return "slot-a";
      case VG_OTA_BOOT_REASON_B_CONFIRMED:
        return "b-confirmed";
      case VG_OTA_BOOT_REASON_B_TRIAL:
        return "b-trial";
      case VG_OTA_BOOT_REASON_A_PENDING:
        return "a-pending";
      case VG_OTA_BOOT_REASON_A_FAILED:
        return "a-failed";
      case VG_OTA_BOOT_REASON_A_NO_STAGING:
        return "a-no-staging";
      case VG_OTA_BOOT_REASON_A_NO_BCB:
        return "a-no-bcb";
      case VG_OTA_BOOT_REASON_A_BCB_INVALID:
        return "a-bcb-invalid";
      case VG_OTA_BOOT_REASON_A_EMMC_ERROR:
        return "a-emmc-error";
      case VG_OTA_BOOT_REASON_A_FAT_ERROR:
        return "a-fat-error";
      case VG_OTA_BOOT_REASON_A_READ_ERROR:
        return "a-read-error";
      case VG_OTA_BOOT_REASON_A_IMAGE_INVALID:
        return "a-image-invalid";
      case VG_OTA_BOOT_REASON_A_SAFE_STOP:
        return "a-safe-stop";
      case VG_OTA_BOOT_REASON_B_INSTALLED:
        return "b-installed";
      case VG_OTA_BOOT_REASON_A_INSTALL_FAILED:
        return "a-install-failed";
      default:
        return "none";
    }
}
