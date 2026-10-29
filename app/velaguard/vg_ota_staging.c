/****************************************************************************
 * app/velaguard/vg_ota_staging.c
 *
 * Staging image storage, manifest validation, and READY_AWAITING_CONFIRM.
 ****************************************************************************/

#ifdef __NuttX__
#include <nuttx/config.h>
#endif

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/statfs.h>
#include <unistd.h>
#include <time.h>
#include <pthread.h>

#include "vg_device_id.h"
#include "vg_fw_identity.h"
#include "vg_ota_install.h"
#include "vg_ota_manifest.h"
#include "vg_ota_sha256.h"
#include "vg_ota_staging.h"
#include "vg_ota_trust.h"
#include "vg_ota_clock.h"

static char g_staging_dir[128] = VG_OTA_STAGING_DIR;
static struct vg_ota_staging_info g_staging_info = {
  .state = VG_OTA_STATE_IDLE
};
/* 网络读取期间仍可读取上一份完整状态，界面和心跳不能等待下载锁。 */
static struct vg_ota_staging_info g_published_info;
static pthread_mutex_t g_info_lock = PTHREAD_MUTEX_INITIALIZER;
static void publish_info(void)
{
  pthread_mutex_lock(&g_info_lock);
  g_published_info = g_staging_info;
  pthread_mutex_unlock(&g_info_lock);
}
static uint64_t g_ready_since_ms;
static pthread_once_t g_ota_once = PTHREAD_ONCE_INIT;
static pthread_mutex_t g_ota_lock;

static void init_ota_lock(void)
{
  pthread_mutexattr_t attr;
  pthread_mutexattr_init(&attr);
  pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
  pthread_mutex_init(&g_ota_lock, &attr);
  pthread_mutexattr_destroy(&attr);
}

void vg_ota_staging_lock(void)
{
  pthread_once(&g_ota_once, init_ota_lock);
  pthread_mutex_lock(&g_ota_lock);
}

void vg_ota_staging_unlock(void)
{
  pthread_mutex_unlock(&g_ota_lock);
}

static uint64_t monotonic_ms(void)
{
  struct timespec ts;
  if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return 0;
  return (uint64_t)ts.tv_sec * 1000u + ts.tv_nsec / 1000000u;
}

/* Append-only tombstones survive reboot and are never evicted. A damaged
 * journal blocks new offers, so a torn write cannot reauthorize an old task.
 * At 4096 terminal tasks (176 KiB), new offers require maintenance; no silent
 * eviction or unbounded storage growth is permitted.
 */
static bool vg_ota_staging_is_suppressed_locked(const char *task_id)
{
  char path[160];
  uint8_t record[44];
  ssize_t n;
  int fd;
  bool found = false;
  unsigned records = 0;
  if (task_id == NULL || !task_id[0]) return false;
  snprintf(path, sizeof(path), "%s/SUPPRESS.BIN", g_staging_dir);
  fd = open(path, O_RDONLY);
  if (fd < 0) return errno != ENOENT;
  while ((n = read(fd, record, sizeof(record))) > 0)
    {
      uint32_t crc;
      memcpy(&crc, record + 40, 4);
      if (n != sizeof(record) || !memchr(record, 0, 40) ||
          crc != vg_ota_crc32(record, 40))
        { found = true; break; }
      if (strcmp((char *)record, task_id) == 0) found = true;
      if (++records >= 4096u) { found = true; break; }
    }
  if (n < 0) found = true;
  close(fd);
  return found;
}

static int suppress_task(const char *task_id)
{
  char path[160];
  uint8_t record[44] = {0};
  uint32_t crc;
  int fd, ret;
  if (vg_ota_staging_is_suppressed(task_id)) return 0;
  snprintf((char *)record, 40, "%s", task_id);
  crc = vg_ota_crc32(record, 40);
  memcpy(record + 40, &crc, 4);
  snprintf(path, sizeof(path), "%s/SUPPRESS.BIN", g_staging_dir);
  fd = open(path, O_WRONLY | O_CREAT | O_APPEND, 0644);
  if (fd < 0) return -errno;
  ret = write(fd, record, sizeof(record)) == sizeof(record) && fsync(fd) == 0 ? 0 : -EIO;
  if (close(fd) < 0) ret = -EIO;
  return ret;
}

