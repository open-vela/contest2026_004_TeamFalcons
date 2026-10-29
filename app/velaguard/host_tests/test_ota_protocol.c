#include "vg_ota_protocol.h"
#include "vg_ota_json.h"
#include "vg_ota_outbox.h"
#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
static const char payload[]={"{\"schema\":1,\"device_id\":\"vg-test\",\"task_id\":\"TASK-09\",\"version\":\"0.2.0\",\"manifest_url\":\"https://updates.example/m?token=secret-query\",\"image_url\":\"https://updates.example/i?token=secret-query\",\"image_size\":16384,\"sha256\":\"0000000000000000000000000000000000000000000000000000000000000000\",\"expires_at\":1900000000}"};
int main(void)
{
  struct vg_ota_offer offer; char error[64],json[1536],changed[1536];
  assert(!vg_ota_offer_parse("vg/vg-test/ota/offer",payload,strlen(payload),"vg-test",1,1,0,1,&offer,error,sizeof(error)));
  assert(!strcmp(offer.task_id,"TASK-09") && offer.image_size==16384);
  assert(vg_ota_offer_parse("vg/vg-test/ota/confirm",payload,strlen(payload),"vg-test",1,1,0,1,&offer,error,sizeof(error))<0);
  assert(vg_ota_offer_parse("vg/vg-test/ota/offer",payload,strlen(payload),"vg-test",0,1,0,1,&offer,error,sizeof(error))<0);
  assert(!strcmp(error,"secure_transport_required"));
  assert(vg_ota_offer_parse("vg/vg-test/ota/offer",payload,strlen(payload),"vg-test",1,2,0,1,&offer,error,sizeof(error))<0);
  assert(!strcmp(error,"wifi_not_supported"));
  assert(vg_ota_offer_parse("vg/vg-test/ota/offer",payload,strlen(payload),"vg-test",1,1,1,1,&offer,error,sizeof(error))<0);
  assert(vg_ota_offer_parse("vg/vg-test/ota/offer",payload,strlen(payload),"vg-test",1,1,0,0,&offer,error,sizeof(error))<0);
  snprintf(changed,sizeof(changed),"{\"confirm\":true,%s",payload+1);
  assert(vg_ota_offer_parse("vg/vg-test/ota/offer",changed,strlen(changed),"vg-test",1,1,0,1,&offer,error,sizeof(error))<0);
  snprintf(changed,sizeof(changed),"{\"task_id\":\"duplicate\",%s",payload+1);
  assert(vg_ota_offer_parse("vg/vg-test/ota/offer",changed,strlen(changed),"vg-test",1,1,0,1,&offer,error,sizeof(error))<0);
  assert(vg_ota_json_guard("[[[[[1]]]]]",11)<0);
  assert(vg_ota_json_guard("{\"x\":\"a\\u0000b\"}",18)<0);
  assert(!vg_ota_status_json(json,sizeof(json),"vg-test","TASK-09","0.2.0","READY_AWAITING_CONFIRM","A",16384,16384,"none"));
  assert(!strstr(json,"secret-query") && !strstr(json,"https://") && !strstr(json,"password"));
  assert(!vg_ota_result_json(json,sizeof(json),"vg-test","TASK-09","0.2.0","installed","B","none","passed"));
  assert(strstr(json,"self_check") && !strstr(json,"confirm_install"));
  char temp[]="/tmp/vg-ota-outbox-XXXXXX",path[256]; assert(mkdtemp(temp)); vg_ota_outbox_set_dir(temp);
  struct vg_ota_event event={.task_id="TASK-09",.version="0.2.0",.result="installed",.error="none",.self_check="passed",.slot='B'};
  assert(vg_ota_outbox_space()==8 && !vg_ota_outbox_enqueue(&event));
  assert(!vg_ota_outbox_enqueue(&event) && vg_ota_outbox_space()==7);
  unsigned slot; struct vg_ota_event readback;
  assert(!vg_ota_outbox_peek(&readback,&slot) && !strcmp(readback.task_id,event.task_id));
  assert(!vg_ota_outbox_ack(slot,1));
  assert(!vg_ota_outbox_peek(&readback,&slot) && readback.acknowledged==1);
  vg_ota_outbox_set_dir(temp);
  assert(!vg_ota_outbox_peek(&readback,&slot) && readback.acknowledged==1);
  assert(!vg_ota_outbox_ack(slot,2) && vg_ota_outbox_space()==8);
  event.self_check[0]=0;
  for(unsigned i=0;i<8;i++) { snprintf(event.task_id,sizeof(event.task_id),"TASK-%u",i); assert(!vg_ota_outbox_enqueue(&event)); }
  strcpy(event.task_id,"FULL"); assert(vg_ota_outbox_enqueue(&event)==-ENOSPC);
  assert(!vg_ota_outbox_peek(&readback,&slot)); assert(!vg_ota_outbox_ack(slot,1));
  assert(vg_ota_outbox_space()==1);
  DIR *d=opendir(temp); struct dirent *entry; assert(d);
  while((entry=readdir(d))) if(entry->d_name[0]!='.') {
    assert(strlen(temp)+strlen(entry->d_name)+2<sizeof(path)); strcpy(path,temp); strcat(path,"/"); strcat(path,entry->d_name); unlink(path);
  }
  closedir(d); rmdir(temp);
  puts("test_ota_protocol: OK (strict offers, no remote confirm, bounded durable PUBACK outbox)");
  return 0;
}
