/****************************************************************************
 * NSH: vgota - read-only OTA hardware readiness probe.
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/statfs.h>
#include <unistd.h>

#include <nuttx/fs/ioctl.h>
#include <nuttx/mmcsd.h>
#include <nuttx/board.h>

#include "vg_fw_identity.h"
#include "vg_ota_bcb.h"
#include "vg_ota_boot.h"
#include "vg_ota_fat.h"
#include "vg_ota_probe.h"
#include "vg_ota_reboot.h"
#include "vg_ota_mbox.h"
#include "vg_ota_manifest.h"
#include "vg_ota_sha256.h"
#include "vg_ota_install.h"
#include "vg_ota_self_check.h"
#include "vg_device_id.h"

#define VG_EMMC_DEV   "/dev/mmcsd0"
#define VG_EMMC_MOUNT "/mnt/emmc"

static void usage(void)
{
  printf("Usage:\n");
  printf("  vgota identity\n");
  printf("  vgota bootinfo\n");
  printf("  vgota reboot\n");
  printf("  vgota selfcheck [--run]\n");
  printf("  vgota confirm\n");
  printf("  vgota rollback [reason]\n");
  printf("  vgota probe\n");
  printf("  vgota qspiprobe\n");
  printf("  vgota qspiprobe --confirm\n");
#ifdef CONFIG_VG_OTA_TEST_FIXTURES
  printf("  vgota fixture status\n");
  printf("  vgota fixture write <NAME> <bytes> [seed]\n");
  printf("  vgota fixture clear\n");
  printf("  vgota fixture bcb status\n");
  printf("  vgota fixture bcb set <0|1> <state> <a|b> <seq> <size> [ver] [task]\n");
  printf("  vgota fixture bcb corrupt <0|1> <offset>\n");
  printf("  vgota fixture bcb clear\n");
  printf("  vgota fixture stage <bytes> <seq> [task]\n");
  printf("  vgota fixture arm\n");
  printf("  vgota fixture fail <on|off|status>\n");
#endif
}

static void print_identity(void)
{
  printf("vgota: identity version=%s slot=%s build=%s link=%08" PRIx32 "\n",
         vg_fw_version(), vg_fw_slot_name(), vg_fw_build_id(),
         vg_fw_link_base());
}

static void print_bootinfo(void)
{
  volatile struct vg_ota_mbox *mbox = VG_OTA_MBOX;

  if (mbox->magic != VG_OTA_MBOX_MAGIC ||
      mbox->version != VG_OTA_MBOX_VERSION)
    {
      printf("vgota: bootinfo unavailable\n");
      return;
    }

  printf("vgota: bootinfo slot=%s reason=%s bcb=%" PRIu32
         " state=%s seq=%" PRIu32 " bcb_slot=%s"
         " valid0=%" PRIu32 " valid1=%" PRIu32
         " bcb_error=%" PRIu32 " fat=%s fat_error=%" PRIu32
         " emmc_error=%" PRIu32 " trials=%" PRIu32 "\n",
         vg_ota_bcb_slot_name(mbox->boot_slot),
         vg_ota_boot_reason_name(mbox->boot_reason),
         mbox->boot_bcb_which,
         vg_ota_bcb_state_name(mbox->boot_bcb_state),
         mbox->boot_bcb_seq,
         vg_ota_bcb_slot_name(mbox->boot_bcb_slot),
         mbox->boot_valid0, mbox->boot_valid1,
         mbox->boot_bcb_error,
         vg_ota_fat_type_name((enum vg_ota_fat_type_e)mbox->boot_fat_type),
         mbox->boot_fat_error, mbox->boot_emmc_error,
         mbox->boot_bcb_trials);
  printf("vgota: install stage=%" PRIu32 " error=%" PRIu32 " bytes=%" PRIu32 "\n",
         mbox->install_stage,mbox->install_error,mbox->install_bytes);
}

static void print_hex(const char *label, const uint8_t *buf, size_t len)
{
  size_t i;

  printf("vgota: %s=", label);
  for (i = 0; i < len; i++)
    {
      printf("%02x", buf[i]);
    }

  printf("\n");
}

static const char *mbox_state_name(uint32_t state)
{
  switch (state)
    {
      case VG_OTA_MBOX_IDLE:
        return "idle";
      case VG_OTA_MBOX_REQUESTED:
        return "requested";
      case VG_OTA_MBOX_RUNNING:
        return "running";
      case VG_OTA_MBOX_OK:
        return "ok";
      case VG_OTA_MBOX_FAIL:
        return "fail";
      default:
        return "unknown";
    }
}

static void print_qspi_result(void)
{
  volatile struct vg_ota_mbox *mbox = VG_OTA_MBOX;

  if (mbox->magic != VG_OTA_MBOX_MAGIC ||
      mbox->version != VG_OTA_MBOX_VERSION)
    {
      printf("vgota: qspi no result (run: vgota qspiprobe --confirm)\n");
      return;
    }

  printf("vgota: qspi state=%s error=%" PRIu32
         " jedec1=%06" PRIx32 " jedec2=%06" PRIx32
         " capacity=%" PRIu32 " (%" PRIu32 " MiB)"
         " erase=%" PRIu32 " write=%" PRIu32
         " seq=%" PRIu32 " run=%" PRIu32
         " sr=%08" PRIx32 " cr=%08" PRIx32
         " ccr=%08" PRIx32 " dcr=%08" PRIx32
         " raw1=%06" PRIx32 " raw2=%06" PRIx32
         " fi=%" PRIu32 " fg=%02" PRIx32 " fw=%02" PRIx32 "\n",
         mbox_state_name(mbox->state), mbox->error,
         mbox->jedec1, mbox->jedec2, mbox->capacity,
         mbox->capacity / (1024u * 1024u),
         mbox->erase_size, mbox->write_size,
         mbox->seq, mbox->run_seq,
         mbox->sr, mbox->cr, mbox->ccr, mbox->dcr,
         mbox->raw1, mbox->raw2,
         mbox->fail_index, mbox->fail_got, mbox->fail_want);

  if (mbox->state == VG_OTA_MBOX_REQUESTED ||
      mbox->state == VG_OTA_MBOX_RUNNING)
    {
      printf("vgota: qspi probe pending; reset the board to run it\n");
    }
}

static int cmd_qspiprobe(int argc, char *argv[])
{
  volatile struct vg_ota_mbox *mbox = VG_OTA_MBOX;

  if (argc == 3 && strcmp(argv[2], "--confirm") == 0)
    {
      uint32_t seq = 1;

      if (mbox->magic == VG_OTA_MBOX_MAGIC &&
          mbox->version == VG_OTA_MBOX_VERSION)
        {
          seq = mbox->seq + 1;
        }

      mbox->magic = VG_OTA_MBOX_MAGIC;
      mbox->version = VG_OTA_MBOX_VERSION;
      mbox->request = VG_OTA_MBOX_REQ_QSPI_PROBE;
      mbox->state = VG_OTA_MBOX_REQUESTED;
      mbox->error = 0;
      mbox->seq = seq;

      __asm__ volatile ("dsb 0xf" ::: "memory");

      printf("vgota: qspi probe requested, rebooting\n");
      board_reset(0);
      printf("vgota: board reset failed\n");
      return 1;
    }

  if (argc != 2)
    {
      usage();
      return 1;
    }

  print_qspi_result();
  return 0;
}

#ifdef CONFIG_VG_OTA_TEST_FIXTURES
#define VG_OTA_DIR              "/mnt/emmc/VGOTA"
#define VG_OTA_MAX_FIXTURE_SIZE (8u * 1024u * 1024u)

static const char *g_fixture_names[] =
{
  "IMAGE.BIN",
  "IMAGE.TMP",
  "MANIFEST.BIN",
  "BCB0.BIN",
  "BCB1.BIN",
  "STATE.BIN"
};

static int fixture_name_ok(const char *name)
{
  size_t i;

  for (i = 0; i < sizeof(g_fixture_names) / sizeof(g_fixture_names[0]); i++)
    {
      if (strcmp(name, g_fixture_names[i]) == 0)
        {
          return 1;
        }
    }

  return 0;
}

static int fixture_write(const char *name, uint32_t size, uint32_t seed)
{
  char path[64];
  uint8_t buf[256];
  uint32_t off;
  int fd;

  if (fixture_name_ok(name) == 0)
    {
      printf("vgota: fixture invalid name %s\n", name);
      return 1;
    }

  if (size > VG_OTA_MAX_FIXTURE_SIZE)
    {
      printf("vgota: fixture size %" PRIu32 " exceeds %u\n",
             size, VG_OTA_MAX_FIXTURE_SIZE);
      return 1;
    }

  if (mkdir(VG_OTA_DIR, 0755) < 0 && errno != EEXIST)
    {
      printf("vgota: fixture mkdir %s ERR=%d\n", VG_OTA_DIR, errno);
      return 1;
    }

  snprintf(path, sizeof(path), "%s/%s", VG_OTA_DIR, name);
  fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
  if (fd < 0)
    {
      printf("vgota: fixture open %s ERR=%d\n", path, errno);
      return 1;
    }

  for (off = 0; off < size; )
    {
      uint32_t chunk = size - off;
      uint32_t done = 0;
      uint32_t i;

      if (chunk > sizeof(buf))
        {
          chunk = sizeof(buf);
        }

      for (i = 0; i < chunk; i++)
        {
          buf[i] = (uint8_t)(seed + off + i);
        }

      while (done < chunk)
        {
          ssize_t written = write(fd, buf + done, chunk - done);
          if (written <= 0)
            {
              printf("vgota: fixture write %s ERR=%d\n", path, errno);
              close(fd);
              return 1;
            }

          done += (uint32_t)written;
        }

      off += chunk;
    }

  if (fsync(fd) < 0)
    {
      printf("vgota: fixture fsync %s ERR=%d\n", path, errno);
      close(fd);
      return 1;
    }

  close(fd);
  printf("vgota: fixture write %s size=%" PRIu32 " seed=%" PRIu32 "\n",
         name, size, seed);
  return 0;
}

static int fixture_status(void)
{
  size_t i;
  int ret = 0;

  for (i = 0; i < sizeof(g_fixture_names) / sizeof(g_fixture_names[0]); i++)
    {
      char path[64];
      struct stat st;

      snprintf(path, sizeof(path), "%s/%s", VG_OTA_DIR, g_fixture_names[i]);
      if (stat(path, &st) == 0)
        {
          printf("vgota: fixture %s present size=%ld\n",
                 g_fixture_names[i], (long)st.st_size);
        }
      else if (errno == ENOENT || errno == ENOTDIR)
        {
          printf("vgota: fixture %s missing\n", g_fixture_names[i]);
        }
      else
        {
          printf("vgota: fixture %s ERR=%d\n", g_fixture_names[i], errno);
          ret = 1;
        }
    }

  return ret;
}

static int fixture_clear(void)
{
  size_t i;
  int ret = 0;

  for (i = 0; i < sizeof(g_fixture_names) / sizeof(g_fixture_names[0]); i++)
    {
      char path[64];

      snprintf(path, sizeof(path), "%s/%s", VG_OTA_DIR, g_fixture_names[i]);
      if (unlink(path) < 0 && errno != ENOENT)
        {
          printf("vgota: fixture unlink %s ERR=%d\n", path, errno);
          ret = 1;
        }
    }

  printf("vgota: fixture clear done\n");
  return ret;
}

static int fixture_store(const char *name, const uint8_t *data,
                         uint32_t size)
{
  char path[64];
  uint32_t done = 0;
  int fd;

  if (mkdir(VG_OTA_DIR, 0755) < 0 && errno != EEXIST)
    {
      printf("vgota: fixture mkdir %s ERR=%d\n", VG_OTA_DIR, errno);
      return 1;
    }

  snprintf(path, sizeof(path), "%s/%s", VG_OTA_DIR, name);
  fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
  if (fd < 0)
    {
      printf("vgota: fixture open %s ERR=%d\n", path, errno);
      return 1;
    }

  while (done < size)
    {
      ssize_t written = write(fd, data + done, size - done);

      if (written <= 0)
        {
          printf("vgota: fixture write %s ERR=%d\n", path, errno);
          close(fd);
          return 1;
        }

      done += (uint32_t)written;
    }

  if (fsync(fd) < 0)
    {
      printf("vgota: fixture fsync %s ERR=%d\n", path, errno);
      close(fd);
      return 1;
    }

  close(fd);
  return 0;
}

static int fixture_read(const char *name, uint8_t *data, uint32_t size)
{
  char path[64];
  uint32_t done = 0;
  int fd;

  snprintf(path, sizeof(path), "%s/%s", VG_OTA_DIR, name);
  fd = open(path, O_RDONLY);
  if (fd < 0)
    {
      return -errno;
    }

  while (done < size)
    {
      ssize_t got = read(fd, data + done, size - done);

      if (got == 0)
        {
          close(fd);
          return -EINVAL;
        }

      if (got < 0)
        {
          close(fd);
          return -EIO;
        }

      done += (uint32_t)got;
    }

  close(fd);
  return 0;
}

static int fixture_bcb_index(const char *arg, uint32_t *index,
                             const char **name)
{
  char *end;
  unsigned long value;

  errno = 0;
  value = strtoul(arg, &end, 0);
  if (errno != 0 || *end != '\0' || value > 1)
    {
      printf("vgota: fixture bcb invalid index %s\n", arg);
      return 1;
    }

  *index = (uint32_t)value;
  *name = value == 0 ? "BCB0.BIN" : "BCB1.BIN";
  return 0;
}

static int fixture_bcb_state(const char *arg, uint32_t *state)
{
  if (strcmp(arg, "pending") == 0)
    {
      *state = VG_OTA_BCB_STATE_PENDING;
    }
  else if (strcmp(arg, "trial") == 0)
    {
      *state = VG_OTA_BCB_STATE_TRIAL;
    }
  else if (strcmp(arg, "confirmed") == 0)
    {
      *state = VG_OTA_BCB_STATE_CONFIRMED;
    }
  else if (strcmp(arg, "failed") == 0)
    {
      *state = VG_OTA_BCB_STATE_FAILED;
    }
  else
    {
      printf("vgota: fixture bcb invalid state %s\n", arg);
      return 1;
    }

  return 0;
}

static int fixture_bcb_slot(const char *arg, uint32_t *slot)
{
  if (strcmp(arg, "a") == 0 || strcmp(arg, "A") == 0 ||
      strcmp(arg, "0") == 0)
    {
      *slot = VG_OTA_BCB_SLOT_A;
      return 0;
    }

  if (strcmp(arg, "b") == 0 || strcmp(arg, "B") == 0 ||
      strcmp(arg, "1") == 0)
    {
      *slot = VG_OTA_BCB_SLOT_B;
      return 0;
    }

  printf("vgota: fixture bcb invalid slot %s\n", arg);
  return 1;
}

static int parse_u32(const char *arg, uint32_t *out)
{
  char *end;
  unsigned long value;

  errno = 0;
  value = strtoul(arg, &end, 0);
  if (errno != 0 || *end != '\0' || value > 0xfffffffful)
    {
      printf("vgota: fixture bcb invalid number %s\n", arg);
      return 1;
    }

  *out = (uint32_t)value;
  return 0;
}

static int fixture_bcb_status(void)
{
  static const char *names[2] = { "BCB0.BIN", "BCB1.BIN" };
  uint8_t buf[VG_OTA_BCB_FILE_BYTES];
  uint32_t i;

  for (i = 0; i < 2; i++)
    {
      struct vg_ota_bcb bcb;
      int ret = fixture_read(names[i], buf, sizeof(buf));

      if (ret < 0)
        {
          if (ret == -ENOENT)
            {
              printf("vgota: fixture %s missing ERR=%d\n", names[i], -ret);
            }
          else
            {
              printf("vgota: fixture %s invalid ERR=%d\n", names[i], -ret);
            }

          continue;
        }

      ret = vg_ota_bcb_decode(buf, sizeof(buf), &bcb);
      if (ret != 0)
        {
          printf("vgota: fixture %s invalid ERR=%d\n", names[i], -ret);
          continue;
        }

      printf("vgota: fixture %s valid seq=%" PRIu32 " state=%s slot=%s"
             " size=%" PRIu32 " version=%s task=%s trials=%" PRIu32 "\n",
             names[i], bcb.seq, vg_ota_bcb_state_name(bcb.state),
             vg_ota_bcb_slot_name(bcb.slot), bcb.image_size, bcb.version,
             bcb.task_id, bcb.trial_count);
    }

  return 0;
}

static int fixture_bcb_set(int argc, char *argv[])
{
  struct vg_ota_bcb bcb;
  uint8_t buf[VG_OTA_BCB_FILE_BYTES];
  const char *version = "0.2.0";
  const char *task = "FIXTURE";
  const char *name;
  uint32_t index;
  uint32_t i;
  int ret;

  if (argc < 9 || argc > 11)
    {
      usage();
      return 1;
    }

  if (fixture_bcb_index(argv[4], &index, &name) != 0)
    {
      return 1;
    }

  (void)index;
  memset(&bcb, 0, sizeof(bcb));

  if (fixture_bcb_state(argv[5], &bcb.state) != 0 ||
      fixture_bcb_slot(argv[6], &bcb.slot) != 0 ||
      parse_u32(argv[7], &bcb.seq) != 0 ||
      parse_u32(argv[8], &bcb.image_size) != 0)
    {
      return 1;
    }

  if (argc >= 10)
    {
      version = argv[9];
    }

  if (argc >= 11)
    {
      task = argv[10];
    }

  if (strlen(version) >= sizeof(bcb.version) ||
      strlen(task) >= sizeof(bcb.task_id))
    {
      printf("vgota: fixture bcb version or task too long\n");
      return 1;
    }

  snprintf(bcb.version, sizeof(bcb.version), "%s", version);
  snprintf(bcb.task_id, sizeof(bcb.task_id), "%s", task);

  for (i = 0; i < sizeof(bcb.sha256); i++)
    {
      bcb.sha256[i] = (uint8_t)(0x5au ^ i);
    }

  ret = vg_ota_bcb_encode(&bcb, buf, sizeof(buf));
  if (ret != 0)
    {
      printf("vgota: fixture bcb encode ERR=%d\n", -ret);
      return 1;
    }

  if (fixture_store(name, buf, sizeof(buf)) != 0)
    {
      return 1;
    }

  printf("vgota: fixture %s seq=%" PRIu32 " state=%s slot=%s"
         " size=%" PRIu32 " version=%s task=%s\n",
         name, bcb.seq, vg_ota_bcb_state_name(bcb.state),
         vg_ota_bcb_slot_name(bcb.slot), bcb.image_size, bcb.version,
         bcb.task_id);
  return 0;
}

static int fixture_bcb_corrupt(int argc, char *argv[])
{
  uint8_t buf[VG_OTA_BCB_FILE_BYTES];
  const char *name;
  uint32_t index;
  uint32_t offset;
  int ret;

  if (argc != 6)
    {
      usage();
      return 1;
    }

  if (fixture_bcb_index(argv[4], &index, &name) != 0 ||
      parse_u32(argv[5], &offset) != 0)
    {
      return 1;
    }

  (void)index;

  if (offset >= VG_OTA_BCB_HEADER_BYTES)
    {
      printf("vgota: fixture bcb offset must be under %u\n",
             VG_OTA_BCB_HEADER_BYTES);
      return 1;
    }

  ret = fixture_read(name, buf, sizeof(buf));
  if (ret < 0)
    {
      printf("vgota: fixture %s missing ERR=%d\n", name, -ret);
      return 1;
    }

  buf[offset] ^= 0x5a;

  if (fixture_store(name, buf, sizeof(buf)) != 0)
    {
      return 1;
    }

  printf("vgota: fixture %s corrupted offset=%" PRIu32 "\n", name, offset);
  return 0;
}

static int fixture_bcb_clear(void)
{
  static const char *names[2] = { "BCB0.BIN", "BCB1.BIN" };
  char path[64];
  uint32_t i;

  for (i = 0; i < 2; i++)
    {
      snprintf(path, sizeof(path), "%s/%s", VG_OTA_DIR, names[i]);
      if (unlink(path) < 0 && errno != ENOENT)
        {
          printf("vgota: fixture unlink %s ERR=%d\n", path, errno);
          return 1;
        }
    }

  printf("vgota: fixture bcb clear done\n");
  return 0;
}

static int cmd_fixture_bcb(int argc, char *argv[])
{
  if (argc == 4 && strcmp(argv[3], "status") == 0)
    {
      return fixture_bcb_status();
    }

  if (argc == 4 && strcmp(argv[3], "clear") == 0)
    {
      return fixture_bcb_clear();
    }

  if (argc >= 5 && strcmp(argv[3], "set") == 0)
    {
      return fixture_bcb_set(argc, argv);
    }

  if (argc == 6 && strcmp(argv[3], "corrupt") == 0)
    {
      return fixture_bcb_corrupt(argc, argv);
    }

  usage();
  return 1;
}

static int fixture_image_hash(uint8_t digest[32])
{
  struct vg_ota_sha256 sha;
  uint8_t buf[512];
  ssize_t n;
  int fd=open(VG_OTA_DIR "/IMAGE.BIN",O_RDONLY);
  if (fd<0) return 1;
  vg_ota_sha256_init(&sha);
  while ((n=read(fd,buf,sizeof(buf)))>0)
    vg_ota_sha256_update(&sha,buf,(uint32_t)n);
  close(fd);
  if (n<0) return 1;
  vg_ota_sha256_final(&sha,digest);
  return 0;
}

/* ST-Link 只把传输副本写到固定测试窗口，B 槽交给 boot stub 安装。 */
static int fixture_stage(int argc, char *argv[])
{
  struct vg_ota_manifest m;
  struct vg_ota_bcb b;
  uint8_t buf[4096];
  uint32_t size, seq, off, n, i;
  volatile const uint8_t *src=(volatile const uint8_t *)0x91000000u;
  int fd;
  if ((argc!=5 && argc!=6) || vg_fw_slot()!='A' ||
      parse_u32(argv[3],&size) || parse_u32(argv[4],&seq) ||
      size<8 || size>VG_OTA_IMAGE_MAX_BYTES) return 1;
  for (i=0; i<8; i++) buf[i]=src[i];
  if (!vg_ota_image_vector_valid(buf,size))
    { printf("vgota: fixture transport vector invalid\n"); return 1; }
  memset(&m,0,sizeof(m));
  snprintf(m.task_id,sizeof(m.task_id),"%s",argc==6 ? argv[5] : "T04-FIXTURE");
  if (argc==6 && strlen(argv[5])>=sizeof(m.task_id)) return 1;
  m.slot=VG_OTA_BCB_SLOT_B; m.image_size=size;
  m.flags=VG_OTA_MANIFEST_FIXTURE; m.issued_at=1; m.expires_at=UINT64_MAX;
  snprintf(m.version,sizeof(m.version),"%s",vg_fw_version());
  snprintf(m.device_id,sizeof(m.device_id),"%s",vg_device_id());
  snprintf(m.model,sizeof(m.model),"%s",VG_OTA_MODEL);
  snprintf(m.key_id,sizeof(m.key_id),"fixture");
  if (mkdir(VG_OTA_DIR,0755)<0 && errno!=EEXIST) return 1;
  fd=open(VG_OTA_DIR "/IMAGE.BIN",O_WRONLY|O_CREAT|O_TRUNC,0644);
  if (fd<0) return 1;
  for (off=0; off<size; off+=n)
    {
      n=size-off; if (n>sizeof(buf)) n=sizeof(buf);
      for (i=0; i<n; i++) buf[i]=src[off+i];
      if (write(fd,buf,n)!=(ssize_t)n) { close(fd); return 1; }
    }
  if (fsync(fd)<0) { close(fd); return 1; }
  close(fd);
  if (fixture_image_hash(m.sha256) || vg_ota_manifest_encode(&m,buf) ||
      fixture_store("MANIFEST.BIN",buf,512)) return 1;
  memset(&b,0,sizeof(b));
  b.slot=VG_OTA_BCB_SLOT_B; b.image_size=size; b.state=VG_OTA_BCB_STATE_FAILED;
  snprintf(b.version,sizeof(b.version),"%s",m.version);
  snprintf(b.task_id,sizeof(b.task_id),"%s",m.task_id);
  memcpy(b.sha256,m.sha256,32);
  b.seq=seq-1u;
  if (vg_ota_bcb_encode(&b,buf,sizeof(buf)) ||
      fixture_store("BCB0.BIN",buf,sizeof(buf))) return 1;
  b.seq=seq-2u;
  if (vg_ota_bcb_encode(&b,buf,sizeof(buf)) ||
      fixture_store("BCB1.BIN",buf,sizeof(buf))) return 1;
  sync();
  printf("vgota: fixture staged bytes=%" PRIu32 " task=%s sha=%02x%02x%02x%02x; not armed\n",
         size,m.task_id,m.sha256[0],m.sha256[1],m.sha256[2],m.sha256[3]);
  return 0;
}

