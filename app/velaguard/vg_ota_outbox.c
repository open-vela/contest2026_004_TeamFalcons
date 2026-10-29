/* SPDX-License-Identifier: Apache-2.0 */
#include "vg_ota_outbox.h"
#include "vg_ota_protocol.h"
#include "vg_ota_bcb.h"
#include "vg_file_store.h"
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static char g_directory[128]="/data/velaguard/ota";
static pthread_mutex_t g_lock=PTHREAD_MUTEX_INITIALIZER;
static int valid(const struct vg_ota_event *e)
{
  static const char *results[]={"installed","declined","expired","failed","rolled_back","self_check_failed"};
  if(!e || !vg_ota_identifier(e->task_id,sizeof(e->task_id)) || !vg_ota_identifier(e->version,sizeof(e->version)) ||
     !vg_ota_identifier(e->error,sizeof(e->error)) || (e->slot!='A' && e->slot!='B') || e->acknowledged>3 ||
     (!memchr(e->self_check,0,sizeof(e->self_check)) || !memchr(e->result,0,sizeof(e->result)))) return 0;
  if(e->self_check[0] && strcmp(e->self_check,"passed") && strcmp(e->self_check,"failed")) return 0;
  for(unsigned i=0;i<sizeof(results)/sizeof(results[0]);i++) if(!strcmp(e->result,results[i])) return 1;
  return 0;
}
static void path_for(char *path,size_t n,unsigned slot)
{ snprintf(path,n,"%s/event%u.bin",g_directory,slot); }
static int save(unsigned slot,const struct vg_ota_event *e)
{
  uint8_t wire[256]={0}; char path[160]; uint32_t crc;
  if(!valid(e)) return -EINVAL;
  memcpy(wire,"VGOR",4); wire[4]=1; wire[5]=(uint8_t)e->acknowledged; wire[6]=(uint8_t)e->slot;
  memcpy(wire+8,e->task_id,40); memcpy(wire+48,e->version,32); memcpy(wire+80,e->result,24);
  memcpy(wire+104,e->error,48); memcpy(wire+152,e->self_check,16);
  crc=vg_ota_crc32(wire,252);
  for(unsigned i=0;i<4;i++) wire[252+i]=(uint8_t)(crc>>(i*8));
  path_for(path,sizeof(path),slot);
  return vg_file_replace(path,wire,sizeof(wire),0600);
}
static int read_one(const char *path,struct vg_ota_event *e)
{
  uint8_t wire[256],extra; int fd=open(path,O_RDONLY); uint32_t crc=0;
  if(fd<0) return -errno;
  ssize_t n=read(fd,wire,sizeof(wire));
  int oversized=read(fd,&extra,1)!=0;
  close(fd);
  if(n!=sizeof(wire) || oversized || memcmp(wire,"VGOR",4) || wire[4]!=1 || wire[7]) return -EBADMSG;
  for(unsigned i=0;i<4;i++) crc|=(uint32_t)wire[252+i]<<(i*8);
  if(crc!=vg_ota_crc32(wire,252)) return -EBADMSG;
  for(unsigned i=168;i<252;i++) if(wire[i]) return -EBADMSG;
  memset(e,0,sizeof(*e));
  e->acknowledged=wire[5]; e->slot=(char)wire[6];
  memcpy(e->task_id,wire+8,40); memcpy(e->version,wire+48,32); memcpy(e->result,wire+80,24);
  memcpy(e->error,wire+104,48); memcpy(e->self_check,wire+152,16);
  return valid(e) ? 0 : -EBADMSG;
}
static int load(unsigned slot,struct vg_ota_event *e)
{
  char path[168]; path_for(path,sizeof(path),slot);
  int ret=read_one(path,e);
  if(ret<0) {
    size_t n=strlen(path); memcpy(path+n,".bak",5);
    int backup=read_one(path,e);
    if(backup==0) return 0;
    if(ret==-ENOENT && backup==-ENOENT) return -ENOENT;
    return -EBADMSG;
  }
  return 0;
}
void vg_ota_outbox_set_dir(const char *directory)
{
  pthread_mutex_lock(&g_lock);
  snprintf(g_directory,sizeof(g_directory),"%s",directory ? directory : "/data/velaguard/ota");
  pthread_mutex_unlock(&g_lock);
}
int vg_ota_outbox_enqueue(const struct vg_ota_event *e)
{
  struct vg_ota_event existing;
  int free_slot=-1,ret=-ENOSPC;
  if(!valid(e)) return -EINVAL;
  pthread_mutex_lock(&g_lock);
  if(mkdir(g_directory,0700) && errno!=EEXIST) { ret=-errno; goto done; }
  for(unsigned i=0;i<VG_OTA_OUTBOX_SLOTS;i++) {
    int got=load(i,&existing);
    if(!got && !strcmp(existing.task_id,e->task_id) && !strcmp(existing.result,e->result) && !strcmp(existing.self_check,e->self_check)) { ret=0; goto done; }
    if(got==-ENOENT && free_slot<0) free_slot=(int)i;
    else if(got!=0 && got!=-ENOENT) { ret=got; goto done; }
  }
  if(free_slot>=0) ret=save((unsigned)free_slot,e);
done:
  pthread_mutex_unlock(&g_lock); return ret;
}
int vg_ota_outbox_peek(struct vg_ota_event *e,unsigned *slot)
{
  if(!e || !slot) return -EINVAL;
  pthread_mutex_lock(&g_lock);
  int ret=-ENOENT;
  for(unsigned i=0;i<VG_OTA_OUTBOX_SLOTS;i++) {
    ret=load(i,e);
    if(!ret) { *slot=i; break; }
    if(ret!=-ENOENT) break;
  }
  pthread_mutex_unlock(&g_lock); return ret;
}
int vg_ota_outbox_space(void)
{
  struct vg_ota_event e; int count=0;
  pthread_mutex_lock(&g_lock);
  for(unsigned i=0;i<VG_OTA_OUTBOX_SLOTS;i++) {
    int ret=load(i,&e);
    if(ret==-ENOENT) count++;
    else if(ret<0) { count=ret; break; }
  }
  pthread_mutex_unlock(&g_lock); return count;
}
int vg_ota_outbox_ack(unsigned slot,unsigned bit)
{
  struct vg_ota_event e; char path[168];
  if(slot>=VG_OTA_OUTBOX_SLOTS || (bit!=1 && bit!=2)) return -EINVAL;
  pthread_mutex_lock(&g_lock);
  int ret=load(slot,&e);
  if(ret) goto done;
  e.acknowledged|=bit;
  unsigned required=e.self_check[0] ? 3u : 1u;
  if((e.acknowledged&required)==required) {
    path_for(path,sizeof(path),slot);
    if(unlink(path) && errno!=ENOENT) { ret=-errno; goto done; }
    size_t n=strlen(path); memcpy(path+n,".bak",5);
    if(unlink(path) && errno!=ENOENT) { ret=-errno; goto done; }
    sync();
  } else ret=save(slot,&e);
done:
  pthread_mutex_unlock(&g_lock); return ret;
}
