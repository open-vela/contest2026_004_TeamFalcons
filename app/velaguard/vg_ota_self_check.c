/****************************************************************************
 * app/velaguard/vg_ota_self_check.c
 *
 * New firmware self-check, confirmed / failed BCB update, and watchdog feed.
 ****************************************************************************/

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "vg_fw_identity.h"
#include "vg_ota_bcb.h"
#include "vg_ota_boot.h"
#include "vg_ota_mbox.h"
#include "vg_ota_reboot.h"
#include "vg_ota_self_check.h"

#if !defined(__arm__)
volatile struct vg_ota_mbox g_host_ota_mbox;
#endif

static char g_ota_dir[128] = VG_OTA_DIR;
static struct vg_ota_self_check_status g_status;

void vg_ota_self_check_set_dir(const char *dir)
{
  if (dir != NULL && dir[0] != '\0')
    {
      snprintf(g_ota_dir, sizeof(g_ota_dir), "%s", dir);
    }
  else
    {
      snprintf(g_ota_dir, sizeof(g_ota_dir), "%s", VG_OTA_DIR);
    }
}

const char *vg_ota_self_check_get_dir(void)
{
  return g_ota_dir;
}

void vg_ota_watchdog_kick(void)
{
#if defined(__arm__)
  volatile uint32_t *kr = (volatile uint32_t *)0x58004800u;
  *kr = 0xaaaau;
#endif
}

static int read_file(const char *name, uint8_t *buf, size_t len)
{
  char path[160];
  ssize_t got;
  size_t total = 0;
  int fd;

  snprintf(path, sizeof(path), "%s/%s", g_ota_dir, name);
  fd = open(path, O_RDONLY);
  if (fd < 0)
    {
      return -errno;
    }

  while (total < len)
    {
      got = read(fd, buf + total, len - total);
      if (got == 0)
        {
          close(fd);
          return -EINVAL;
        }

      if (got < 0)
        {
          close(fd);
          return -errno;
        }

      total += (size_t)got;
    }

  close(fd);
  return 0;
}

static int store_file(const char *name, const uint8_t *buf, size_t len)
{
  char path[160];
  ssize_t written;
  size_t total = 0;
  int fd;

  if (mkdir(g_ota_dir, 0755) < 0 && errno != EEXIST)
    {
      return -errno;
    }

  snprintf(path, sizeof(path), "%s/%s", g_ota_dir, name);
  fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
  if (fd < 0)
    {
      return -errno;
    }

  while (total < len)
    {
      written = write(fd, buf + total, len - total);
      if (written <= 0)
        {
          close(fd);
          return -errno;
        }

      total += (size_t)written;
    }

  if (fsync(fd) < 0)
    {
      close(fd);
      return -errno;
    }

  close(fd);
  return 0;
}

static void write_state_file(const char *result_str, uint32_t seq,
                             const char *task_id, const char *ver)
{
  char line[160];
  int n = snprintf(line, sizeof(line),
                   "result=%s seq=%u task=%s ver=%s\n",
                   result_str, (unsigned)seq,
                   task_id ? task_id : "", ver ? ver : "");
  if (n > 0)
    {
      (void)store_file(VG_OTA_STATE_NAME, (const uint8_t *)line, (size_t)n);
    }
}

int vg_ota_self_check_get_inject_fail(void)
{
  char path[160];
  struct stat st;

  snprintf(path, sizeof(path), "%s/%s", g_ota_dir, VG_OTA_FAIL_INJECT_NAME);
  return (stat(path, &st) == 0) ? 1 : 0;
}

int vg_ota_self_check_set_inject_fail(int enable)
{
  char path[160];

  snprintf(path, sizeof(path), "%s/%s", g_ota_dir, VG_OTA_FAIL_INJECT_NAME);
  if (enable)
    {
      const char *content = "FORCE_FAIL\n";
      return store_file(VG_OTA_FAIL_INJECT_NAME,
                        (const uint8_t *)content, strlen(content));
    }

  if (unlink(path) < 0 && errno != ENOENT)
    {
      return -errno;
    }

  return 0;
}

