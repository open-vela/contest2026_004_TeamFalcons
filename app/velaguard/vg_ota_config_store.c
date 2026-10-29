/* SPDX-License-Identifier: Apache-2.0 */
#include <nuttx/config.h>
#include "vg_ota_config.h"
#include "vg_file_store.h"
#include "vg_provision.h"
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <mbedtls/platform_util.h>

int vg_ota_config_load(struct vg_ota_config *out)
{
  uint8_t uid[12],*blob;
  struct stat st;
  int fd,ret;
  if(!out) return -EINVAL;
  memset(out,0,sizeof(*out));
  if(vg_provision_read_uid(uid)) return -EIO;
  fd=open(VG_OTA_CONFIG_FILE,O_RDONLY);
  if(fd<0) return -errno;
  if(fstat(fd,&st) || st.st_size<52 || st.st_size>VG_OTA_CONFIG_BLOB_MAX) { close(fd); return -EBADMSG; }
  blob=malloc((size_t)st.st_size);
  if(!blob) { close(fd); return -ENOMEM; }
  size_t done=0;
  while(done<(size_t)st.st_size) {
    ssize_t got=read(fd,blob+done,(size_t)st.st_size-done);
    if(got<=0) break;
    done+=(size_t)got;
  }
  close(fd);
  ret=done==(size_t)st.st_size ? vg_ota_config_open(uid,blob,done,out) : -EIO;
  mbedtls_platform_zeroize(blob,(size_t)st.st_size); free(blob);
  return ret;
}
int vg_ota_config_import(const uint8_t *blob,size_t length)
{
  uint8_t uid[12];
  struct vg_ota_config *config=calloc(1,sizeof(*config));
  if(!config) return -ENOMEM;
  int ret=vg_provision_read_uid(uid);
  if(!ret) ret=vg_ota_config_open(uid,blob,length,config);
  mbedtls_platform_zeroize(config,sizeof(*config)); free(config);
  if(ret) return ret;
  ret=vg_provision_store_is_persistent("/data");
  if(ret) return ret;
  if(mkdir("/data/velaguard/provision",0700) && errno!=EEXIST) return -errno;
  return vg_file_replace(VG_OTA_CONFIG_FILE,blob,length,0600);
}
