/* SPDX-License-Identifier: Apache-2.0 */
#include "vg_ota_protocol.h"
#include "vg_ota_json.h"
#include "vg_ota_outbox.h"
#include "vg_ota_staging.h"
#include "vg_ota_bcb.h"
#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
static int fault;
ssize_t __real_write(int,const void *,size_t);
int __real_fsync(int);
int __real_rename(const char *,const char *);
ssize_t __wrap_write(int fd,const void *p,size_t n)
{ if(fault==1) { errno=ENOSPC; return -1; } return __real_write(fd,p,n); }
int __wrap_fsync(int fd)
{ if(fault==2) { errno=EIO; return -1; } return __real_fsync(fd); }
int __wrap_rename(const char *a,const char *b)
{ if(fault==3) { errno=EIO; return -1; } return __real_rename(a,b); }
/* Host soak does not flush unrelated host mounts; file fsync is still real. */
void __wrap_sync(void) { }
static unsigned files(const char *dir)
{
  DIR *d=opendir(dir); struct dirent *entry; unsigned n=0; assert(d);
  while((entry=readdir(d))) if(entry->d_name[0]!='.') n++;
  closedir(d); return n;
}
static void clear_dir(const char *dir)
{
  DIR *d=opendir(dir); struct dirent *entry; char path[256]; assert(d);
  while((entry=readdir(d))) if(entry->d_name[0]!='.') {
    assert(strlen(dir)+strlen(entry->d_name)+2<sizeof(path));
    strcpy(path,dir); strcat(path,"/"); strcat(path,entry->d_name); assert(!unlink(path));
  }
  closedir(d);
}
static void test_outbox(const char *dir)
{
  struct vg_ota_event event={.task_id="FAULT-10",.version="0.2.0",.result="installed",.error="none",.self_check="passed",.slot='B'},readback;
  unsigned slot;
  vg_ota_outbox_set_dir(dir);
  for(fault=1;fault<=3;fault++) assert(vg_ota_outbox_enqueue(&event)<0);
  fault=0; assert(vg_ota_outbox_space()==8 && !vg_ota_outbox_enqueue(&event));
  assert(!vg_ota_outbox_peek(&readback,&slot));
  for(fault=1;fault<=3;fault++) {
    assert(vg_ota_outbox_ack(slot,1)<0);
    assert(!vg_ota_outbox_peek(&readback,&slot) && readback.acknowledged==0);
  }
  fault=0; assert(!vg_ota_outbox_ack(slot,1));
  char path[256]; snprintf(path,sizeof(path),"%s/event%u.bin",dir,slot);
  int fd=open(path,O_WRONLY|O_TRUNC); assert(fd>=0); assert(write(fd,"torn",4)==4); close(fd);
  /* Previous durable backup is replayed, never treated as an acknowledged loss. */
  assert(!vg_ota_outbox_peek(&readback,&slot) && readback.acknowledged==0);
  assert(!vg_ota_outbox_ack(slot,1) && !vg_ota_outbox_ack(slot,2));
  assert(files(dir)==0 && vg_ota_outbox_space()==8);
  for(unsigned i=0;i<1000;i++) {
    snprintf(event.task_id,sizeof(event.task_id),"SOAK-%u",i);
    assert(!vg_ota_outbox_enqueue(&event));
    assert(!vg_ota_outbox_peek(&readback,&slot));
    assert(!vg_ota_outbox_ack(slot,1));
    vg_ota_outbox_set_dir(dir);
    assert(!vg_ota_outbox_peek(&readback,&slot) && readback.acknowledged==1);
    assert(!vg_ota_outbox_ack(slot,2));
    assert(files(dir)==0 && vg_ota_outbox_space()==8);
  }
  assert(!vg_ota_outbox_enqueue(&event));
  snprintf(path,sizeof(path),"%s/event0.bin",dir); fd=open(path,O_WRONLY|O_TRUNC); assert(fd>=0); close(fd);
  assert(vg_ota_outbox_space()==-EBADMSG && vg_ota_outbox_enqueue(&event)<0);
  clear_dir(dir);
}
static void test_tombstones(const char *dir)
{
  char path[256]; uint8_t record[44]={0};
  vg_ota_staging_set_dir(dir);
  assert(!vg_ota_staging_remember("KEEP-10"));
  for(fault=1;fault<=2;fault++) assert(vg_ota_staging_remember("WRITE-FAIL-10")<0);
  fault=0; clear_dir(dir);
  snprintf(path,sizeof(path),"%s/SUPPRESS.BIN",dir);
  FILE *f=fopen(path,"wb"); assert(f);
  for(unsigned i=0;i<4096;i++) {
    memset(record,0,sizeof(record)); snprintf((char *)record,40,"DONE-%u",i);
    uint32_t crc=vg_ota_crc32(record,40); memcpy(record+40,&crc,4);
    assert(fwrite(record,1,sizeof(record),f)==sizeof(record));
  }
  fclose(f); struct stat st; assert(!stat(path,&st) && st.st_size==4096*44);
  assert(vg_ota_staging_is_suppressed("NEW-AT-CAPACITY"));
  assert(!vg_ota_staging_remember("NEW-AT-CAPACITY"));
  assert(!stat(path,&st) && st.st_size==4096*44);
  clear_dir(dir);
}
static void test_fuzz(void)
{
  unsigned random=0x20261029; uint8_t buffer[1537];
  struct vg_ota_offer offer; char error[64];
  for(unsigned round=0;round<10000;round++) {
    size_t n=round%sizeof(buffer);
    for(size_t i=0;i<n;i++) { random=random*1664525u+1013904223u; buffer[i]=(uint8_t)(random>>24); }
    vg_ota_json_guard(buffer,n);
    assert(vg_ota_offer_parse("vg/vg-test/ota/offer",buffer,n,"vg-test",1,1,0,1,&offer,error,sizeof(error))<0);
  }
  memset(buffer,'[',sizeof(buffer)); assert(vg_ota_json_guard(buffer,sizeof(buffer))<0);
}
int main(void)
{
  char dir[]="/tmp/vg-ota-faults-XXXXXX"; assert(mkdtemp(dir));
  test_outbox(dir); test_tombstones(dir); test_fuzz(); assert(!rmdir(dir));
  puts("test_ota_faults: OK (write/fsync/rename failures, torn queue, 1000 report cycles, journal bound, 10000 parser inputs)");
  return 0;
}