static int read_active_bcb(struct vg_ota_bcb *active, uint32_t *active_which)
{
  uint8_t *buf = malloc(VG_OTA_BCB_FILE_BYTES);
  struct vg_ota_bcb bcb0, bcb1;
  int have0 = 0, have1 = 0;

  if (buf == NULL)
    {
      return -ENOMEM;
    }

  if (read_file("BCB0.BIN", buf, VG_OTA_BCB_FILE_BYTES) == 0 &&
      vg_ota_bcb_decode(buf, VG_OTA_BCB_FILE_BYTES, &bcb0) == 0)
    {
      have0 = 1;
    }

  if (read_file("BCB1.BIN", buf, VG_OTA_BCB_FILE_BYTES) == 0 &&
      vg_ota_bcb_decode(buf, VG_OTA_BCB_FILE_BYTES, &bcb1) == 0)
    {
      have1 = 1;
    }

  free(buf);

  if (!have0 && !have1)
    {
      return -ENOENT;
    }

  if (have0 && !have1)
    {
      *active = bcb0;
      *active_which = 0;
      return 0;
    }

  if (!have0 && have1)
    {
      *active = bcb1;
      *active_which = 1;
      return 0;
    }

  if (bcb0.seq == bcb1.seq)
    {
      return -EINVAL;
    }

  if (vg_ota_bcb_seq_newer(bcb1.seq, bcb0.seq))
    {
      *active = bcb1;
      *active_which = 1;
    }
  else
    {
      *active = bcb0;
      *active_which = 0;
    }

  return 0;
}

int vg_ota_self_check_needed(void)
{
  struct vg_ota_bcb bcb;
  uint32_t which;

  if (vg_fw_slot() != 'B')
    {
      return 0;
    }

  if (read_active_bcb(&bcb, &which) != 0)
    {
      return 0;
    }

  return (bcb.state == VG_OTA_BCB_STATE_TRIAL && bcb.slot == VG_OTA_BCB_SLOT_B);
}

int vg_ota_self_check_get_status(struct vg_ota_self_check_status *status)
{
  struct vg_ota_bcb bcb;
  uint32_t which = 0;

  if (status == NULL)
    {
      return -EINVAL;
    }

  *status = g_status;
  if (read_active_bcb(&bcb, &which) == 0)
    {
      status->active_bcb_which = which;
      status->active_bcb_seq = bcb.seq;
      status->active_bcb_state = bcb.state;
      status->active_bcb_slot = bcb.slot;
      status->trial_count = bcb.trial_count;
      snprintf(status->active_version, sizeof(status->active_version),
               "%s", bcb.version);
      snprintf(status->active_task_id, sizeof(status->active_task_id),
               "%s", bcb.task_id);
    }

  return 0;
}

static int commit_bcb_transition(const struct vg_ota_bcb *current,
                                 uint32_t current_which,
                                 uint32_t target_state)
{
  struct vg_ota_bcb next = *current;
  uint8_t *buf;
  uint32_t target_which = 1u - current_which;
  const char *target_name = (target_which == 0) ? "BCB0.BIN" : "BCB1.BIN";
  int ret;

  buf = malloc(VG_OTA_BCB_FILE_BYTES);
  if (buf == NULL)
    {
      return -ENOMEM;
    }

  next.seq = current->seq + 1u;
  next.state = target_state;
  next.slot = VG_OTA_BCB_SLOT_B;

  ret = vg_ota_bcb_encode(&next, buf, VG_OTA_BCB_FILE_BYTES);
  if (ret != 0)
    {
      free(buf);
      return ret;
    }

  ret = store_file(target_name, buf, VG_OTA_BCB_FILE_BYTES);
  if (ret != 0)
    {
      free(buf);
      return ret;
    }

  /* Verify written file */
  memset(buf, 0, VG_OTA_BCB_FILE_BYTES);
  ret = read_file(target_name, buf, VG_OTA_BCB_FILE_BYTES);
  if (ret != 0)
    {
      free(buf);
      return ret;
    }

  ret = vg_ota_bcb_decode(buf, VG_OTA_BCB_FILE_BYTES, &next);
  free(buf);

  if (ret != 0 || next.state != target_state || next.seq != current->seq + 1u)
    {
      return -EBADMSG;
    }

  return 0;
}

