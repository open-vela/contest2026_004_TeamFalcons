#include "vg_ota_staging.h"
#include "vg_ota_sha256.h"
#include "vg_device_id.h"
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
static pthread_mutex_t lock=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t changed=PTHREAD_COND_INITIALIZER;
static int entered,release_source,reader_done;
static uint8_t image[64]={0,0,1,0x24,0x21,0,0x80,0x90};
static struct vg_ota_manifest manifest;
static ssize_t source(void *ctx,uint32_t off,uint8_t *out,size_t n)
{
  (void)ctx;
  pthread_mutex_lock(&lock); entered=1; pthread_cond_broadcast(&changed);
  while(!release_source) pthread_cond_wait(&changed,&lock);
  pthread_mutex_unlock(&lock);
  memcpy(out,image+off,n); return (ssize_t)n;
}
static void *writer(void *arg)
{
  char error[64]; (void)arg;
  assert(vg_ota_staging_stage(&manifest,source,NULL,error,sizeof(error))==0);
  return NULL;
}
static void *reader(void *arg)
{
  struct vg_ota_staging_info info; (void)arg;
  assert(vg_ota_staging_get_info(&info)==0);
  assert(info.state==VG_OTA_STATE_DOWNLOADING);
  vg_ota_staging_tick();
  pthread_mutex_lock(&lock); reader_done=1; pthread_cond_broadcast(&changed); pthread_mutex_unlock(&lock);
  return NULL;
}
int main(void)
{
  pthread_t w,r; struct timespec deadline; struct vg_ota_sha256 hash;
  char temp[]="/tmp/vg-ota-nonblocking-XXXXXX",path[160];
  assert(mkdtemp(temp)); vg_ota_staging_set_dir(temp);
  manifest.slot=1; manifest.flags=VG_OTA_MANIFEST_FIXTURE; manifest.image_size=sizeof(image);
  strcpy(manifest.version,"0.2.0"); strcpy(manifest.task_id,"T08-NONBLOCK");
  strcpy(manifest.device_id,vg_device_id()); strcpy(manifest.model,VG_OTA_MODEL); strcpy(manifest.key_id,"fixture");
  manifest.issued_at=1; manifest.expires_at=2;
  vg_ota_sha256_init(&hash); vg_ota_sha256_update(&hash,image,sizeof(image)); vg_ota_sha256_final(&hash,manifest.sha256);
  assert(!pthread_create(&w,NULL,writer,NULL));
  pthread_mutex_lock(&lock); while(!entered) pthread_cond_wait(&changed,&lock); pthread_mutex_unlock(&lock);
  assert(!pthread_create(&r,NULL,reader,NULL));
  clock_gettime(CLOCK_REALTIME,&deadline); deadline.tv_sec++;
  pthread_mutex_lock(&lock);
  while(!reader_done) assert(!pthread_cond_timedwait(&changed,&lock,&deadline));
  release_source=1; pthread_cond_broadcast(&changed); pthread_mutex_unlock(&lock);
  pthread_join(w,NULL); pthread_join(r,NULL);
  vg_ota_staging_clear(); snprintf(path,sizeof(path),"%s/STATE.BIN",temp); unlink(path); rmdir(temp);
  puts("test_ota_staging_concurrent: OK (status and heartbeat stay responsive during blocked network read)");
  return 0;
}
