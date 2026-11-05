/****************************************************************************
 * app/velaguard/vg_config_mqtt_credentials.c
 ****************************************************************************/

#ifdef __NuttX__
#  include <nuttx/config.h>
#endif

#include "vg_config_mqtt_credentials.h"

#include "vg_ota_http.h"
#include "vg_ota_json.h"

#ifdef __NuttX__
#  include "vg_file_store.h"
#  include "vg_provision.h"
#  include <fcntl.h>
#  include <sys/stat.h>
#  include <unistd.h>
#endif

#ifdef __NuttX__
#  include <netutils/cJSON.h>
#else
#  include "cJSON.h"
#endif

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int text(const cJSON *node, char *out, size_t capacity)
{
  size_t length;
  size_t i;

  if (!cJSON_IsString(node) || node->valuestring == NULL)
    {
      return -EINVAL;
    }
  length = strlen(node->valuestring);
  if (length == 0 || length >= capacity)
    {
      return -EINVAL;
    }
  for (i = 0; i < length; i++)
    {
      unsigned char c = (unsigned char)node->valuestring[i];
      if (c < 32 || c == 127)
        {
          return -EINVAL;
        }
    }
  memcpy(out, node->valuestring, length + 1);
  return 0;
}

static void clear_values(cJSON *node)
{
  cJSON *child;

  if (node == NULL)
    {
      return;
    }
  if (node->valuestring != NULL)
    {
      volatile char *cursor = node->valuestring;
      size_t length = strlen(node->valuestring);
      while (length-- != 0)
        {
          *cursor++ = 0;
        }
    }
  for (child = node->child; child != NULL; child = child->next)
    {
      clear_values(child);
    }
}

int vg_config_mqtt_credentials_parse(
  const void *json,
  size_t length,
  struct vg_config_mqtt_config *out)
{
  static const char *const names[] =
  {
    "broker", "port", "username", "password"
  };
  const cJSON *fields[4] = { 0 };
  char *copy = NULL;
  cJSON *root = NULL;
  cJSON *item;
  char endpoint[128];
  struct vg_ota_url url;
  unsigned seen = 0;
  int ret = -EINVAL;

  if (json == NULL || out == NULL || length == 0 ||
      length > VG_CONFIG_MQTT_CREDENTIALS_JSON_MAX ||
      vg_ota_json_guard(json, length) != 0)
    {
      return -EINVAL;
    }

  memset(out, 0, sizeof(*out));
  copy = malloc(length + 1);
  if (copy == NULL)
    {
      return -ENOMEM;
    }
  memcpy(copy, json, length);
  copy[length] = '\0';
  root = cJSON_ParseWithOpts(copy, NULL, 1);
  if (!cJSON_IsObject(root))
    {
      goto done;
    }

  cJSON_ArrayForEach(item, root)
    {
      unsigned i;
      for (i = 0; i < 4; i++)
        {
          if (item->string != NULL && strcmp(item->string, names[i]) == 0)
            {
              break;
            }
        }
      if (i == 4 || (seen & (1u << i)) != 0)
        {
          goto done;
        }
      seen |= 1u << i;
      fields[i] = item;
    }
  if (seen != 15 ||
      text(fields[0], out->broker, sizeof(out->broker)) != 0 ||
      text(fields[1], out->port, sizeof(out->port)) != 0 ||
      text(fields[2], out->username, sizeof(out->username)) != 0 ||
      text(fields[3], out->password, sizeof(out->password)) != 0)
    {
      goto done;
    }

  snprintf(endpoint, sizeof(endpoint), "https://%s:%s/",
           out->broker, out->port);
  if (vg_ota_url_parse(endpoint, &url) != 0 ||
      strcmp(url.host, out->broker) != 0 ||
      strcmp(url.port, out->port) != 0)
    {
      goto done;
    }

  ret = 0;

done:
  clear_values(root);
  cJSON_Delete(root);
  if (copy != NULL)
    {
      memset(copy, 0, length);
      free(copy);
    }
  if (ret != 0)
    {
      volatile uint8_t *bytes = (volatile uint8_t *)out;
      size_t remaining = sizeof(*out);
      while (remaining-- != 0)
        {
          *bytes++ = 0;
        }
    }
  return ret;
}

#ifdef __NuttX__

static int read_blob(uint8_t *blob, size_t capacity, size_t *length)
{
  struct stat st;
  size_t used = 0;
  int fd;
  int ret = 0;

  fd = open(VG_CONFIG_MQTT_CREDENTIALS_FILE, O_RDONLY);
  if (fd < 0)
    {
      return -errno;
    }
  if (fstat(fd, &st) != 0 || st.st_size < 60 ||
      st.st_size > VG_CONFIG_MQTT_CREDENTIALS_BLOB_MAX ||
      (size_t)st.st_size > capacity)
    {
      close(fd);
      return -EBADMSG;
    }

  while (used < (size_t)st.st_size)
    {
      ssize_t got = read(fd, blob + used, (size_t)st.st_size - used);
      if (got < 0 && errno == EINTR)
        {
          continue;
        }
      if (got <= 0)
        {
          ret = -EIO;
          break;
        }
      used += (size_t)got;
    }
  if (close(fd) != 0 && ret == 0)
    {
      ret = -errno;
    }
  if (ret == 0)
    {
      *length = used;
    }
  return ret;
}