static int fixture_arm(void)
{
  struct vg_ota_manifest m;
  struct vg_ota_bcb b, other;
  uint8_t buf[4096], digest[32];
  uint32_t i, held=0;
  const char *target;
  if (vg_fw_slot()!='A' || fixture_read("MANIFEST.BIN",buf,512) ||
      vg_ota_manifest_decode(buf,512,&m) || m.flags!=VG_OTA_MANIFEST_FIXTURE ||
      fixture_read("BCB0.BIN",buf,4096) || vg_ota_bcb_decode(buf,4096,&b) ||
      fixture_read("BCB1.BIN",buf,4096) || vg_ota_bcb_decode(buf,4096,&other))
    return 1;
  if (vg_ota_bcb_seq_newer(other.seq,b.seq))
    { b=other; target="BCB0.BIN"; }
  else target="BCB1.BIN";
  b.seq++; b.state=VG_OTA_BCB_STATE_PENDING;
  b.flags=VG_OTA_BCB_LOCAL_CONFIRMED; b.trial_count=0;
  if (vg_ota_manifest_bind(&m,&b,vg_device_id(),1)) return 1;
  printf("vgota: fixture local confirmation version=%s task=%s bytes=%" PRIu32 "\n",
         m.version,m.task_id,m.image_size);
  printf("vgota: release board USER button, then hold it for 2 seconds within 60 seconds\n");
  fflush(stdout);
  /* 必须先释放再按下，串口命令和调用方不能代替本机按键事件。 */
  for (i=0; i<600 && board_buttons()!=0; i++) usleep(100000);
  if (i==600) return 1;
  for (i=0; i<600 && held<20; i++)
    {
      held=board_buttons()!=0 ? held+1 : 0;
      usleep(100000);
    }
  if (held<20) { printf("vgota: fixture local confirmation timed out\n"); return 1; }
  if (fixture_image_hash(digest) || memcmp(digest,m.sha256,32) ||
      vg_ota_bcb_encode(&b,buf,sizeof(buf)) ||
      fixture_store(target,buf,sizeof(buf))) return 1;
  sync();
  printf("vgota: fixture pending locally confirmed seq=%" PRIu32 "\n",b.seq);
  return 0;
}