void vg_ota_staging_tick(void)
{
  pthread_once(&g_ota_once, init_ota_lock);
  if (pthread_mutex_trylock(&g_ota_lock) != 0) return;
  if (g_staging_info.state == VG_OTA_STATE_READY_AWAITING_CONFIRM &&
      monotonic_ms() - g_ready_since_ms >= VG_OTA_READY_TIMEOUT_MS)
    vg_ota_staging_expire(g_staging_info.task_id);
  vg_ota_staging_unlock();
}

void vg_ota_staging_set_dir(const char *dir)
{
  memset(&g_staging_info, 0, sizeof(g_staging_info));
  publish_info();
  if (dir != NULL && dir[0] != '\0')
    {
      snprintf(g_staging_dir, sizeof(g_staging_dir), "%s", dir);
    }
  else
    {
      snprintf(g_staging_dir, sizeof(g_staging_dir), "%s", VG_OTA_STAGING_DIR);
    }
}

const char *vg_ota_staging_get_dir(void)
{
  return g_staging_dir;
}

const char *vg_ota_state_name(uint32_t state)
{
  switch (state)
    {
      case VG_OTA_STATE_IDLE:
        return "IDLE";
      case VG_OTA_STATE_OFFER_RECEIVED:
        return "OFFER_RECEIVED";
      case VG_OTA_STATE_DOWNLOADING:
        return "DOWNLOADING";
      case VG_OTA_STATE_VERIFYING:
        return "VERIFYING";
      case VG_OTA_STATE_READY_AWAITING_CONFIRM:
        return "READY_AWAITING_CONFIRM";
      case VG_OTA_STATE_INSTALL_PENDING:
        return "INSTALL_PENDING";
      case VG_OTA_STATE_REBOOTING:
        return "REBOOTING";
      case VG_OTA_STATE_CONFIRMED:
        return "CONFIRMED";
      case VG_OTA_STATE_DECLINED:
        return "DECLINED";
      case VG_OTA_STATE_EXPIRED:
        return "EXPIRED";
      case VG_OTA_STATE_FAILED:
        return "FAILED";
      case VG_OTA_STATE_ROLLED_BACK:
        return "ROLLED_BACK";
      default:
        return "UNKNOWN";
    }
}

int vg_ota_staging_check_space(uint32_t image_size, uint64_t *free_out, uint64_t *req_out)
{
  struct statfs fs;
  uint64_t required;
  uint64_t available;
  const char *check_path = g_staging_dir;
  struct stat st;

  /* If staging directory does not exist yet, check mount parent */
  if (stat(check_path, &st) != 0)
    {
      check_path = "/mnt/emmc";
      if (stat(check_path, &st) != 0)
        {
          check_path = "/";
        }
    }

  if (statfs(check_path, &fs) != 0)
    {
      return -errno;
    }

  required = (uint64_t)image_size + (4096u * 4u) + VG_OTA_STAGING_SAFETY_MARGIN;
  available = (uint64_t)fs.f_bfree * (uint64_t)fs.f_bsize;

  if (free_out)
    {
      *free_out = available;
    }

  if (req_out)
    {
      *req_out = required;
    }

  if (available < required)
    {
      return -ENOSPC;
    }

  return 0;
}

static int parse_version_numbers(const char *v, int *maj, int *min, int *pat)
{
  if (v == NULL) return -1;
  if (sscanf(v, "%d.%d.%d", maj, min, pat) != 3) return -1;
  return 0;
}