int vg_ota_self_check_run(char *detail, size_t detail_len)
{
  struct vg_ota_bcb active;
  uint32_t which = 0;
  char err[128] = {0};
  int failed = 0;
  int ret;

  vg_ota_watchdog_kick();

  if (vg_fw_slot() != 'B')
    {
      snprintf(err, sizeof(err), "running on slot %c, expected B", vg_fw_slot());
      failed = 1;
    }
  else if (vg_fw_link_base() != VG_FW_SLOT_B_BASE)
    {
      snprintf(err, sizeof(err), "link base 0x%08x != expected 0x%08x",
               (unsigned)vg_fw_link_base(), (unsigned)VG_FW_SLOT_B_BASE);
      failed = 1;
    }

  if (!failed)
    {
      volatile struct vg_ota_mbox *mbox = VG_OTA_MBOX;
      if (mbox->magic == VG_OTA_MBOX_MAGIC &&
          mbox->version == VG_OTA_MBOX_VERSION)
        {
          if (mbox->boot_slot != VG_OTA_BCB_SLOT_B)
            {
              snprintf(err, sizeof(err), "mbox boot_slot %u != B",
                       (unsigned)mbox->boot_slot);
              failed = 1;
            }
        }
    }

  if (!failed)
    {
      ret = read_active_bcb(&active, &which);
      if (ret != 0)
        {
          snprintf(err, sizeof(err), "read BCB failed ERR=%d", -ret);
          failed = 1;
        }
      else if (active.state == VG_OTA_BCB_STATE_CONFIRMED &&
               active.slot == VG_OTA_BCB_SLOT_B)
        {
          /* Already confirmed */
          g_status.state = VG_OTA_CHECK_STATE_PASSED;
          snprintf(g_status.detail, sizeof(g_status.detail), "already confirmed");
          if (detail && detail_len)
            {
              snprintf(detail, detail_len, "already confirmed");
            }
          return 0;
        }
      else if (active.state != VG_OTA_BCB_STATE_TRIAL ||
               active.slot != VG_OTA_BCB_SLOT_B)
        {
          snprintf(err, sizeof(err), "BCB not in trial slot B (state=%s slot=%s)",
                   vg_ota_bcb_state_name(active.state),
                   vg_ota_bcb_slot_name(active.slot));
          failed = 1;
        }
      else if (strcmp(active.version, vg_fw_version()) != 0)
        {
          snprintf(err, sizeof(err), "BCB version %s != fw %s",
                   active.version, vg_fw_version());
          failed = 1;
        }
    }

  /* Check failure injection */
  if (!failed)
    {
      if (vg_ota_self_check_get_inject_fail())
        {
          vg_ota_self_check_set_inject_fail(0); /* consume trigger */
          snprintf(err, sizeof(err), "injected failure via %s", VG_OTA_FAIL_INJECT_NAME);
          failed = 1;
        }
      else if (strncmp(active.task_id, "FAIL", 4) == 0)
        {
          snprintf(err, sizeof(err), "injected failure via task_id=%s", active.task_id);
          failed = 1;
        }
    }

  /* Storage write check */
  if (!failed)
    {
      const char *canary = "SELFCHECK_CANARY\n";
      ret = store_file("CANARY.TMP", (const uint8_t *)canary, strlen(canary));
      if (ret != 0)
        {
          snprintf(err, sizeof(err), "storage write test failed ERR=%d", -ret);
          failed = 1;
        }
      else
        {
          char cpath[160];
          snprintf(cpath, sizeof(cpath), "%s/CANARY.TMP", g_ota_dir);
          unlink(cpath);
        }
    }

  if (failed)
    {
      g_status.state = VG_OTA_CHECK_STATE_FAILED;
      snprintf(g_status.detail, sizeof(g_status.detail), "%s", err);
      if (detail && detail_len)
        {
          snprintf(detail, detail_len, "%s", err);
        }

      if (read_active_bcb(&active, &which) == 0)
        {
          (void)commit_bcb_transition(&active, which, VG_OTA_BCB_STATE_FAILED);
          write_state_file("self_check_failed", active.seq + 1u,
                           active.task_id, active.version);
        }

      return -1;
    }

  /* Success */
  ret = commit_bcb_transition(&active, which, VG_OTA_BCB_STATE_CONFIRMED);
  if (ret != 0)
    {
      snprintf(err, sizeof(err), "commit confirmed BCB failed ERR=%d", -ret);
      g_status.state = VG_OTA_CHECK_STATE_FAILED;
      snprintf(g_status.detail, sizeof(g_status.detail), "%s", err);
      if (detail && detail_len)
        {
          snprintf(detail, detail_len, "%s", err);
        }
      return -1;
    }

  write_state_file("confirmed", active.seq + 1u, active.task_id, active.version);
  g_status.state = VG_OTA_CHECK_STATE_PASSED;
  snprintf(g_status.detail, sizeof(g_status.detail), "confirmed seq=%u",
           (unsigned)(active.seq + 1u));
  if (detail && detail_len)
    {
      snprintf(detail, detail_len, "confirmed seq=%u", (unsigned)(active.seq + 1u));
    }

  vg_ota_watchdog_kick();
  return 0;
}