int vg_config_mqtt_credentials_load(
  struct vg_config_mqtt_config *out)
{
  uint8_t uid[VG_PROVISION_UID_LEN];
  uint8_t blob[VG_CONFIG_MQTT_CREDENTIALS_BLOB_MAX];
  uint8_t plain[VG_CONFIG_MQTT_CREDENTIALS_JSON_MAX];
  size_t blob_length = 0;
  size_t plain_length = 0;
  int ret;

  if (out == NULL)
    {
      return -EINVAL;
    }
  memset(out, 0, sizeof(*out));
  ret = vg_provision_read_uid(uid);
  if (ret != 0)
    {
      return ret;
    }
  ret = read_blob(blob, sizeof(blob), &blob_length);
  if (ret != 0)
    {
      goto done;
    }
  ret = vg_provision_crypto_open(uid, blob, blob_length,
                                 plain, sizeof(plain), &plain_length);
  if (ret != 0)
    {
      goto done;
    }
  ret = vg_config_mqtt_credentials_parse(plain, plain_length, out);

done:
  memset(plain, 0, sizeof(plain));
  memset(blob, 0, sizeof(blob));
  memset(uid, 0, sizeof(uid));
  return ret;
}

int vg_config_mqtt_credentials_import(
  const void *json,
  size_t length)
{
  uint8_t uid[VG_PROVISION_UID_LEN];
  uint8_t blob[VG_CONFIG_MQTT_CREDENTIALS_BLOB_MAX];
  size_t blob_length;
  struct vg_config_mqtt_config config;
  int ret;

  ret = vg_config_mqtt_credentials_parse(json, length, &config);
  if (ret != 0)
    {
      return ret;
    }
  ret = vg_provision_store_is_persistent("/data");
  if (ret == 0)
    {
      ret = vg_provision_read_uid(uid);
    }
  if (ret == 0)
    {
      ret = vg_provision_crypto_seal(uid, json, length, blob,
                                     sizeof(blob), &blob_length);
    }
  if (ret == 0)
    {
      ret = vg_file_replace(VG_CONFIG_MQTT_CREDENTIALS_FILE,
                            blob, blob_length, 0600);
    }

  memset(&config, 0, sizeof(config));
  memset(blob, 0, sizeof(blob));
  memset(uid, 0, sizeof(uid));
  return ret;
}

int vg_config_mqtt_credentials_install_blob(
  const void *blob,
  size_t length)
{
  uint8_t uid[VG_PROVISION_UID_LEN];
  uint8_t plain[VG_CONFIG_MQTT_CREDENTIALS_JSON_MAX];
  size_t plain_length = 0;
  struct vg_config_mqtt_config config;
  int ret;

  if (blob == NULL || length < 60 ||
      length > VG_CONFIG_MQTT_CREDENTIALS_BLOB_MAX)
    {
      return -EINVAL;
    }

  ret = vg_provision_store_is_persistent("/data");
  if (ret == 0)
    {
      ret = vg_provision_read_uid(uid);
    }
  if (ret == 0)
    {
      ret = vg_provision_crypto_open(uid, blob, length, plain,
                                     sizeof(plain), &plain_length);
    }
  if (ret == 0)
    {
      ret = vg_config_mqtt_credentials_parse(plain, plain_length, &config);
    }
  if (ret == 0)
    {
      ret = vg_file_replace(VG_CONFIG_MQTT_CREDENTIALS_FILE,
                            blob, length, 0600);
    }

  memset(&config, 0, sizeof(config));
  memset(plain, 0, sizeof(plain));
  memset(uid, 0, sizeof(uid));
  return ret;
}

int vg_config_mqtt_credentials_wipe(void)
{
  return unlink(VG_CONFIG_MQTT_CREDENTIALS_FILE) == 0 ||
         errno == ENOENT ? 0 : -errno;
}

#else

int vg_config_mqtt_credentials_load(
  struct vg_config_mqtt_config *out)
{
  if (out != NULL)
    {
      memset(out, 0, sizeof(*out));
    }
  return -ENOSYS;
}

int vg_config_mqtt_credentials_import(
  const void *json,
  size_t length)
{
  (void)json;
  (void)length;
  return -ENOSYS;
}

int vg_config_mqtt_credentials_install_blob(
  const void *blob,
  size_t length)
{
  (void)blob;
  (void)length;
  return -ENOSYS;
}

int vg_config_mqtt_credentials_wipe(void)
{
  return -ENOSYS;
}

#endif