int vg_ota_manifest_validate(const struct vg_ota_manifest *m,
                             const uint8_t actual_digest[32],
                             char *err_out, size_t err_len)
{
  char err[64] = {0};
  const char *dev_id;
  int m_maj, m_min, m_pat;
  int cur_maj, cur_min, cur_pat;
  uint32_t i;
  int sig_nonzero = 0;

  if (m == NULL)
    {
      snprintf(err, sizeof(err), "manifest_null");
      goto fail;
    }

  if (!memchr(m->version,0,sizeof(m->version)) ||
      !memchr(m->task_id,0,sizeof(m->task_id)) ||
      !memchr(m->device_id,0,sizeof(m->device_id)) ||
      !memchr(m->model,0,sizeof(m->model)) ||
      !memchr(m->key_id,0,sizeof(m->key_id)) ||
      (m->flags & ~VG_OTA_MANIFEST_FIXTURE))
    { snprintf(err,sizeof(err),"manifest_text_or_flags_invalid"); goto fail; }

  /* 1. Target slot must be B */
  if (m->slot != VG_OTA_BCB_SLOT_B)
    {
      snprintf(err, sizeof(err), "bad_target_slot");
      goto fail;
    }

  /* 2. Device ID must match this board */
  dev_id = vg_device_id();
  if (dev_id == NULL || strcmp(m->device_id, dev_id) != 0)
    {
      snprintf(err, sizeof(err), "bad_device_id");
      goto fail;
    }

  /* 3. Model must match STM32H750B-DK */
  if (strcmp(m->model, VG_OTA_MODEL) != 0)
    {
      snprintf(err, sizeof(err), "bad_model");
      goto fail;
    }

  /* 4. Image size within bounds */
  if (m->image_size < 8 || m->image_size > VG_OTA_IMAGE_MAX_BYTES)
    {
      snprintf(err, sizeof(err), "bad_image_size");
      goto fail;
    }

  /* 5. Version must be valid semver */
  if (!vg_fw_version_valid(m->version) ||
      parse_version_numbers(m->version, &m_maj, &m_min, &m_pat) != 0)
    {
      snprintf(err, sizeof(err), "bad_version_format");
      goto fail;
    }

  /* For non-fixture manifests, version must not be older than current */
  if ((m->flags & VG_OTA_MANIFEST_FIXTURE) == 0)
    {
      if (parse_version_numbers(vg_fw_version(), &cur_maj, &cur_min, &cur_pat) == 0)
        {
          if (m_maj < cur_maj ||
              (m_maj == cur_maj && m_min < cur_min) ||
              (m_maj == cur_maj && m_min == cur_min && m_pat < cur_pat))
            {
              snprintf(err, sizeof(err), "version_downgrade_rejected");
              goto fail;
            }
        }
    }

  /* 6. Time window check */
  if (m->expires_at <= m->issued_at)
    {
      snprintf(err, sizeof(err), "bad_time_window");
      goto fail;
    }

  /* 7. Signature presence check */
  for (i = 0; i < sizeof(m->signature); i++)
    {
      sig_nonzero |= m->signature[i];
    }

  if ((m->flags & VG_OTA_MANIFEST_FIXTURE) == 0 && !sig_nonzero)
    {
      snprintf(err, sizeof(err), "signature_missing");
      goto fail;
    }

  if (m->flags & VG_OTA_MANIFEST_FIXTURE)
    {
#if defined(__NuttX__) && !defined(CONFIG_VG_OTA_TEST_FIXTURES)
      snprintf(err, sizeof(err), "fixture_forbidden");
      goto fail;
#endif
    }
  else
    {
      time_t now = vg_ota_utc_now();
      if (now < 1767225600 || (uint64_t)now < m->issued_at || (uint64_t)now >= m->expires_at)
        { snprintf(err, sizeof(err), "manifest_expired_or_time_invalid"); goto fail; }
      if (vg_ota_trust_verify_manifest(m) != 0)
        { snprintf(err, sizeof(err), "signature_invalid_or_untrusted"); goto fail; }
    }

  /* 8. Digest match */
  if (actual_digest != NULL)
    {
      if (memcmp(m->sha256, actual_digest, 32) != 0)
        {
          snprintf(err, sizeof(err), "digest_mismatch");
          goto fail;
        }
    }

  return 0;

fail:
  if (err_out && err_len > 0)
    {
      snprintf(err_out, err_len, "%s", err);
    }

  return -EINVAL;
}

static void record_state_file(const char *state_str, const char *task_id,
                              const char *version, uint32_t size,
                              const uint8_t *sha, const char *err)
{
  char path[160];
  char line[200];
  int fd;

  snprintf(path, sizeof(path), "%s/%s", g_staging_dir, VG_OTA_STATE_BIN_NAME);
  fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
  if (fd >= 0)
    {
      int n = snprintf(line, sizeof(line),
                       "state=%s task=%s ver=%s size=%" PRIu32
                       " sha=%02x%02x%02x%02x err=%s\n",
                       state_str,
                       task_id ? task_id : "",
                       version ? version : "",
                       size,
                       sha ? sha[0] : 0, sha ? sha[1] : 0,
                       sha ? sha[2] : 0, sha ? sha[3] : 0,
                       err ? err : "none");
      if (n > 0)
        {
          (void)write(fd, line, (size_t)n);
          (void)fsync(fd);
        }
      close(fd);
    }
}

