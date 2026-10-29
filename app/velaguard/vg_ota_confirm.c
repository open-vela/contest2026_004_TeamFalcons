/****************************************************************************
 * app/velaguard/vg_ota_confirm.c
 *
 * Local Install Confirmation, safety interlocks, pending BCB commitment,
 * and handoff to the boot stub installer.
 ****************************************************************************/

#ifdef __NuttX__
#include <nuttx/config.h>
#endif

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <time.h>

#include "vg_fw_identity.h"
#include "vg_ota_bcb.h"
#include "vg_ota_confirm.h"
#include "vg_ota_clock.h"
#include "vg_ota_link.h"
#include "vg_ota_manifest.h"
#if defined(__arm__) || defined(__NuttX__)
#include "vg_ota_reboot.h"
#include "vg_runtime.h"
#include "vg_discover.h"
#endif
#include "vg_ota_sha256.h"
#include "vg_ota_staging.h"

#ifdef CONFIG_VG_HMI
#include "model/vg_model.h"
#endif

#if defined(__arm__) && defined(CONFIG_NET_ETHERNET)
#include "vg_eth.h"
#endif

static uint32_t g_fault_mask = VG_OTA_FAULT_NONE;

void vg_ota_confirm_set_fault(uint32_t fault_mask)
{
#if defined(CONFIG_VG_OTA_TEST_FIXTURES) || !defined(__NuttX__)
  g_fault_mask = fault_mask;
#else
  (void)fault_mask;
#endif
}

uint32_t vg_ota_confirm_get_fault(void)
{
  return g_fault_mask;
}

void vg_ota_confirm_clear_faults(void)
{
  g_fault_mask = VG_OTA_FAULT_NONE;
}

int vg_ota_check_interlocks(const struct vg_ota_interlock_ports *ports,
                            char *reason_out, size_t reason_len)
{
  int ret;

  if (ports == NULL || ports->check_alarm == NULL ||
      ports->check_storage == NULL || ports->check_digest == NULL ||
      ports->check_idle == NULL || ports->check_rj45 == NULL ||
      ports->reboot == NULL)
    {
      if (reason_out && reason_len)
        {
          snprintf(reason_out, reason_len, "null_ports");
        }
      return -EINVAL;
    }

  /* 1. High severity active alarm */
  if (ports->check_alarm != NULL)
    {
      ret = ports->check_alarm(ports->ctx);
      if (ret != 0)
        {
          if (reason_out && reason_len)
            {
              snprintf(reason_out, reason_len, "interlock_active_alarm");
            }
          return (ret < 0) ? ret : -EAGAIN;
        }
    }

  /* 2. Storage writeability */
  if (ports->check_storage != NULL)
    {
      ret = ports->check_storage(ports->ctx);
      if (ret != 0)
        {
          if (reason_out && reason_len)
            {
              snprintf(reason_out, reason_len, "interlock_storage_unwritable");
            }
          return (ret < 0) ? ret : -EIO;
        }
    }

  /* 3. Image digest match */
  if (ports->check_digest != NULL)
    {
      ret = ports->check_digest(ports->ctx);
      if (ret != 0)
        {
          if (reason_out && reason_len)
            {
              snprintf(reason_out, reason_len, "interlock_digest_mismatch");
            }
          return (ret < 0) ? ret : -EBADMSG;
        }
    }

  /* 4. Device idle / no active configuration transaction */
  if (ports->check_idle != NULL)
    {
      ret = ports->check_idle(ports->ctx);
      if (ret != 0)
        {
          if (reason_out && reason_len)
            {
              snprintf(reason_out, reason_len, "interlock_device_busy");
            }
          return (ret < 0) ? ret : -EBUSY;
        }
    }

  /* 5. RJ45 Ethernet link availability */
  if (ports->check_rj45 != NULL)
    {
      ret = ports->check_rj45(ports->ctx);
      if (ret != 0)
        {
          if (reason_out && reason_len)
            {
              snprintf(reason_out, reason_len, "interlock_rj45_down");
            }
          return (ret < 0) ? ret : -ENETDOWN;
        }
    }

  if (reason_out && reason_len)
    {
      snprintf(reason_out, reason_len, "ok");
    }

  return 0;
}

