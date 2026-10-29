#include "vg_ota_mqtt.h"
#include "vg_ota_protocol.h"
#include <mqtt.h>
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
static int accepted,rejected;
int vg_mqtt_pal_try_sendall(int fd,const void *bytes,size_t n,int flags,ssize_t *out)
{ (void)flags; return vg_ota_mqtt_pal_send(fd,bytes,n,out); }
int vg_mqtt_pal_try_recvall(int fd,void *bytes,size_t n,int flags,ssize_t *out)
{ (void)flags; return vg_ota_mqtt_pal_recv(fd,bytes,n,out); }
static void offer(void *ctx,const char *topic,const void *payload,size_t n,int retain,int qos)
{
  struct vg_ota_offer parsed; char error[64]; (void)ctx;
  if(!vg_ota_offer_parse(topic,payload,n,"vg-test",1,1,retain,qos,&parsed,error,sizeof(error))) accepted++;
  else rejected++;
}
static uint64_t now(void)
{ struct timespec t; clock_gettime(CLOCK_MONOTONIC,&t); return (uint64_t)t.tv_sec*1000+t.tv_nsec/1000000; }
int main(int argc,char **argv)
{
  struct vg_ota_config config={.broker="localhost",.username="ota-unit",.password="ota-synthetic-password"};
  assert(argc==4); snprintf(config.port,sizeof(config.port),"%s",argv[1]);
  if(!strcmp(argv[2],"wrong-auth")) strcpy(config.password,"wrong-synthetic-password");
  if(!strcmp(argv[2],"wrong-host")) strcpy(config.broker,"127.0.0.1");
  int ret=vg_ota_mqtt_open(&config,"vg-test",offer,NULL);
  if(atoi(argv[3])) {
    assert(ret<0 && !vg_ota_mqtt_online());
    printf("mqtt helper: rejected err=%d\n",ret); return 0;
  }
  assert(!ret && vg_ota_mqtt_online());
  uint64_t until=now()+3000;
  while((accepted<1 || rejected<2) && now()<until) { assert(!vg_ota_mqtt_poll()); usleep(10000); }
  assert(accepted==1 && rejected==2);
  char json[512];
  for(unsigned kind=0;kind<3;kind++) {
    if(kind==0) assert(!vg_ota_status_json(json,sizeof(json),"vg-test","TASK-09","0.2.0","READY_AWAITING_CONFIRM","A",16384,16384,"none"));
    else assert(!vg_ota_result_json(json,sizeof(json),"vg-test","TASK-09","0.2.0","installed","B","none",kind==2 ? "passed" : NULL));
    uint16_t ticket;
    assert(!vg_ota_mqtt_publish((enum vg_ota_mqtt_kind)kind,json,&ticket));
    until=now()+3000;
    while(vg_ota_mqtt_acked(ticket)==0 && now()<until) { assert(!vg_ota_mqtt_poll()); usleep(10000); }
    assert(vg_ota_mqtt_acked(ticket)==1);
  }
  vg_ota_mqtt_close();
  assert(!vg_ota_mqtt_open(&config,"vg-test",offer,NULL));
  until=now()+3000;
  while(accepted<2 && now()<until) { assert(!vg_ota_mqtt_poll()); usleep(10000); }
  assert(accepted==2);
  vg_ota_mqtt_close(); memset(&config,0,sizeof(config));
  puts("mqtt helper: OK (real TLS, auth, subscribe, QoS1/PUBACK, reconnect; remote confirmation rejected)");
  return 0;
}