static int cmd_fixture(int argc, char *argv[])
{
  char *end;
  unsigned long size;
  unsigned long seed = 0;

  if (argc>=3 && strcmp(argv[2],"stage")==0) return fixture_stage(argc,argv);
  if (argc==3 && strcmp(argv[2],"arm")==0) return fixture_arm();

  if (argc >= 3 && strcmp(argv[2], "fail") == 0)
    {
      if (argc == 4 && strcmp(argv[3], "on") == 0)
        {
          vg_ota_self_check_set_inject_fail(1);
          printf("vgota: fixture fail injection enabled\n");
          return 0;
        }
      if (argc == 4 && strcmp(argv[3], "off") == 0)
        {
          vg_ota_self_check_set_inject_fail(0);
          printf("vgota: fixture fail injection disabled\n");
          return 0;
        }
      if (argc == 3 || (argc == 4 && strcmp(argv[3], "status") == 0))
        {
          int on = vg_ota_self_check_get_inject_fail();
          printf("vgota: fixture fail injection %s\n", on ? "ON" : "OFF");
          return 0;
        }
      usage();
      return 1;
    }

  if (argc >= 3 && strcmp(argv[2], "bcb") == 0)
    {
      return cmd_fixture_bcb(argc, argv);
    }

  if (argc == 3 && strcmp(argv[2], "status") == 0)
    {
      return fixture_status();
    }

  if (argc == 3 && strcmp(argv[2], "clear") == 0)
    {
      return fixture_clear();
    }

  if ((argc == 5 || argc == 6) && strcmp(argv[2], "write") == 0)
    {
      errno = 0;
      size = strtoul(argv[4], &end, 0);
      if (errno != 0 || *end != '\0')
        {
          printf("vgota: fixture invalid size %s\n", argv[4]);
          return 1;
        }

      if (argc == 6)
        {
          errno = 0;
          seed = strtoul(argv[5], &end, 0);
          if (errno != 0 || *end != '\0')
            {
              printf("vgota: fixture invalid seed %s\n", argv[5]);
              return 1;
            }
        }

      return fixture_write(argv[3], (uint32_t)size, (uint32_t)seed);
    }

  usage();
  return 1;
}
#endif

