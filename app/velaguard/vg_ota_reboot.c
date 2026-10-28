/****************************************************************************
 * app/velaguard/vg_ota_reboot.c
 *
 * OTA-initiated reset.  Mounted filesystems are synced first so the boot
 * stub only observes persisted FAT directory entries.
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>
#include <stdio.h>
#include <sys/boardctl.h>
#include <unistd.h>

#include "vg_fw_identity.h"
#include "vg_ota_reboot.h"

int vg_ota_safe_reboot(void)
{
  printf("vgota: safe reboot version=%s slot=%s\n",
         vg_fw_version(), vg_fw_slot_name());
  fflush(stdout);
  usleep(50000);

  sync();
  boardctl(BOARDIOC_RESET, 0);
  return -errno;
}