static int read_bcb_file(const char *dir, const char *name, struct vg_ota_bcb *out)
{
  char path[160];
  uint8_t *buf;
  ssize_t got;
  size_t total = 0;
  int fd;
  int ret;

  buf = malloc(VG_OTA_BCB_FILE_BYTES);
  if (buf == NULL)
    {
      return -ENOMEM;
    }

  snprintf(path, sizeof(path), "%s/%s", dir, name);
  fd = open(path, O_RDONLY);
  if (fd < 0)
    {
      free(buf);
      return -errno;
    }

  while (total < VG_OTA_BCB_FILE_BYTES)
    {
      got = read(fd, buf + total, VG_OTA_BCB_FILE_BYTES - total);
      if (got == 0)
        {
          break;
        }

      if (got < 0)
        {
          close(fd);
          free(buf);
          return -errno;
        }

      total += (size_t)got;
    }

  close(fd);

  if (total != VG_OTA_BCB_FILE_BYTES)
    {
      free(buf);
      return -EINVAL;
    }

  ret = vg_ota_bcb_decode(buf, VG_OTA_BCB_FILE_BYTES, out);
  free(buf);
  return ret;
}

static int write_bcb_atomic(const char *dir, const char *name, const struct vg_ota_bcb *bcb)
{
  char tmp_path[160];
  char final_path[160];
  uint8_t *buf;
  ssize_t written;
  size_t total = 0;
  int fd;
  int ret;

  buf = malloc(VG_OTA_BCB_FILE_BYTES);
  if (buf == NULL)
    {
      return -ENOMEM;
    }

  ret = vg_ota_bcb_encode(bcb, buf, VG_OTA_BCB_FILE_BYTES);
  if (ret != 0)
    {
      free(buf);
      return ret;
    }

  snprintf(tmp_path, sizeof(tmp_path), "%s/%s", dir, strcmp(name, "BCB0.BIN") == 0 ? "BCB0.TMP" : "BCB1.TMP");
  snprintf(final_path, sizeof(final_path), "%s/%s", dir, name);

  fd = open(tmp_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
  if (fd < 0)
    {
      free(buf);
      return -errno;
    }

  while (total < VG_OTA_BCB_FILE_BYTES)
    {
      written = write(fd, buf + total, VG_OTA_BCB_FILE_BYTES - total);
      if (written <= 0)
        {
          close(fd);
          unlink(tmp_path);
          free(buf);
          return -EIO;
        }

      total += (size_t)written;
    }

  if (fsync(fd) < 0)
    {
      close(fd);
      unlink(tmp_path);
      free(buf);
      return -EIO;
    }

  if (close(fd) < 0)
    {
      unlink(tmp_path);
      free(buf);
      return -EIO;
    }
  /* Verify the temporary record before exposing a bootable pending BCB. */
  {
    struct vg_ota_bcb check;
    const char *tmp_name = strrchr(tmp_path, '/') + 1;
    ret = read_bcb_file(dir, tmp_name, &check);
    if (ret != 0 || check.seq != bcb->seq || check.state != bcb->state ||
        check.slot != bcb->slot || check.flags != bcb->flags ||
        check.image_size != bcb->image_size ||
        strcmp(check.task_id, bcb->task_id) || strcmp(check.version, bcb->version) ||
        memcmp(check.sha256, bcb->sha256, 32))
      {
        unlink(tmp_path);
        free(buf);
        return -EIO;
      }
  }
  free(buf);

  if (rename(tmp_path, final_path) < 0)
    {
      unlink(tmp_path);
      return -errno;
    }

  return 0;
}

static int vg_ota_write_pending_bcb(const struct vg_ota_manifest *m,
                             char *err_out, size_t err_len)
{
  const char *dir = vg_ota_staging_get_dir();
  struct vg_ota_bcb bcb0, bcb1, pending;
  uint32_t active_seq = 0;
  uint32_t active_which = 0;
  uint32_t target_which = 0;
  const char *target_name;
  const char *other_name;
  int valid0 = 0;
  int valid1 = 0;
  int ret;

  if (m == NULL)
    {
      if (err_out && err_len) snprintf(err_out, err_len, "invalid_manifest");
      return -EINVAL;
    }

  if (mkdir(dir, 0755) < 0 && errno != EEXIST)
    {
      if (err_out && err_len) snprintf(err_out, err_len, "mkdir_failed");
      return -errno;
    }

  valid0 = (read_bcb_file(dir, "BCB0.BIN", &bcb0) == 0);
  valid1 = (read_bcb_file(dir, "BCB1.BIN", &bcb1) == 0);

  if (valid0 && valid1)
    {
      if ((bcb0.seq - bcb1.seq) == 0x80000000u)
        {
          if (err_out && err_len) snprintf(err_out, err_len, "ambiguous_bcb_sequence");
          return -EINVAL;
        }
      if (bcb0.seq == bcb1.seq)
        {
          active_seq = bcb0.seq;
          active_which = 0;
        }
      else if (vg_ota_bcb_seq_newer(bcb1.seq, bcb0.seq))
        {
          active_seq = bcb1.seq;
          active_which = 1;
        }
      else
        {
          active_seq = bcb0.seq;
          active_which = 0;
        }

      target_which = 1u - active_which;
    }
  else if (valid0)
    {
      active_seq = bcb0.seq;
      target_which = 1;
    }
  else if (valid1)
    {
      active_seq = bcb1.seq;
      target_which = 0;
    }
  else
    {
      active_seq = 0;
      target_which = 0;
    }

  target_name = (target_which == 0) ? "BCB0.BIN" : "BCB1.BIN";
  other_name = (target_which == 0) ? "BCB1.BIN" : "BCB0.BIN";

  /* Ensure the other BCB file also exists (boot stub installer requires both entries) */
  if ((target_which == 0 && !valid1) || (target_which == 1 && !valid0))
    {
      struct vg_ota_bcb other_bcb;
      memset(&other_bcb, 0, sizeof(other_bcb));
      other_bcb.seq = active_seq;
      other_bcb.state = VG_OTA_BCB_STATE_FAILED;
      other_bcb.slot = VG_OTA_BCB_SLOT_B;
      other_bcb.image_size = m->image_size;
      snprintf(other_bcb.version, sizeof(other_bcb.version), "%s", m->version);
      snprintf(other_bcb.task_id, sizeof(other_bcb.task_id), "FACTORY");
      memcpy(other_bcb.sha256, m->sha256, 32);
      ret = write_bcb_atomic(dir, other_name, &other_bcb);
      if (ret != 0)
        {
          if (err_out && err_len) snprintf(err_out, err_len, "write_other_bcb_failed");
          return ret;
        }
    }

  /* Prepare pending BCB */
  memset(&pending, 0, sizeof(pending));
  pending.seq = active_seq + 1u;
  pending.state = VG_OTA_BCB_STATE_PENDING;
  pending.slot = VG_OTA_BCB_SLOT_B;
  pending.flags = VG_OTA_BCB_LOCAL_CONFIRMED;
  pending.trial_count = 0;
  pending.image_size = m->image_size;
  snprintf(pending.version, sizeof(pending.version), "%s", m->version);
  snprintf(pending.task_id, sizeof(pending.task_id), "%s", m->task_id);
  memcpy(pending.sha256, m->sha256, 32);

  ret = write_bcb_atomic(dir, target_name, &pending);
  if (ret != 0)
    {
      if (err_out && err_len) snprintf(err_out, err_len, "write_pending_bcb_failed");
      return ret;
    }

  return 0;
}

static int confirm_install_locked(enum vg_ota_confirm_source_e src,
                           const struct vg_ota_interlock_ports *ports,
                           char *err_out, size_t err_len)
{
  struct vg_ota_staging_info info = {0};
  struct vg_ota_manifest m;
  char reason[64];
  int ret;

  /* 1. Only local confirmation sources are authorized */
  if (src != VG_OTA_CONFIRM_SRC_TOUCH &&
      src != VG_OTA_CONFIRM_SRC_BUTTON)
    {
      const char *sname = "unknown";
      if (src == VG_OTA_CONFIRM_SRC_MQTT) sname = "mqtt";
      else if (src == VG_OTA_CONFIRM_SRC_PHONE) sname = "phone";
      else if (src == VG_OTA_CONFIRM_SRC_VOICE) sname = "voice";
      else if (src == VG_OTA_CONFIRM_SRC_AGENT) sname = "agent";

      if (err_out && err_len)
        {
          snprintf(err_out, err_len, "unauthorized_source: %s", sname);
        }
      return -EPERM;
    }

  /* 2. Check state must be READY_AWAITING_CONFIRM */
  if (vg_ota_staging_get_info(&info) != 0 ||
      info.state != VG_OTA_STATE_READY_AWAITING_CONFIRM)
    {
      if (err_out && err_len)
        {
          snprintf(err_out, err_len, "not_in_ready_state (current=%s)",
                   vg_ota_state_name(info.state));
        }
      return -EBUSY;
    }

  if (vg_fw_slot() != 'A' && vg_fw_slot() != 'B')
    {
      if (err_out && err_len) snprintf(err_out,err_len,"running_slot_invalid");
      return -EPERM;
    }

  /* 3. Safety interlock checks */
  ret = vg_ota_check_interlocks(ports, reason, sizeof(reason));
  if (ret != 0)
    {
      /* Interlock failed! Remain in READY_AWAITING_CONFIRM, no BCB write, no reboot */
      vg_ota_staging_set_error(reason);
      if (err_out && err_len)
        {
          snprintf(err_out, err_len, "%s", reason);
        }
      return ret;
    }

  /* 4. Load staged manifest */
  ret = vg_ota_staging_load_manifest(&m);
  if (ret != 0)
    {
      vg_ota_staging_set_error("load_manifest_failed");
      if (err_out && err_len) snprintf(err_out, err_len, "load_manifest_failed");
      return ret;
    }

  /* Bind confirmation to the exact READY offer, not just a CRC-valid file. */
  if (m.image_size != info.image_size || m.slot != VG_OTA_BCB_SLOT_B ||
      strcmp(m.task_id, info.task_id) || strcmp(m.version, info.version) ||
      memcmp(m.sha256, info.sha256, 32))
    {
      vg_ota_staging_set_error("ready_manifest_changed");
      if (err_out && err_len) snprintf(err_out, err_len, "ready_manifest_changed");
      return -EBADMSG;
    }
  if (!(m.flags & VG_OTA_MANIFEST_FIXTURE))
    {
      time_t now = vg_ota_utc_now();
      if (now < 1767225600 || (uint64_t)now < m.issued_at ||
          (uint64_t)now >= m.expires_at)
        {
          vg_ota_staging_set_error("interlock_time_invalid");
          if (err_out && err_len) snprintf(err_out, err_len, "interlock_time_invalid");
          return -ETIME;
        }
    }

  ret = vg_ota_manifest_validate(&m, NULL, reason, sizeof(reason));
  if (ret != 0)
    {
      vg_ota_staging_set_error(reason);
      if (err_out && err_len) snprintf(err_out, err_len, "%s", reason);
      return ret;
    }

  /* 5. Write pending BCB with VG_OTA_BCB_LOCAL_CONFIRMED */
  ret = vg_ota_write_pending_bcb(&m, err_out, err_len);
  if (ret != 0)
    {
      /* Pending BCB write failed: remain in READY, do NOT reboot */
      vg_ota_staging_set_error("write_pending_bcb_failed");
      return ret;
    }

  /* 6. Enter INSTALL_PENDING -> sync -> REBOOTING -> reboot */
  vg_ota_staging_set_state(VG_OTA_STATE_INSTALL_PENDING, "none");
  vg_ota_staging_set_state(VG_OTA_STATE_REBOOTING, "none");
  sync();

  if (ports != NULL && ports->reboot != NULL)
    {
      ports->reboot(ports->ctx);
    }

  return 0;
}

int vg_ota_confirm_decline(const char *task_id, const char *reason)
{
  return vg_ota_staging_decline(task_id, reason);
}

int vg_ota_confirm_expire(const char *task_id)
{
  return vg_ota_staging_expire(task_id);
}

bool vg_ota_confirm_is_suppressed(const char *task_id)
{
  return vg_ota_staging_is_suppressed(task_id);
}

/* ──────────────────────────────────────────────────────────────────────────
 * Board default interlock ports
 * ────────────────────────────────────────────────────────────────────────── */

static int board_check_alarm(void *ctx)
{
  (void)ctx;
  if (g_fault_mask & VG_OTA_FAULT_ALARM)
    {
      return -EAGAIN;
    }

#if defined(__arm__) || defined(__NuttX__)
  struct vg_runtime_anomaly worst;
  int total;
  int count = vg_runtime_anomalies(&worst, 1, &total, 0);
  if (count < 0 || (count > 0 && worst.rank == 0))
    {
      return -EAGAIN;
    }
#endif
  return 0;
}

static int board_check_storage(void *ctx)
{
  char path[160];
  int fd;

  (void)ctx;
  if (g_fault_mask & VG_OTA_FAULT_STORAGE)
    {
      return -EIO;
    }

  snprintf(path, sizeof(path), "%s/WRITE.TMP", vg_ota_staging_get_dir());
  fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
  if (fd < 0)
    {
      return -EIO;
    }

  if (write(fd, "probe", 5) != 5 || fsync(fd) < 0)
    {
      close(fd);
      unlink(path);
      return -EIO;
    }

  close(fd);
  unlink(path);
  return 0;
}

static int board_check_digest(void *ctx)
{
  (void)ctx;
  if (g_fault_mask & VG_OTA_FAULT_DIGEST)
    {
      return -EBADMSG;
    }

  return vg_ota_staging_verify_digest();
}

#if defined(__arm__) || defined(__NuttX__)
static int candidate_is_pending(void)
{
#ifndef CONFIG_VG_DISCOVER_CANDIDATE_PATH
#define CONFIG_VG_DISCOVER_CANDIDATE_PATH "/data/velaguard/discover/point_table_candidate.json"
#endif
#ifndef CONFIG_VG_DISCOVER_POINTS_PATH
#define CONFIG_VG_DISCOVER_POINTS_PATH "/data/velaguard/config/points.json"
#endif
  uint8_t a[128], b[128];
  ssize_t na, nb;
  int rc = 0;
  int ca = open(CONFIG_VG_DISCOVER_CANDIDATE_PATH, O_RDONLY);
  int co;
  if (ca < 0) return errno == ENOENT ? 0 : -EBUSY;
  co = open(CONFIG_VG_DISCOVER_POINTS_PATH, O_RDONLY);
  if (co < 0) { close(ca); return -EBUSY; }
  do {
    na = read(ca, a, sizeof(a));
    nb = read(co, b, sizeof(b));
    if (na < 0 || nb < 0 || na != nb || memcmp(a,b,(size_t)na))
      { rc = -EBUSY; break; }
  } while (na > 0);
  close(ca);
  close(co);
  return rc;
}
#endif

static int board_check_idle(void *ctx)
{
  (void)ctx;
  if (g_fault_mask & VG_OTA_FAULT_BUSY) return -EBUSY;
#if defined(__arm__) || defined(__NuttX__)
  if (vg_bus_is_locked() || candidate_is_pending() != 0) return -EBUSY;
#endif
#ifdef CONFIG_VG_HMI
  if (vg_model_add_sensor_state() != VG_ADD_IDLE) return -EBUSY;
#endif
  return 0;
}

static int board_check_rj45(void *ctx)
{
  (void)ctx;
  if (g_fault_mask & VG_OTA_FAULT_RJ45)
    {
      return -ENETDOWN;
    }

#if defined(__NuttX__)
  if (!vg_ota_link_ready()) return -ENETDOWN;
#else
  return -ENETDOWN;
#endif
  return 0;
}

static void board_reboot(void *ctx)
{
  (void)ctx;
#if defined(__arm__) || defined(__NuttX__)
  (void)vg_ota_safe_reboot();
#else
  printf("vgota: host board_reboot invoked\n");
#endif
}

static const struct vg_ota_interlock_ports g_board_ports = {
  .check_alarm = board_check_alarm,
  .check_storage = board_check_storage,
  .check_digest = board_check_digest,
  .check_idle = board_check_idle,
  .check_rj45 = board_check_rj45,
  .reboot = board_reboot,
  .ctx = NULL
};

const struct vg_ota_interlock_ports *vg_ota_get_board_interlock_ports(void)
{
  return &g_board_ports;
}

int vg_ota_confirm_install(enum vg_ota_confirm_source_e src,
                           const struct vg_ota_interlock_ports *ports,
                           char *err_out, size_t err_len)
{
  int ret;
  vg_ota_staging_lock();
  vg_ota_staging_tick();
  ret = confirm_install_locked(src, ports, err_out, err_len);
  vg_ota_staging_unlock();
  return ret;
}