static int read_mmc_register(int fd, uint32_t opcode, void *buf)
{
  struct mmc_ioc_cmd cmd;
  int ret;

  memset(&cmd, 0, sizeof(cmd));
  cmd.opcode = opcode;
  cmd.blksz = 512;
  cmd.blocks = 1;
  cmd.data_ptr = (uint64_t)(uintptr_t)buf;

  ret = ioctl(fd, MMC_IOC_CMD, (unsigned long)(uintptr_t)&cmd);
  if (ret < 0)
    {
      return -errno;
    }

  return 0;
}

static void probe_emmc(const char *dev)
{
  uint32_t cid[4];
  uint32_t csd[4];
  uint8_t extcsd[VG_OTA_EXT_CSD_BYTES];
  uint8_t bpb[VG_OTA_FAT_BPB_BYTES];
  struct vg_ota_cid_info cid_info;
  struct vg_ota_csd_info csd_info;
  struct vg_ota_extcsd_info extcsd_info;
  struct vg_ota_fat_info fat_info;
  struct geometry geo;
  struct statfs fs;
  ssize_t nread;
  int fd;
  int ret;

  print_identity();

  fd = open(dev, O_RDONLY);
  if (fd < 0)
    {
      printf("vgota: emmc open %s ERR=%d\n", dev, errno);
      return;
    }

  memset(cid, 0, sizeof(cid));
  memset(csd, 0, sizeof(csd));
  memset(extcsd, 0, sizeof(extcsd));
  memset(bpb, 0, sizeof(bpb));
  memset(&geo, 0, sizeof(geo));
  memset(&fs, 0, sizeof(fs));

  ret = read_mmc_register(fd, 2, cid);
  if (ret < 0)
    {
      printf("vgota: emmc cid ERR=%d\n", -ret);
    }
  else if (vg_ota_parse_cid(cid, &cid_info) == 0)
    {
      print_hex("cid", cid_info.raw, sizeof(cid_info.raw));
      printf("vgota: emmc cid mid=%02x oid=%02x pnm=%s prv=%02x "
             "psn=%08" PRIx32 " mdt=%02x crc=%02x\n",
             cid_info.mid, cid_info.oid, cid_info.pnm, cid_info.prv,
             cid_info.psn, cid_info.mdt, cid_info.crc);
    }

  ret = read_mmc_register(fd, 9, csd);
  if (ret < 0)
    {
      printf("vgota: emmc csd ERR=%d\n", -ret);
    }
  else if (vg_ota_parse_csd(csd, &csd_info) == 0)
    {
      print_hex("csd", csd_info.raw, sizeof(csd_info.raw));
      printf("vgota: emmc csd struct=%u spec=%u csize=%u mult=%u "
             "read_bl_len=%u capacity=%" PRIu64 "\n",
             csd_info.structure, csd_info.spec_vers, csd_info.csize,
             csd_info.csize_mult, csd_info.read_bl_len,
             csd_info.capacity_bytes);
    }

  ret = read_mmc_register(fd, 8, extcsd);
  if (ret < 0)
    {
      printf("vgota: emmc extcsd ERR=%d\n", -ret);
    }
  else if (vg_ota_parse_extcsd(extcsd, &extcsd_info) == 0)
    {
      printf("vgota: emmc extcsd rev=%u sec_count=%" PRIu32
             " capacity=%" PRIu64 "\n",
             extcsd_info.rev, extcsd_info.sec_count,
             extcsd_info.capacity_bytes);
    }

  ret = ioctl(fd, BIOC_GEOMETRY, (unsigned long)(uintptr_t)&geo);
  if (ret < 0)
    {
      printf("vgota: emmc geometry ERR=%d\n", errno);
    }
  else
    {
      printf("vgota: emmc geometry sectors=%" PRIu64 " sector_size=%u "
             "capacity=%" PRIu64 " model=%s\n",
             (uint64_t)geo.geo_nsectors, (unsigned)geo.geo_sectorsize,
             (uint64_t)geo.geo_nsectors * (uint64_t)geo.geo_sectorsize,
             geo.geo_model);
    }

  nread = -1;
  if (lseek(fd, 0, SEEK_SET) >= 0)
    {
      nread = read(fd, bpb, sizeof(bpb));
    }

  if (nread != (ssize_t)sizeof(bpb))
    {
      printf("vgota: emmc fat ERR=%d\n", nread < 0 ? errno : EIO);
    }
  else
    {
      ret = vg_ota_parse_fat_bpb(bpb, &fat_info);
      if (ret < 0)
        {
          printf("vgota: emmc fat type=%s ERR=%d\n",
                 vg_ota_fat_type_name(fat_info.type), -ret);
        }
      else
        {
          printf("vgota: emmc fat type=%s bps=%u spc=%u reserved=%u "
                 "fats=%u root_entries=%u clusters=%" PRIu32
                 " total_sectors=%" PRIu32 "\n",
                 vg_ota_fat_type_name(fat_info.type),
                 fat_info.bytes_per_sector, fat_info.sectors_per_cluster,
                 fat_info.reserved_sectors, fat_info.num_fats,
                 fat_info.root_entries, fat_info.cluster_count,
                 fat_info.total_sectors);
        }
    }

  close(fd);

  if (statfs(VG_EMMC_MOUNT, &fs) < 0)
    {
      printf("vgota: emmc statfs %s ERR=%d\n", VG_EMMC_MOUNT, errno);
    }
  else
    {
      printf("vgota: emmc statfs type=%08" PRIx32 " bsize=%u "
             "total=%" PRIu64 " free=%" PRIu64 "\n",
             fs.f_type, (unsigned)fs.f_bsize,
             (uint64_t)fs.f_blocks * (uint64_t)fs.f_bsize,
             (uint64_t)fs.f_bfree * (uint64_t)fs.f_bsize);
    }

  printf("vgota: emmc probe done\n");
  print_qspi_result();
  print_bootinfo();
}

