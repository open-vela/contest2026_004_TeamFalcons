#include "vg_ota_service.h"
#include "vg_ota_config.h"
#include "vg_ota_mqtt.h"
#include "vg_ota_staging.h"
#include "vg_ota_self_check.h"
#include "vg_ota_outbox.h"
#include "vg_ota_bcb.h"
#include "vg_ota_manifest.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
static const char *broker_port,*allowed;
int vg_mqtt_pal_try_sendall(int fd,const void *bytes,size_t n,int flags,ssize_t *out)
{ (void)flags; return vg_ota_mqtt_pal_send(fd,bytes,n,out); }
int vg_mqtt_pal_try_recvall(int fd,void *bytes,size_t n,int flags,ssize_t *out)
{ (void)flags; return vg_ota_mqtt_pal_recv(fd,bytes,n,out); }
int vg_ota_service_egress(void) { return 1; }
int vg_ota_config_load(struct vg_ota_config *out)
{
  memset(out,0,sizeof(*out)); strcpy(out->broker,"localhost");
  snprintf(out->port,sizeof(out->port),"%s",broker_port);
  strcpy(out->username,"ota-unit"); strcpy(out->password,"ota-synthetic-password");
  out->allowed_count=1; snprintf(out->allowed_hosts[0],sizeof(out->allowed_hosts[0]),"%s",allowed);
  return 0;
}
int vg_ota_safe_reboot(void) { abort(); }
static uint64_t now(void)
{ struct timespec ts; clock_gettime(CLOCK_MONOTONIC,&ts); return (uint64_t)ts.tv_sec*1000+ts.tv_nsec/1000000; }
int main(int argc,char **argv)
{
  assert(argc==5); broker_port=argv[1]; allowed=argv[2];
  char staging[128],outbox[128],path[160];
  assert(strlen(argv[4])+10<sizeof(staging));
  snprintf(staging,sizeof(staging),"%s/staging",argv[4]); snprintf(outbox,sizeof(outbox),"%s/events",argv[4]);
  assert(!mkdir(staging,0700) || access(staging,F_OK)==0);
  vg_ota_staging_set_dir(staging); vg_ota_self_check_set_dir(staging); vg_ota_outbox_set_dir(outbox);
  int boot=!strcmp(argv[3],"boot");
  if(boot) {
    struct vg_ota_bcb b={.seq=9,.state=VG_OTA_BCB_STATE_CONFIRMED,.slot=1,.image_size=16384,.flags=VG_OTA_BCB_LOCAL_CONFIRMED};
    uint8_t wire[4096]; strcpy(b.version,"0.2.0"); strcpy(b.task_id,"TASK-BOOT-09"); memset(b.sha256,0x42,32);
    assert(!vg_ota_bcb_encode(&b,wire,sizeof(wire)));
    snprintf(path,sizeof(path),"%s/BCB0.BIN",staging); FILE *f=fopen(path,"wb"); assert(f && fwrite(wire,1,sizeof(wire),f)==sizeof(wire)); fclose(f);
  }
  assert(!vg_ota_service_start());
  uint64_t until=now()+32000;
  struct vg_ota_staging_info info; struct vg_ota_service_status status;
  if(!strcmp(argv[3],"normal")) {
    do { vg_ota_staging_get_info(&info); if(info.state==VG_OTA_STATE_READY_AWAITING_CONFIRM) break; usleep(20000); } while(now()<until);
    assert(info.state==VG_OTA_STATE_READY_AWAITING_CONFIRM && !strcmp(info.task_id,"TASK-SERVICE-09"));
    snprintf(path,sizeof(path),"%s/BCB0.BIN",staging); assert(access(path,F_OK)!=0);
    snprintf(path,sizeof(path),"%s/BCB1.BIN",staging); assert(access(path,F_OK)!=0);
    usleep(2500000);
    /* Host-only cancellation at the real service seam; no board input is simulated. */
    assert(!vg_ota_staging_decline(info.task_id,"user_declined"));
  } else usleep(2500000);
  do {
    vg_ota_service_status(&status);
    if(!status.active && status.online && vg_ota_outbox_space()==8) break;
    usleep(20000);
  } while(now()<until);
  assert(!status.active && status.online && vg_ota_outbox_space()==8);
  if(!boot) {
    assert(vg_ota_staging_is_suppressed("TASK-SERVICE-09"));
    vg_ota_staging_get_info(&info); assert(info.state!=VG_OTA_STATE_READY_AWAITING_CONFIRM);
    snprintf(path,sizeof(path),"%s/BCB0.BIN",staging); assert(access(path,F_OK)!=0);
    snprintf(path,sizeof(path),"%s/BCB1.BIN",staging); assert(access(path,F_OK)!=0);
  }
  printf("service helper: OK mode=%s (real MQTTS+HTTPS, durable reports, no install authorization)\n",argv[3]);
  return 0;
}
