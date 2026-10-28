#include "vg_ota_download.h"
#include "vg_ota_https.h"
#include "vg_ota_staging.h"
#include "vg_ota_manifest.h"
#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
struct context { struct vg_ota_https https; int egress; };
static int open_http(void *ctx,const char *u,uint32_t n,const char *e,struct vg_ota_http_response *r)
{ return vg_ota_https_open(&((struct context *)ctx)->https,u,n,e,r); }
static ssize_t read_http(void *ctx,uint8_t *b,size_t n)
{ return vg_ota_https_read(&((struct context *)ctx)->https,b,n); }
static void close_http(void *ctx) { vg_ota_https_close(&((struct context *)ctx)->https); }
static uint64_t mono(void *ctx) { struct timespec t; (void)ctx; clock_gettime(CLOCK_MONOTONIC,&t); return (uint64_t)t.tv_sec*1000+t.tv_nsec/1000000; }
static uint64_t unix_time(void *ctx) { (void)ctx; return (uint64_t)time(NULL); }
static int egress(void *ctx) { return ((struct context *)ctx)->egress; }
static void delay(void *ctx,uint32_t ms) { (void)ctx; (void)ms; usleep(1000); }
static void cleanup(const char *path)
{
  DIR *d=opendir(path); struct dirent *entry; char file[256];
  assert(d);
  while ((entry=readdir(d))) if (strcmp(entry->d_name,".") && strcmp(entry->d_name,"..")) {
    assert(strlen(path)+strlen(entry->d_name)+2<sizeof(file));
    strcpy(file,path); strcat(file,"/"); strcat(file,entry->d_name); unlink(file);
  }
  closedir(d); rmdir(path);
}
int main(int argc,char **argv)
{
  struct context context;
  struct vg_ota_manifest m;
  struct vg_ota_offer offer;
  struct vg_ota_staging_info info;
  struct vg_ota_download_ports ports={&context,open_http,read_http,close_http,mono,unix_time,egress,delay};
  uint8_t wire[512];
  char *ca,error[96],temp[]="/tmp/vg-ota-network-XXXXXX",path[160];
  FILE *fp;
  long ca_len;
  assert(argc==8);
  fp=fopen(argv[1],"rb"); assert(fp); fseek(fp,0,SEEK_END); ca_len=ftell(fp); rewind(fp);
  assert(ca_len>0 && ca_len<32768); ca=calloc(1,(size_t)ca_len+1); assert(ca);
  assert(fread(ca,1,(size_t)ca_len,fp)==(size_t)ca_len); fclose(fp);
  vg_ota_https_init(&context.https,ca); context.egress=atoi(argv[7]);
  fp=fopen(argv[2],"rb"); assert(fp && fread(wire,1,512,fp)==512); fclose(fp);
  assert(vg_ota_manifest_decode(wire,sizeof(wire),&m)==0);
  memset(&offer,0,sizeof(offer));
  snprintf(offer.task_id,sizeof(offer.task_id),"%s",m.task_id);
  snprintf(offer.version,sizeof(offer.version),"%s",m.version);
  offer.image_size=m.image_size; offer.expires_at=m.expires_at; memcpy(offer.sha256,m.sha256,32);
  snprintf(offer.manifest_url,sizeof(offer.manifest_url),"%s",argv[3]);
  snprintf(offer.image_url,sizeof(offer.image_url),"%s",argv[4]);
  const char *hosts[]={argv[5]};
  assert(mkdtemp(temp)); vg_ota_staging_set_dir(temp);
  int ret=vg_ota_download_run(&offer,hosts,1,&ports,error,sizeof(error));
  vg_ota_staging_get_info(&info);
  snprintf(path,sizeof(path),"%s/BCB0.BIN",temp); assert(access(path,F_OK)!=0);
  snprintf(path,sizeof(path),"%s/BCB1.BIN",temp); assert(access(path,F_OK)!=0);
  if (!ret) assert(info.state==VG_OTA_STATE_READY_AWAITING_CONFIRM && info.downloaded_bytes==offer.image_size);
  else assert(info.state!=VG_OTA_STATE_READY_AWAITING_CONFIRM);
  assert(!strstr(error,"https://") && !strstr(error,"secret-query"));
  printf("ota_network_helper: rc=%d error=%s state=%s bytes=%u\n",ret,error[0]?error:"none",vg_ota_state_name(info.state),info.downloaded_bytes);
  int expected=atoi(argv[6]);
  cleanup(temp); free(ca);
  return (ret==0)==(expected==0) ? 0 : 1;
}