static int cmd_selfcheck(int argc, char *argv[])
{
  struct vg_ota_self_check_status st;
  char detail[128];
  int ret;

  if (argc == 3 && strcmp(argv[2], "--run") == 0)
    {
      ret = vg_ota_self_check_run(detail, sizeof(detail));
      printf("vgota: selfcheck %s: %s\n", ret == 0 ? "PASSED" : "FAILED", detail);
      return ret == 0 ? 0 : 1;
    }

  if (vg_ota_self_check_get_status(&st) == 0)
    {
      const char *sname = "none";
      if (st.state == VG_OTA_CHECK_STATE_PASSED)
        {
          sname = "passed";
        }
      else if (st.state == VG_OTA_CHECK_STATE_FAILED)
        {
          sname = "failed";
        }

      printf("vgota: selfcheck state=%s slot=%s running_slot=%s trials=%" PRIu32
             " seq=%" PRIu32 " bcb_state=%s task=%s ver=%s detail=%s\n",
             sname, vg_ota_bcb_slot_name(st.active_bcb_slot),
             vg_fw_slot_name(), st.trial_count, st.active_bcb_seq,
             vg_ota_bcb_state_name(st.active_bcb_state),
             st.active_task_id, st.active_version, st.detail);
      return 0;
    }

  printf("vgota: selfcheck status unavailable\n");
  return 1;
}