int vg_ota_self_check_boot(void)
{
  char detail[128];
  int ret;

  if (!vg_ota_self_check_needed())
    {
      return 0;
    }

  printf("vgota: running new firmware self-check on slot B...\n");
  ret = vg_ota_self_check_run(detail, sizeof(detail));
  if (ret == 0)
    {
      printf("vgota: self-check PASSED: %s\n", detail);
      vg_ota_watchdog_kick();
      return 0;
    }

  printf("vgota: self-check FAILED: %s\n", detail);
  printf("vgota: rolling back to slot A...\n");
  sync();
  usleep(100000);
  vg_ota_safe_reboot();
  return -1;
}

int vg_ota_self_check_manual_confirm(void)
{
  struct vg_ota_bcb active;
  uint32_t which;
  int ret;

  ret = read_active_bcb(&active, &which);
  if (ret != 0)
    {
      return ret;
    }

  ret = commit_bcb_transition(&active, which, VG_OTA_BCB_STATE_CONFIRMED);
  if (ret != 0)
    {
      return ret;
    }

  write_state_file("confirmed", active.seq + 1u, active.task_id, active.version);
  g_status.state = VG_OTA_CHECK_STATE_PASSED;
  snprintf(g_status.detail, sizeof(g_status.detail), "manually confirmed seq=%u",
           (unsigned)(active.seq + 1u));
  vg_ota_watchdog_kick();
  return 0;
}

int vg_ota_self_check_manual_rollback(const char *reason)
{
  struct vg_ota_bcb active;
  uint32_t which;
  int ret;

  ret = read_active_bcb(&active, &which);
  if (ret != 0)
    {
      return ret;
    }

  ret = commit_bcb_transition(&active, which, VG_OTA_BCB_STATE_FAILED);
  if (ret != 0)
    {
      return ret;
    }

  write_state_file("rolled_back", active.seq + 1u, active.task_id, active.version);
  g_status.state = VG_OTA_CHECK_STATE_FAILED;
  snprintf(g_status.detail, sizeof(g_status.detail), "manual rollback: %s",
           reason ? reason : "user request");

  printf("vgota: rollback to slot A committed, rebooting...\n");
  sync();
  usleep(100000);
  vg_ota_safe_reboot();
  return 0;
}
