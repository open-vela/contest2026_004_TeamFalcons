/****************************************************************************
 * app/velaguard/vgconfigmqtt.c
 *
 * NSH provisioning/status helper for the dedicated candidate MQTT service.
 * Credential values are never printed.
 ****************************************************************************/

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "vg_config_mqtt_credentials.h"
#include "vg_config_mqtt_runtime.h"
#include "widgets/vg_config_confirmation_ui.h"

static int service_is_running(void)
{
  struct vg_config_mqtt_runtime_status status;

  if (vg_config_mqtt_runtime_status(&status) != 0)
    {
      return 0;
    }
  return status.running;
}

static int read_import_file(const char *path, char *buffer, size_t capacity,
                            size_t *length)
{
  size_t used = 0;
  int fd;

  if (path == NULL || buffer == NULL || length == NULL)
    {
      return -EINVAL;
    }
  fd = open(path, O_RDONLY);
  if (fd < 0)
    {
      return -errno;
    }
  while (used < capacity)
    {
      ssize_t got = read(fd, buffer + used, capacity - used);
      if (got < 0 && errno == EINTR)
        {
          continue;
        }
      if (got < 0)
        {
          int ret = -errno;
          close(fd);
          return ret;
        }
      if (got == 0)
        {
          break;
        }
      used += (size_t)got;
    }
  if (used == capacity)
    {
      char extra;
      if (read(fd, &extra, 1) != 0)
        {
          close(fd);
          return -EFBIG;
        }
    }
  if (close(fd) != 0)
    {
      return -errno;
    }
  *length = used;
  return 0;
}

static int hex_nibble(char value)
{
  if (value >= '0' && value <= '9')
    {
      return value - '0';
    }
  if (value >= 'a' && value <= 'f')
    {
      return value - 'a' + 10;
    }
  if (value >= 'A' && value <= 'F')
    {
      return value - 'A' + 10;
    }
  return -1;
}

static int append_sealed_chunk(const char *hex)
{
  uint8_t bytes[32];
  size_t count;
  size_t i;
  int fd;
  ssize_t written;

  if (hex == NULL)
    {
      return -EINVAL;
    }
  count = strlen(hex);
  if (count == 0 || (count & 1u) != 0 || count > sizeof(bytes) * 2u)
    {
      return -EINVAL;
    }
  for (i = 0; i < count; i += 2)
    {
      int high = hex_nibble(hex[i]);
      int low = hex_nibble(hex[i + 1]);

      if (high < 0 || low < 0)
        {
          return -EINVAL;
        }
      bytes[i / 2] = (uint8_t)((high << 4) | low);
    }

  (void)mkdir("/data/velaguard/provision", 0700);
  fd = open(VG_CONFIG_MQTT_CREDENTIALS_STAGING,
            O_WRONLY | O_CREAT | O_APPEND, 0600);
  if (fd < 0)
    {
      return -errno;
    }
  written = write(fd, bytes, count / 2);
  if (close(fd) != 0 && written >= 0)
    {
      return -errno;
    }
  return written == (ssize_t)(count / 2) ? 0 : -EIO;
}

static int commit_sealed_blob(void)
{
  uint8_t blob[VG_CONFIG_MQTT_CREDENTIALS_BLOB_MAX];
  struct stat st;
  size_t length;
  size_t used = 0;
  int fd;
  int ret;

  if (stat(VG_CONFIG_MQTT_CREDENTIALS_STAGING, &st) != 0)
    {
      return -errno;
    }
  if (st.st_size < 60 ||
      st.st_size > VG_CONFIG_MQTT_CREDENTIALS_BLOB_MAX)
    {
      return -EBADMSG;
    }
  length = (size_t)st.st_size;
  fd = open(VG_CONFIG_MQTT_CREDENTIALS_STAGING, O_RDONLY);
  if (fd < 0)
    {
      return -errno;
    }
  while (used < length)
    {
      ssize_t got = read(fd, blob + used, length - used);
      if (got < 0 && errno == EINTR)
        {
          continue;
        }
      if (got <= 0)
        {
          close(fd);
          memset(blob, 0, sizeof(blob));
          return -EIO;
        }
      used += (size_t)got;
    }
  ret = close(fd) == 0 ? 0 : -errno;
  if (ret == 0)
    {
      ret = vg_config_mqtt_credentials_install_blob(blob, length);
    }
  if (ret == 0)
    {
      (void)unlink(VG_CONFIG_MQTT_CREDENTIALS_STAGING);
    }
  memset(blob, 0, sizeof(blob));
  return ret;
}

static void usage(void)
{
  puts("Usage:");
  puts("  vgconfigmqtt status");
  puts("  vgconfigmqtt import <json-file>");
  puts("  vgconfigmqtt put <sealed-hex>");
  puts("  vgconfigmqtt commit");
  puts("  vgconfigmqtt wipe");
}

int main(int argc, char *argv[])
{
  if (argc < 2)
    {
      usage();
      return 1;
    }

  if (strcmp(argv[1], "status") == 0)
    {
      vg_config_mqtt_runtime_print(stdout);
      return 0;
    }
  if (strcmp(argv[1], "start") == 0)
    {
      puts("vgconfigmqtt: service starts from velaguard_app_main; reset required");
      return 1;
    }
  if (strcmp(argv[1], "import") == 0)
    {
      char json[VG_CONFIG_MQTT_CREDENTIALS_JSON_MAX];
      size_t length = 0;
      int ret;

      if (argc != 3)
        {
          usage();
          return 1;
        }
      ret = read_import_file(argv[2], json, sizeof(json), &length);
      if (ret == 0)
        {
          ret = vg_config_mqtt_credentials_import(json, length);
        }
      memset(json, 0, sizeof(json));
      if (ret != 0)
        {
          printf("vgconfigmqtt: import failed %d\n", ret);
          return 1;
        }
      puts("vgconfigmqtt: credentials stored; reset required");
      return 0;
    }
  if (strcmp(argv[1], "put") == 0)
    {
      int ret;

      if (argc != 3)
        {
          usage();
          return 1;
        }
      ret = append_sealed_chunk(argv[2]);
      if (ret != 0)
        {
          printf("vgconfigmqtt: put failed %d\n", ret);
          return 1;
        }
      return 0;
    }
  if (strcmp(argv[1], "commit") == 0)
    {
      int ret;

      if (argc != 2)
        {
          usage();
          return 1;
        }
      ret = commit_sealed_blob();
      if (ret != 0)
        {
          printf("vgconfigmqtt: commit failed %d\n", ret);
          return 1;
        }
      puts("vgconfigmqtt: credentials stored; reset required");
      return 0;
    }
  if (strcmp(argv[1], "wipe") == 0)
    {
      struct vg_config_mqtt_runtime_status status;
      int ret;

      (void)vg_config_mqtt_runtime_status(&status);
      (void)unlink(VG_CONFIG_MQTT_CREDENTIALS_STAGING);
      ret = vg_config_mqtt_credentials_wipe();
      if (ret != 0)
        {
          printf("vgconfigmqtt: wipe failed %d\n", ret);
          return 1;
        }
      puts(status.running ?
           "vgconfigmqtt: wiped; reset required to stop the active service" :
           "vgconfigmqtt: wiped");
      return 0;
    }

  usage();
  return 1;
}