static int cmd_confirm(void)
{
  int ret = vg_ota_self_check_manual_confirm();
  if (ret == 0)
    {
      printf("vgota: manual confirm SUCCESS, confirmed BCB written\n");
      return 0;
    }

  printf("vgota: manual confirm FAILED ERR=%d\n", -ret);
  return 1;
}

static int cmd_rollback(int argc, char *argv[])
{
  const char *reason = (argc >= 3) ? argv[2] : "user-requested rollback";
  int ret = vg_ota_self_check_manual_rollback(reason);
  if (ret != 0)
    {
      printf("vgota: manual rollback failed ERR=%d\n", -ret);
      return 1;
    }

  return 0;
}

int main(int argc, char *argv[])
{
  if (argc < 2)
    {
      usage();
      return 1;
    }

  if (strcmp(argv[1], "identity") == 0 && argc == 2)
    {
      print_identity();
      return 0;
    }

  if (strcmp(argv[1], "bootinfo") == 0 && argc == 2)
    {
      print_bootinfo();
      return 0;
    }

  if (strcmp(argv[1], "reboot") == 0 && argc == 2)
    {
      int ret = vg_ota_safe_reboot();
      printf("vgota: safe reboot failed ERR=%d\n", -ret);
      return 1;
    }

  if (strcmp(argv[1], "selfcheck") == 0)
    {
      return cmd_selfcheck(argc, argv);
    }

  if (strcmp(argv[1], "confirm") == 0 && argc == 2)
    {
      return cmd_confirm();
    }

  if (strcmp(argv[1], "rollback") == 0)
    {
      return cmd_rollback(argc, argv);
    }

  if (strcmp(argv[1], "probe") == 0 && argc == 2)
    {
      probe_emmc(VG_EMMC_DEV);
      return 0;
    }

  if (strcmp(argv[1], "qspiprobe") == 0)
    {
      return cmd_qspiprobe(argc, argv);
    }

#ifdef CONFIG_VG_OTA_TEST_FIXTURES
  if (strcmp(argv[1], "fixture") == 0)
    {
      return cmd_fixture(argc, argv);
    }
#endif

  usage();
  return 1;
}
