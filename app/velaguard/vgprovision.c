/****************************************************************************
 * NSH: vgprovision — seal / inspect encrypted LLM credentials on eMMC.
 ****************************************************************************/

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <sys/stat.h>
#include <unistd.h>

#include <nuttx/config.h>

#include "vg_provision.h"

static void usage(void)
{
  printf("Usage:\n");
  printf("  vgprovision uid\n");
  printf("  vgprovision status\n");
  printf("  vgprovision wipe\n");
  printf("  vgprovision put <hex>\n");
  printf("  vgprovision commit\n");
  printf("  vgprovision apply\n");
}

#define VG_STAGING_FILE "/data/velaguard/provision/.staging"
#define VG_PROVISION_DIR  "/data/velaguard/provision"
#define VG_APPLY_FLAG     "/data/velaguard/provision/.apply_on_boot"

static int hex_nibble(char c)
{
  if (c >= '0' && c <= '9')
    {
      return c - '0';
    }

  if (c >= 'a' && c <= 'f')
    {
      return c - 'a' + 10;
    }

  if (c >= 'A' && c <= 'F')
    {
      return c - 'A' + 10;
    }

  return -1;
}

static int cmd_put(const char *hex)
{
  uint8_t buf[32];
  size_t n = 0;
  size_t len;
  int fd;
  ssize_t w;

  if (hex == NULL)
    {
      return -EINVAL;
    }

  len = strlen(hex);
  if (len == 0 || (len % 2) != 0 || len > sizeof(buf) * 2)
    {
      return -EINVAL;
    }

  for (size_t i = 0; i < len; i += 2)
    {
      int hi = hex_nibble(hex[i]);
      int lo = hex_nibble(hex[i + 1]);

      if (hi < 0 || lo < 0)
        {
          return -EINVAL;
        }

      buf[n++] = (uint8_t)((hi << 4) | lo);
    }

  (void)mkdir(VG_PROVISION_DIR, 0700);
  fd = open(VG_STAGING_FILE, O_WRONLY | O_CREAT | O_APPEND, 0600);
  if (fd < 0)
    {
      return -errno;
    }

  w = write(fd, buf, n);
  close(fd);
  return (w == (ssize_t)n) ? 0 : -EIO;
}

static int cmd_commit(void)
{
  struct stat st;
  int fd;
  int ret;

  if (stat(VG_STAGING_FILE, &st) != 0 || st.st_size <= 0)
    {
      return -EINVAL;
    }

  ret = rename(VG_STAGING_FILE, VG_PROVISION_FILE);
  if (ret < 0)
    {
      return -errno;
    }

  printf("vgprovision: committed %s (%ld bytes)\n",
         VG_PROVISION_FILE, (long)st.st_size);

  /* Say plainly whether the blob will still be there after a restart.  A
   * commit onto RAM-backed /data prints no other symptom at the time and only
   * shows up later as an agent with no backend. */

  ret = vg_provision_store_is_persistent(VG_PROVISION_DIR);
  if (ret == -ENOENT)
    {
      ret = vg_provision_store_is_persistent("/data");
    }

  if (ret != 0)
    {
      printf("vgprovision: WARNING store is not persistent (%d): "
             "credentials will be lost on reboot\n", ret);
    }

  fd = open(VG_APPLY_FLAG, O_WRONLY | O_CREAT | O_TRUNC, 0600);
  if (fd >= 0)
    {
      close(fd);
    }

  printf("vgprovision: reboot required to apply LLM config "
         "(this build has no reboot command; reset the board)\n");
  return 0;
}

static void *apply_worker(FAR void *arg)
{
  int ret;

  (void)arg;
  ret = vg_provision_apply_llm_config();
  printf("vgprovision: apply %d\n", ret);
  return NULL;
}

static int cmd_apply_async(void)
{
  pthread_t tid;
  pthread_attr_t attr;
  int ret;

  pthread_attr_init(&attr);
  pthread_attr_setstacksize(&attr, 8192);
  ret = pthread_create(&tid, &attr, apply_worker, NULL);
  pthread_attr_destroy(&attr);
  if (ret != 0)
    {
      return -ret;
    }

  pthread_detach(tid);
  return 0;
}

static void print_uid(void)
{
  uint8_t uid[VG_PROVISION_UID_LEN];
  int ret;

  ret = vg_provision_read_uid(uid);
  if (ret < 0)
    {
      printf("vgprovision: uid error %d\n", ret);
      return;
    }

  printf("vgprovision: uid=");
  for (int i = 0; i < VG_PROVISION_UID_LEN; i++)
    {
      printf("%02x", uid[i]);
    }

  printf("\n");
}

int main(int argc, char *argv[])
{
  int ret;

  if (argc < 2)
    {
      usage();
      return 1;
    }

  if (strcmp(argv[1], "uid") == 0)
    {
      print_uid();
      return 0;
    }

  if (strcmp(argv[1], "status") == 0)
    {
      if (!vg_provision_is_present())
        {
          printf("vgprovision: not provisioned\n");
          return 1;
        }

      printf("vgprovision: OK file=%s\n", VG_PROVISION_FILE);
      return 0;
    }

  if (strcmp(argv[1], "wipe") == 0)
    {
      unlink(VG_STAGING_FILE);
      unlink(VG_PROVISION_FILE);
      printf("vgprovision: wiped\n");
      return 0;
    }

  if (strcmp(argv[1], "put") == 0)
    {
      if (argc < 3)
        {
          usage();
          return 1;
        }

      ret = cmd_put(argv[2]);
      if (ret < 0)
        {
          printf("vgprovision: put failed %d\n", ret);
          return 1;
        }

      return 0;
    }

  if (strcmp(argv[1], "commit") == 0)
    {
      ret = cmd_commit();
      return (ret < 0) ? 1 : 0;
    }

  if (strcmp(argv[1], "apply") == 0)
    {
      ret = cmd_apply_async();
      if (ret < 0)
        {
          printf("vgprovision: apply start failed %d\n", ret);
          return 1;
        }

      sleep(2);
      return 0;
    }

  usage();
  return 1;
}
