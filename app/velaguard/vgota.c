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
#include "vg_ota_probe.h"
#include "vg_ota_reboot.h"
#include "vg_ota_mbox.h"

#define VG_EMMC_DEV   "/dev/mmcsd0"
#define VG_EMMC_MOUNT "/mnt/emmc"

static void usage(void)
{
  printf("Usage:\n");
  printf("  vgota identity\n");
  printf("  vgota reboot\n");
  printf("  vgota probe\n");
  printf("  vgota qspiprobe\n");
  printf("  vgota qspiprobe --confirm\n");
#ifdef CONFIG_VG_OTA_TEST_FIXTURES
  printf("  vgota fixture status\n");
  printf("  vgota fixture write <NAME> <bytes> [seed]\n");
  printf("  vgota fixture clear\n");
#endif
}

static void print_identity(void)
{
  printf("vgota: identity version=%s slot=%s build=%s link=%08" PRIx32 "\n",
         vg_fw_version(), vg_fw_slot_name(), vg_fw_build_id(),
         vg_fw_link_base());
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

static int cmd_fixture(int argc, char *argv[])
{
  char *end;
  unsigned long size;
  unsigned long seed = 0;

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

  if (strcmp(argv[1], "reboot") == 0 && argc == 2)
    {
      int ret = vg_ota_safe_reboot();
      printf("vgota: safe reboot failed ERR=%d\n", -ret);
      return 1;
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