static int vg_ota_staging_stage_locked(const struct vg_ota_manifest *m,
                         vg_ota_source_read_fn source_read, void *source_ctx,
                         char *err_out, size_t err_len)
{
  char tmp_path[160];
  char bin_path[160];
  char manifest_path[160];
  uint8_t *buf = NULL;
  uint8_t manifest_buf[VG_OTA_MANIFEST_BYTES];
  uint8_t calc_digest[32];
  uint8_t vector[8];
  struct vg_ota_sha256 sha;
  uint64_t free_b = 0, req_b = 0;
  uint32_t off;
  int fd = -1;
  int ret;

  if (g_staging_info.state == VG_OTA_STATE_READY_AWAITING_CONFIRM ||
      g_staging_info.state == VG_OTA_STATE_INSTALL_PENDING ||
      g_staging_info.state == VG_OTA_STATE_REBOOTING)
    {
      if (err_out && err_len) snprintf(err_out, err_len, "ota_busy");
      return -EBUSY;
    }

  if (m == NULL || source_read == NULL)
    {
      snprintf(g_staging_info.error_code, sizeof(g_staging_info.error_code), "invalid_args");
      g_staging_info.state = VG_OTA_STATE_FAILED;
      return -EINVAL;
    }

  if (vg_ota_staging_is_suppressed(m->task_id))
    {
      if (err_out && err_len)
        {
          snprintf(err_out, err_len, "task_suppressed");
        }
      return -EALREADY;
    }

  memset(&g_staging_info, 0, sizeof(g_staging_info));
  g_staging_info.state = VG_OTA_STATE_OFFER_RECEIVED;
  snprintf(g_staging_info.task_id, sizeof(g_staging_info.task_id), "%s", m->task_id);
  snprintf(g_staging_info.version, sizeof(g_staging_info.version), "%s", m->version);
  g_staging_info.image_size = m->image_size;
  memcpy(g_staging_info.sha256, m->sha256, 32);

  /* 1. Space pre-check */
  ret = vg_ota_staging_check_space(m->image_size, &free_b, &req_b);
  g_staging_info.free_bytes = free_b;
  g_staging_info.required_bytes = req_b;
  if (ret != 0)
    {
      snprintf(g_staging_info.error_code, sizeof(g_staging_info.error_code), "insufficient_space");
      g_staging_info.state = VG_OTA_STATE_FAILED;
      record_state_file("FAILED", m->task_id, m->version, m->image_size, m->sha256, "insufficient_space");
      if (err_out && err_len) snprintf(err_out, err_len, "insufficient_space (avail=%" PRIu64 " < req=%" PRIu64 ")", free_b, req_b);
      return ret;
    }

  /* 2. Preliminary manifest validate (without digest yet) */
  ret = vg_ota_manifest_validate(m, NULL, g_staging_info.error_code, sizeof(g_staging_info.error_code));
  if (ret != 0)
    {
      g_staging_info.state = VG_OTA_STATE_FAILED;
      record_state_file("FAILED", m->task_id, m->version, m->image_size, m->sha256, g_staging_info.error_code);
      if (err_out && err_len) snprintf(err_out, err_len, "%s", g_staging_info.error_code);
      return ret;
    }

  /* 3. Ensure staging directory exists */
  if (mkdir(g_staging_dir, 0755) < 0 && errno != EEXIST)
    {
      snprintf(g_staging_info.error_code, sizeof(g_staging_info.error_code), "mkdir_failed");
      g_staging_info.state = VG_OTA_STATE_FAILED;
      return -errno;
    }

  snprintf(tmp_path, sizeof(tmp_path), "%s/%s", g_staging_dir, VG_OTA_IMAGE_TMP_NAME);
  snprintf(bin_path, sizeof(bin_path), "%s/%s", g_staging_dir, VG_OTA_IMAGE_BIN_NAME);
  snprintf(manifest_path, sizeof(manifest_path), "%s/%s", g_staging_dir, VG_OTA_MANIFEST_BIN_NAME);

  /* 4. Stream write into IMAGE.TMP */
  g_staging_info.state = VG_OTA_STATE_DOWNLOADING;
  publish_info();
  fd = open(tmp_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
  if (fd < 0)
    {
      snprintf(g_staging_info.error_code, sizeof(g_staging_info.error_code), "open_tmp_failed");
      g_staging_info.state = VG_OTA_STATE_FAILED;
      return -errno;
    }

  buf = malloc(4096);
  if (buf == NULL)
    {
      close(fd);
      unlink(tmp_path);
      snprintf(g_staging_info.error_code, sizeof(g_staging_info.error_code), "nomem");
      g_staging_info.state = VG_OTA_STATE_FAILED;
      return -ENOMEM;
    }

  vg_ota_sha256_init(&sha);
  for (off = 0; off < m->image_size; )
    {
      uint32_t chunk = m->image_size - off;
      ssize_t nread;
      ssize_t nwritten;
      size_t done = 0;

      if (chunk > 4096)
        {
          chunk = 4096;
        }

      nread = source_read(source_ctx, off, buf, chunk);
      if (nread != (ssize_t)chunk)
        {
          close(fd);
          unlink(tmp_path);
          free(buf);
          snprintf(g_staging_info.error_code, sizeof(g_staging_info.error_code), "source_read_failed");
          g_staging_info.state = VG_OTA_STATE_FAILED;
          return -EIO;
        }

      while (done < (size_t)chunk)
        {
          nwritten = write(fd, buf + done, chunk - done);
          if (nwritten <= 0)
            {
              close(fd);
              unlink(tmp_path);
              free(buf);
              snprintf(g_staging_info.error_code, sizeof(g_staging_info.error_code), "write_tmp_failed");
              g_staging_info.state = VG_OTA_STATE_FAILED;
              return -EIO;
            }

          done += (size_t)nwritten;
        }

      vg_ota_sha256_update(&sha, buf, chunk);
      off += chunk;
      g_staging_info.downloaded_bytes = off;
      publish_info();
    }
  vg_ota_sha256_final(&sha, calc_digest);
  if (memcmp(calc_digest, m->sha256, 32))
    {
      close(fd); unlink(tmp_path); free(buf);
      vg_ota_staging_set_state(VG_OTA_STATE_FAILED, "stream_digest_mismatch");
      if (err_out && err_len) snprintf(err_out, err_len, "digest_mismatch");
      return -EBADMSG;
    }

  if (fsync(fd) < 0)
    {
      close(fd);
      unlink(tmp_path);
      free(buf);
      snprintf(g_staging_info.error_code, sizeof(g_staging_info.error_code), "fsync_tmp_failed");
      g_staging_info.state = VG_OTA_STATE_FAILED;
      return -EIO;
    }

  close(fd);

  /* 5. Read back IMAGE.TMP and verify SHA-256 + vector table */
  g_staging_info.state = VG_OTA_STATE_VERIFYING;
  publish_info();
  fd = open(tmp_path, O_RDONLY);
  if (fd < 0)
    {
      unlink(tmp_path);
      free(buf);
      snprintf(g_staging_info.error_code, sizeof(g_staging_info.error_code), "readback_open_failed");
      g_staging_info.state = VG_OTA_STATE_FAILED;
      return -EIO;
    }

  if (read(fd, vector, 8) != 8 || !vg_ota_image_vector_valid(vector, m->image_size))
    {
      close(fd);
      unlink(tmp_path);
      free(buf);
      snprintf(g_staging_info.error_code, sizeof(g_staging_info.error_code), "vector_table_invalid");
      g_staging_info.state = VG_OTA_STATE_FAILED;
      if (err_out && err_len) snprintf(err_out, err_len, "vector_table_invalid");
      return -EINVAL;
    }

  lseek(fd, 0, SEEK_SET);
  vg_ota_sha256_init(&sha);
  for (off = 0; off < m->image_size; )
    {
      uint32_t chunk = m->image_size - off;
      ssize_t got;

      if (chunk > 4096)
        {
          chunk = 4096;
        }

      got = read(fd, buf, chunk);
      if (got != (ssize_t)chunk)
        {
          close(fd);
          unlink(tmp_path);
          free(buf);
          snprintf(g_staging_info.error_code, sizeof(g_staging_info.error_code), "readback_read_failed");
          g_staging_info.state = VG_OTA_STATE_FAILED;
          return -EIO;
        }

      vg_ota_sha256_update(&sha, buf, (uint32_t)got);
      off += chunk;
    }

  vg_ota_sha256_final(&sha, calc_digest);
  close(fd);
  free(buf);

  /* 6. Validate manifest against calculated digest */
  ret = vg_ota_manifest_validate(m, calc_digest, g_staging_info.error_code,
                                sizeof(g_staging_info.error_code));
  if (ret != 0)
    {
      unlink(tmp_path);
      g_staging_info.state = VG_OTA_STATE_FAILED;
      record_state_file("FAILED", m->task_id, m->version, m->image_size, m->sha256, g_staging_info.error_code);
      if (err_out && err_len) snprintf(err_out, err_len, "%s", g_staging_info.error_code);
      return ret;
    }

  /* 7. Atomic rename: IMAGE.TMP -> IMAGE.BIN */
  if (rename(tmp_path, bin_path) < 0)
    {
      unlink(tmp_path);
      snprintf(g_staging_info.error_code, sizeof(g_staging_info.error_code), "rename_failed");
      g_staging_info.state = VG_OTA_STATE_FAILED;
      return -errno;
    }

  /* 8. Write MANIFEST.BIN */
  ret = vg_ota_manifest_encode(m, manifest_buf);
  if (ret != 0)
    {
      unlink(bin_path);
      snprintf(g_staging_info.error_code, sizeof(g_staging_info.error_code), "manifest_encode_failed");
      g_staging_info.state = VG_OTA_STATE_FAILED;
      return ret;
    }

  fd = open(manifest_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
  if (fd < 0 || write(fd, manifest_buf, sizeof(manifest_buf)) != sizeof(manifest_buf) ||
      fsync(fd) < 0)
    {
      if (fd >= 0) close(fd);
      unlink(bin_path);
      unlink(manifest_path);
      snprintf(g_staging_info.error_code, sizeof(g_staging_info.error_code), "write_manifest_failed");
      g_staging_info.state = VG_OTA_STATE_FAILED;
      return -EIO;
    }

  close(fd);

  /* 9. Success: Enter READY_AWAITING_CONFIRM (Do NOT write pending BCB!) */
  g_ready_since_ms = monotonic_ms();
  g_staging_info.state = VG_OTA_STATE_READY_AWAITING_CONFIRM;
  g_staging_info.error_code[0] = '\0';
  record_state_file("READY_AWAITING_CONFIRM", m->task_id, m->version, m->image_size, m->sha256, "none");

  return 0;
}

static int vg_ota_staging_get_info_locked(struct vg_ota_staging_info *info)
{
  if (info == NULL)
    {
      return -EINVAL;
    }

  pthread_mutex_lock(&g_info_lock);
  *info = g_published_info;
  pthread_mutex_unlock(&g_info_lock);
  return 0;
}

static int vg_ota_staging_set_state_locked(uint32_t state, const char *err_code)
{
  g_staging_info.state = state;
  if (err_code != NULL)
    {
      snprintf(g_staging_info.error_code, sizeof(g_staging_info.error_code), "%s", err_code);
    }

  record_state_file(vg_ota_state_name(state), g_staging_info.task_id,
                    g_staging_info.version, g_staging_info.image_size,
                    g_staging_info.sha256, g_staging_info.error_code);
  return 0;
}

static int vg_ota_staging_set_error_locked(const char *err_code)
{
  if (err_code != NULL)
    {
      snprintf(g_staging_info.error_code, sizeof(g_staging_info.error_code), "%s", err_code);
    }

  return 0;
}

static int vg_ota_staging_load_manifest_locked(struct vg_ota_manifest *m)
{
  char path[160];
  uint8_t buf[VG_OTA_MANIFEST_BYTES];
  int fd;

  if (m == NULL)
    {
      return -EINVAL;
    }

  snprintf(path, sizeof(path), "%s/%s", g_staging_dir, VG_OTA_MANIFEST_BIN_NAME);
  fd = open(path, O_RDONLY);
  if (fd < 0)
    {
      return -errno;
    }

  if (read(fd, buf, sizeof(buf)) != sizeof(buf))
    {
      close(fd);
      return -EIO;
    }

  close(fd);
  return vg_ota_manifest_decode(buf, sizeof(buf), m);
}

static int vg_ota_staging_calc_image_sha256_locked(uint8_t out[32])
{
  char path[160];
  uint8_t *buf;
  struct vg_ota_sha256 sha;
  ssize_t n;
  int fd;

  if (out == NULL)
    {
      return -EINVAL;
    }

  snprintf(path, sizeof(path), "%s/%s", g_staging_dir, VG_OTA_IMAGE_BIN_NAME);
  fd = open(path, O_RDONLY);
  if (fd < 0)
    {
      return -errno;
    }

  buf = malloc(4096);
  if (buf == NULL)
    {
      close(fd);
      return -ENOMEM;
    }

  vg_ota_sha256_init(&sha);
  while ((n = read(fd, buf, 4096)) > 0)
    {
      vg_ota_sha256_update(&sha, buf, (uint32_t)n);
    }

  free(buf);
  close(fd);

  if (n < 0)
    {
      return -EIO;
    }

  vg_ota_sha256_final(&sha, out);
  return 0;
}

static int vg_ota_staging_verify_digest_locked(void)
{
  struct vg_ota_manifest m;
  uint8_t actual_digest[32];
  char path[160];
  struct stat st;
  int ret;

  ret = vg_ota_staging_load_manifest(&m);
  if (ret != 0)
    {
      return ret;
    }

  snprintf(path, sizeof(path), "%s/%s", g_staging_dir, VG_OTA_IMAGE_BIN_NAME);
  if (stat(path, &st) != 0 || st.st_size != m.image_size) return -EBADMSG;
  ret = vg_ota_staging_calc_image_sha256(actual_digest);
  if (ret != 0)
    {
      return ret;
    }

  if (memcmp(m.sha256, actual_digest, 32) != 0)
    {
      return -EBADMSG;
    }

  return 0;
}

static int finish_offer(const char *task_id, uint32_t state, const char *reason)
{
  char path[160];
  int ret;
  if (g_staging_info.state != VG_OTA_STATE_READY_AWAITING_CONFIRM ||
      !task_id || strcmp(task_id, g_staging_info.task_id)) return -ESTALE;
  ret = suppress_task(task_id);
  if (ret != 0)
    { vg_ota_staging_set_error("suppress_write_failed"); return ret; }
  snprintf(path, sizeof(path), "%s/%s", g_staging_dir, VG_OTA_IMAGE_BIN_NAME);
  ret = unlink(path);
  if (ret < 0 && errno != ENOENT) return -errno;
  snprintf(path, sizeof(path), "%s/%s", g_staging_dir, VG_OTA_MANIFEST_BIN_NAME);
  ret = unlink(path);
  if (ret < 0 && errno != ENOENT) return -errno;
  vg_ota_staging_set_state(state, reason);
  sync();
  return 0;
}

static int vg_ota_staging_decline_locked(const char *task_id, const char *reason)
{
  return finish_offer(task_id, VG_OTA_STATE_DECLINED,
                      reason ? reason : "user_declined");
}

static int vg_ota_staging_expire_locked(const char *task_id)
{
  return finish_offer(task_id, VG_OTA_STATE_EXPIRED, "offer_expired_timeout");
}

static int vg_ota_staging_clear_locked(void)
{
  char path[160];

  snprintf(path, sizeof(path), "%s/%s", g_staging_dir, VG_OTA_IMAGE_TMP_NAME);
  unlink(path);

  snprintf(path, sizeof(path), "%s/%s", g_staging_dir, VG_OTA_IMAGE_BIN_NAME);
  unlink(path);

  snprintf(path, sizeof(path), "%s/%s", g_staging_dir, VG_OTA_MANIFEST_BIN_NAME);
  unlink(path);

  memset(&g_staging_info, 0, sizeof(g_staging_info));
  g_staging_info.state = VG_OTA_STATE_IDLE;
  record_state_file("IDLE", "", "", 0, NULL, "cleared");
  return 0;
}

int vg_ota_staging_stage(const struct vg_ota_manifest *m, vg_ota_source_read_fn source_read, void *source_ctx, char *err_out, size_t err_len)
{
  int ret;
  vg_ota_staging_lock();
  ret = vg_ota_staging_stage_locked(m, source_read, source_ctx, err_out, err_len);
  if(ret && err_out && err_len && !err_out[0])
    snprintf(err_out,err_len,"%s",g_staging_info.error_code[0] ? g_staging_info.error_code : "staging_failed");
  publish_info();
  vg_ota_staging_unlock();
  return ret;
}

int vg_ota_staging_get_info(struct vg_ota_staging_info *info)
{
  int ret;
  ret = vg_ota_staging_get_info_locked(info);
  return ret;
}

int vg_ota_staging_set_state(uint32_t state, const char *err_code)
{
  int ret;
  vg_ota_staging_lock();
  ret = vg_ota_staging_set_state_locked(state, err_code);
  publish_info();
  vg_ota_staging_unlock();
  return ret;
}

int vg_ota_staging_set_error(const char *err_code)
{
  int ret;
  vg_ota_staging_lock();
  ret = vg_ota_staging_set_error_locked(err_code);
  publish_info();
  vg_ota_staging_unlock();
  return ret;
}

int vg_ota_staging_load_manifest(struct vg_ota_manifest *m)
{
  int ret;
  vg_ota_staging_lock();
  ret = vg_ota_staging_load_manifest_locked(m);
  vg_ota_staging_unlock();
  return ret;
}

int vg_ota_staging_calc_image_sha256(uint8_t out[32])
{
  int ret;
  vg_ota_staging_lock();
  ret = vg_ota_staging_calc_image_sha256_locked(out);
  vg_ota_staging_unlock();
  return ret;
}

int vg_ota_staging_verify_digest(void)
{
  int ret;
  vg_ota_staging_lock();
  ret = vg_ota_staging_verify_digest_locked();
  vg_ota_staging_unlock();
  return ret;
}

int vg_ota_staging_decline(const char *task_id, const char *reason)
{
  int ret;
  vg_ota_staging_lock();
  ret = vg_ota_staging_decline_locked(task_id, reason);
  vg_ota_staging_unlock();
  return ret;
}

int vg_ota_staging_expire(const char *task_id)
{
  int ret;
  vg_ota_staging_lock();
  ret = vg_ota_staging_expire_locked(task_id);
  vg_ota_staging_unlock();
  return ret;
}

int vg_ota_staging_clear(void)
{
  int ret;
  vg_ota_staging_lock();
  ret = vg_ota_staging_clear_locked();
  publish_info();
  vg_ota_staging_unlock();
  return ret;
}

bool vg_ota_staging_is_suppressed(const char *task_id)
{
  bool ret;
  vg_ota_staging_lock();
  ret = vg_ota_staging_is_suppressed_locked(task_id);
  vg_ota_staging_unlock();
  return ret;
}

int vg_ota_staging_remember(const char *task_id)
{
  if(!task_id || !task_id[0] || strnlen(task_id,40)>=40) return -EINVAL;
  vg_ota_staging_lock();
  int ret=suppress_task(task_id);
  vg_ota_staging_unlock();
  return ret;
}
int vg_ota_staging_fail(const char *task_id,const char *version,const char *reason)
{
  char path[160];
  int ret;
  if(!task_id || !version || !reason || strnlen(task_id,40)>=40 || strnlen(version,32)>=32) return -EINVAL;
  vg_ota_staging_lock();
  if(g_staging_info.state==VG_OTA_STATE_READY_AWAITING_CONFIRM ||
      g_staging_info.state==VG_OTA_STATE_INSTALL_PENDING || g_staging_info.state==VG_OTA_STATE_REBOOTING) {
    vg_ota_staging_unlock(); return -EBUSY;
  }
  ret=suppress_task(task_id);
  if(!ret) {
    const char *names[]={VG_OTA_IMAGE_TMP_NAME,VG_OTA_IMAGE_BIN_NAME,VG_OTA_MANIFEST_BIN_NAME};
    for(unsigned i=0;i<3;i++) {
      snprintf(path,sizeof(path),"%s/%s",g_staging_dir,names[i]);
      if(unlink(path) && errno!=ENOENT) ret=-errno;
    }
  }
  if(strcmp(g_staging_info.task_id,task_id)) memset(&g_staging_info,0,sizeof(g_staging_info));
  snprintf(g_staging_info.task_id,sizeof(g_staging_info.task_id),"%s",task_id);
  snprintf(g_staging_info.version,sizeof(g_staging_info.version),"%s",version);
  vg_ota_staging_set_state(VG_OTA_STATE_FAILED,reason);
  vg_ota_staging_unlock();
  return ret;
}
