/* SPDX-License-Identifier: Apache-2.0 */
#ifdef __NuttX__
#include <nuttx/config.h>
#endif
#include "vg_ota_service.h"
#include "vg_ota_link.h"
#include "vg_ota_config.h"
#include "vg_ota_download.h"
#include "vg_ota_https.h"
#include "vg_ota_mqtt.h"
#include "vg_ota_protocol.h"
#include "vg_ota_outbox.h"
#include "vg_ota_staging.h"
#include "vg_ota_self_check.h"
#include "vg_ota_boot.h"
#include "vg_ota_trust.h"
#include "vg_device_id.h"
#include "vg_fw_identity.h"
#ifdef __NuttX__
#include "vg_net_mgr.h"
#include "vg_ota_mbox.h"
#endif
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

struct service {
  pthread_mutex_t lock;
  int running,online,recovered;
  int active,queued,downloading,event_pending;
  struct vg_ota_offer offer;
  struct vg_ota_config config;
  struct vg_ota_event pending_event;
  char error[64];
};
static struct service g_service={.lock=PTHREAD_MUTEX_INITIALIZER};
static pthread_mutex_t g_start_lock=PTHREAD_MUTEX_INITIALIZER;
static void clear_memory(void *memory,size_t n)
{ volatile unsigned char *p=memory; while(n--) *p++=0; }
struct download_context { struct vg_ota_offer offer; char hosts[2][102]; unsigned host_count; struct vg_ota_https https; };
static uint64_t monotonic(void *ctx)
{ struct timespec ts; (void)ctx; clock_gettime(CLOCK_MONOTONIC,&ts); return (uint64_t)ts.tv_sec*1000+ts.tv_nsec/1000000; }
static uint64_t wall(void *ctx) { (void)ctx; return (uint64_t)time(NULL); }
#ifdef __NuttX__
int vg_ota_service_egress(void)
{
  struct vg_net_live_status net;
  if(vg_net_mgr_status(&net)) return 0;
  if(net.egress==VG_EGRESS_WIFI) return 2;
  return net.egress==VG_EGRESS_RJ45 && net.rj45_has_ip && vg_ota_link_ready() ? 1 : 0;
}
#endif
static int egress(void *ctx) { (void)ctx; return vg_ota_service_egress(); }
static void delay(void *ctx,uint32_t ms) { (void)ctx; usleep(ms*1000u); }
static int http_open(void *ctx,const char *url,uint32_t offset,const char *etag,struct vg_ota_http_response *r)
{ return vg_ota_https_open(&((struct download_context *)ctx)->https,url,offset,etag,r); }
static ssize_t http_read(void *ctx,uint8_t *bytes,size_t n)
{ return vg_ota_https_read(&((struct download_context *)ctx)->https,bytes,n); }
static void http_close(void *ctx) { vg_ota_https_close(&((struct download_context *)ctx)->https); }
static void safe_error(char *out,size_t n,const char *reason)
{ snprintf(out,n,"%s",vg_ota_identifier(reason,64) ? reason : "ota_failed"); }
static void make_event(struct vg_ota_event *e,const char *task,const char *version,const char *result,const char *error,const char *self_check)
{
  memset(e,0,sizeof(*e));
  snprintf(e->task_id,sizeof(e->task_id),"%s",task);
  snprintf(e->version,sizeof(e->version),"%s",version);
  snprintf(e->result,sizeof(e->result),"%s",result);
  safe_error(e->error,sizeof(e->error),error);
  snprintf(e->self_check,sizeof(e->self_check),"%s",self_check ? self_check : "");
  e->slot=vg_fw_slot();
}
static void *download_worker(void *arg)
{
  struct download_context *c=arg;
  const char *hosts[]={c->hosts[0],c->hosts[1]};
  struct vg_ota_download_ports ports={c,http_open,http_read,http_close,monotonic,wall,egress,delay};
  char error[64]={0};
  vg_ota_https_init(&c->https,vg_ota_trust_ca());
  int ret=vg_ota_download_run(&c->offer,hosts,c->host_count,&ports,error,sizeof(error));
  http_close(c);
  if(ret) vg_ota_staging_fail(c->offer.task_id,c->offer.version,error[0] ? error : "download_failed");
  pthread_mutex_lock(&g_service.lock);
  g_service.downloading=0;
  memset(g_service.offer.image_url,0,sizeof(g_service.offer.image_url));
  memset(g_service.offer.manifest_url,0,sizeof(g_service.offer.manifest_url));
  if(ret) {
    make_event(&g_service.pending_event,c->offer.task_id,c->offer.version,"failed",error[0] ? error : "download_failed",NULL);
    g_service.event_pending=1;
    safe_error(g_service.error,sizeof(g_service.error),error[0] ? error : "download_failed");
  } else g_service.error[0]=0;
  pthread_mutex_unlock(&g_service.lock);
  clear_memory(c,sizeof(*c)); free(c);
  return NULL;
}
static void inbound(void *ctx,const char *topic,const void *payload,size_t length,int retain,int qos)
{
  struct vg_ota_offer *offer=calloc(1,sizeof(*offer));
  char error[64]={0};
  (void)ctx;
  if(!offer) return;
  int ret=vg_ota_offer_parse(topic,payload,length,vg_device_id(),1,vg_ota_service_egress(),retain,qos,offer,error,sizeof(error));
  if(!ret && vg_ota_staging_is_suppressed(offer->task_id)) { ret=-EALREADY; strcpy(error,"task_suppressed"); }
  if(!ret && vg_ota_outbox_space()<1) { ret=-ENOSPC; strcpy(error,"result_queue_unavailable"); }
  pthread_mutex_lock(&g_service.lock);
  if(!ret && g_service.active) { ret=-EBUSY; strcpy(error,"ota_busy"); }
  if(ret) safe_error(g_service.error,sizeof(g_service.error),error[0] ? error : "offer_invalid");
  else {
    g_service.offer=*offer; g_service.active=1; g_service.queued=1;
    g_service.error[0]=0;
  }
  pthread_mutex_unlock(&g_service.lock);
  memset(offer,0,sizeof(*offer)); free(offer);
}
static void start_queued(void)
{
  struct download_context *context=NULL; pthread_t thread; pthread_attr_t attr;
  pthread_mutex_lock(&g_service.lock);
  if(g_service.queued && !g_service.downloading &&
     (wall(NULL)>=g_service.offer.expires_at || wall(NULL)<1767225600u)) {
    make_event(&g_service.pending_event,g_service.offer.task_id,g_service.offer.version,
               "failed","queued_offer_expired",NULL);
    g_service.event_pending=1; g_service.queued=0;
    memset(g_service.offer.image_url,0,sizeof(g_service.offer.image_url));
    memset(g_service.offer.manifest_url,0,sizeof(g_service.offer.manifest_url));
  }
  if(g_service.queued && !g_service.downloading) {
    context=calloc(1,sizeof(*context));
    if(context) {
      context->offer=g_service.offer;
      memcpy(context->hosts,g_service.config.allowed_hosts,sizeof(context->hosts));
      context->host_count=g_service.config.allowed_count;
      g_service.queued=0; g_service.downloading=1;
    }
  }
  pthread_mutex_unlock(&g_service.lock);
  if(!context) return;
  pthread_attr_init(&attr); pthread_attr_setstacksize(&attr,24576);
  int ret=pthread_create(&thread,&attr,download_worker,context);
  pthread_attr_destroy(&attr);
  if(!ret) pthread_detach(thread);
  else {
    pthread_mutex_lock(&g_service.lock);
    g_service.downloading=0;
    make_event(&g_service.pending_event,context->offer.task_id,context->offer.version,"failed","download_worker_failed",NULL);
    g_service.event_pending=1;
    pthread_mutex_unlock(&g_service.lock);
    clear_memory(context,sizeof(*context)); free(context);
  }
}
static void collect_terminal(void)
{
  struct vg_ota_staging_info info;
  vg_ota_staging_get_info(&info);
  pthread_mutex_lock(&g_service.lock);
  if(g_service.active && !g_service.downloading && !g_service.queued && !g_service.event_pending &&
     !strcmp(info.task_id,g_service.offer.task_id) &&
     (info.state==VG_OTA_STATE_DECLINED || info.state==VG_OTA_STATE_EXPIRED)) {
    make_event(&g_service.pending_event,info.task_id,info.version,
                info.state==VG_OTA_STATE_DECLINED ? "declined" : "expired",info.error_code,NULL);
    g_service.event_pending=1;
  }
  int pending=g_service.event_pending;
  struct vg_ota_event event=g_service.pending_event;
  pthread_mutex_unlock(&g_service.lock);
  int saved=pending ? vg_ota_outbox_enqueue(&event) : 0;
  if(pending && saved) {
    pthread_mutex_lock(&g_service.lock);
    strcpy(g_service.error,"result_queue_store_failed");
    pthread_mutex_unlock(&g_service.lock);
  }
  if(pending && saved==0 && vg_ota_staging_remember(event.task_id)!=0) {
    pthread_mutex_lock(&g_service.lock); strcpy(g_service.error,"task_journal_store_failed"); pthread_mutex_unlock(&g_service.lock);
    return;
  }
  if(pending && saved==0) {
    pthread_mutex_lock(&g_service.lock);
    g_service.event_pending=0; g_service.active=0;
    pthread_mutex_unlock(&g_service.lock);
  }
}
static void recovered_event(const struct vg_ota_event *event)
{
  if(vg_ota_outbox_enqueue(event)==0 && vg_ota_staging_remember(event->task_id)==0) return;
  /* Keep the boot result until the durable queue and replay journal both
   * succeed. The control worker drains older events before admitting offers. */
  pthread_mutex_lock(&g_service.lock);
  g_service.pending_event=*event; g_service.event_pending=1; g_service.active=1;
  snprintf(g_service.offer.task_id,sizeof(g_service.offer.task_id),"%s",event->task_id);
  snprintf(g_service.offer.version,sizeof(g_service.offer.version),"%s",event->version);
  strcpy(g_service.error,"boot_result_store_pending");
  pthread_mutex_unlock(&g_service.lock);
}
static void recover_result(void)
{
  struct vg_ota_self_check_status check;
  struct vg_ota_event event;
  char line[256]={0},state[32],task[40],version[32],path[160];
  snprintf(path,sizeof(path),"%s/STATE.BIN",vg_ota_staging_get_dir());
  int fd=open(path,O_RDONLY);
  if(fd>=0) { ssize_t n=read(fd,line,sizeof(line)-1); close(fd); if(n<0) line[0]=0; }
  if(vg_ota_self_check_get_status(&check)==0 && vg_ota_identifier(check.active_task_id,40) &&
     vg_ota_identifier(check.active_version,32) && strcmp(check.active_task_id,"FACTORY")) {
    if(vg_fw_slot()=='B' && check.active_bcb_state==VG_OTA_BCB_STATE_CONFIRMED) {
      make_event(&event,check.active_task_id,check.active_version,"installed","none","passed");
      recovered_event(&event); return;
    }
    if(vg_fw_slot()=='A' && check.active_bcb_state==VG_OTA_BCB_STATE_FAILED) {
      const char *result=strstr(line,"result=self_check_failed") ? "self_check_failed" : "rolled_back";
#ifdef __NuttX__
      if(VG_OTA_MBOX->magic==VG_OTA_MBOX_MAGIC && VG_OTA_MBOX->boot_reason==VG_OTA_BOOT_REASON_A_INSTALL_FAILED) result="failed";
#endif
      make_event(&event,check.active_task_id,check.active_version,result,"boot_failed","failed");
      recovered_event(&event); return;
    }
  }
  if(sscanf(line,"state=%31s task=%39s ver=%31s",state,task,version)==3 &&
      vg_ota_identifier(task,40) && vg_ota_identifier(version,32)) {
    if(!strcmp(state,"DECLINED") || !strcmp(state,"EXPIRED") || !strcmp(state,"FAILED")) {
      const char *result=!strcmp(state,"DECLINED") ? "declined" : !strcmp(state,"EXPIRED") ? "expired" : "failed";
      make_event(&event,task,version,result,"recovered_after_restart",NULL);
      recovered_event(&event);
    } else if(!strcmp(state,"READY_AWAITING_CONFIRM") || !strcmp(state,"DOWNLOADING") || !strcmp(state,"VERIFYING")) {
      vg_ota_staging_fail(task,version,"interrupted_before_confirmation");
      make_event(&event,task,version,"failed","interrupted_before_confirmation",NULL);
      recovered_event(&event);
    }
  }
}
static void status_json(char *out,size_t capacity)
{
  struct vg_ota_staging_info info;
  char task[41],version[33],error[64]; int active;
  vg_ota_staging_get_info(&info);
  pthread_mutex_lock(&g_service.lock);
  active=g_service.active;
  snprintf(task,sizeof(task),"%s",active ? g_service.offer.task_id : info.task_id);
  snprintf(version,sizeof(version),"%s",active ? g_service.offer.version : info.version);
  snprintf(error,sizeof(error),"%s",g_service.error[0] ? g_service.error : info.error_code[0] ? info.error_code : "none");
  pthread_mutex_unlock(&g_service.lock);
  int fresh=active && strcmp(info.task_id,task);
  const char *state=fresh ? "OFFER_RECEIVED" : vg_ota_state_name(info.state);
  if(fresh) { info.downloaded_bytes=0; info.image_size=0; }
  if(vg_ota_status_json(out,capacity,vg_device_id(),task,version,state,vg_fw_slot_name(),info.downloaded_bytes,info.image_size,error)) out[0]=0;
}
static void *control_worker(void *arg)
{
  uint64_t reconnect_at=0,next_status=0,ticket_since=0;
  unsigned backoff=0,pending_slot=0,pending_bit=0;
  uint16_t ticket=0;
  int ticket_is_event=0;
  char json[1024];
  (void)arg;
  for(;;) {
    collect_terminal(); start_queued();
    if(vg_ota_service_egress()!=1) {
      vg_ota_mqtt_close(); ticket=0;
      pthread_mutex_lock(&g_service.lock); g_service.online=0;
      strcpy(g_service.error,vg_ota_service_egress()==2 ? "wifi_not_supported" : "rj45_unavailable");
      pthread_mutex_unlock(&g_service.lock);
      usleep(500000); continue;
    }
    uint64_t now=monotonic(NULL);
    if(!vg_ota_mqtt_online()) {
      if(now<reconnect_at) { usleep(100000); continue; }
      int ret=vg_ota_mqtt_open(&g_service.config,vg_device_id(),inbound,NULL);
      pthread_mutex_lock(&g_service.lock); g_service.online=!ret;
      if(!ret) g_service.error[0]=0;
      if(ret) strcpy(g_service.error,ret==-EKEYREJECTED ? "tls_verification_failed" : ret==-EACCES ? "mqtt_auth_failed" : "mqtt_connect_failed");
      pthread_mutex_unlock(&g_service.lock);
      if(ret) { reconnect_at=now+(1000u<<(backoff<5 ? backoff++ : 5)); usleep(100000); continue; }
      ticket=0; backoff=0; next_status=0;
    }
    if(vg_ota_mqtt_poll()) { ticket=0; reconnect_at=now+1000; continue; }
    if(ticket && now-ticket_since>=15000u) {
      vg_ota_mqtt_close(); ticket=0; reconnect_at=now+1000;
      pthread_mutex_lock(&g_service.lock); strcpy(g_service.error,"mqtt_puback_timeout"); pthread_mutex_unlock(&g_service.lock);
      continue;
    }
    if(ticket) {
      int acked=vg_ota_mqtt_acked(ticket);
      if(acked>0) {
        if(ticket_is_event && vg_ota_outbox_ack(pending_slot,pending_bit)) {
          pthread_mutex_lock(&g_service.lock); strcpy(g_service.error,"result_ack_store_failed"); pthread_mutex_unlock(&g_service.lock);
        }
        ticket=0;
      }
      if(ticket) { usleep(100000); continue; }
    }
    struct vg_ota_event event;
    unsigned slot;
    if(vg_ota_outbox_peek(&event,&slot)==0) {
      unsigned bit=(event.acknowledged&1) ? 2 : 1;
      if(!vg_ota_result_json(json,sizeof(json),vg_device_id(),event.task_id,event.version,event.result,
                             event.slot=='B' ? "B" : "A",event.error,bit==2 ? event.self_check : NULL) &&
          !vg_ota_mqtt_publish(bit==2 ? VG_OTA_MQTT_CONFIRM : VG_OTA_MQTT_RESULT,json,&ticket)) {
        pending_slot=slot; pending_bit=bit; ticket_is_event=1; ticket_since=now;
      }
    } else if(now>=next_status) {
      status_json(json,sizeof(json));
      if(json[0] && !vg_ota_mqtt_publish(VG_OTA_MQTT_STATUS,json,&ticket)) { ticket_is_event=0; ticket_since=now; }
      next_status=now+2000u;
    }
    usleep(100000);
  }
  return NULL;
}
static int start_service_locked(void)
{
  pthread_t thread; pthread_attr_t attr;
  pthread_mutex_lock(&g_service.lock);
  if(g_service.running) { pthread_mutex_unlock(&g_service.lock); return 0; }
  int recover=!g_service.recovered; g_service.recovered=1;
  pthread_mutex_unlock(&g_service.lock);
  if(recover) recover_result();
  struct vg_ota_config *config=calloc(1,sizeof(*config));
  if(!config) return -ENOMEM;
  int ret=vg_ota_config_load(config);
  if(!ret && !vg_ota_trust_configured()) ret=-ENOKEY;
  if(!ret && !vg_ota_tls_entropy_ready()) ret=-EAGAIN;
  if(ret) {
    pthread_mutex_lock(&g_service.lock);
    strcpy(g_service.error,ret==-ENOKEY ? "trust_unconfigured" : ret==-EAGAIN ? "entropy_unverified" : "credentials_unconfigured");
    pthread_mutex_unlock(&g_service.lock);
    clear_memory(config,sizeof(*config)); free(config); return ret;
  }
  pthread_mutex_lock(&g_service.lock);
  g_service.config=*config; g_service.running=1; g_service.error[0]=0;
  pthread_mutex_unlock(&g_service.lock);
  clear_memory(config,sizeof(*config)); free(config);
  pthread_attr_init(&attr); pthread_attr_setstacksize(&attr,24576);
  ret=pthread_create(&thread,&attr,control_worker,NULL);
  pthread_attr_destroy(&attr);
  if(!ret) pthread_detach(thread);
  else {
    pthread_mutex_lock(&g_service.lock); g_service.running=0;
    memset(&g_service.config,0,sizeof(g_service.config)); pthread_mutex_unlock(&g_service.lock);
  }
  return ret ? -ret : 0;
}
int vg_ota_service_status(struct vg_ota_service_status *out)
{
  if(!out) return -EINVAL;
  pthread_mutex_lock(&g_service.lock);
  out->running=g_service.running; out->online=g_service.online;
  out->active=g_service.active; out->downloading=g_service.downloading;
  snprintf(out->error,sizeof(out->error),"%s",g_service.error);
  pthread_mutex_unlock(&g_service.lock);
  return 0;
}
void vg_ota_service_print(FILE *out)
{
  struct vg_ota_service_status status;
  vg_ota_service_status(&status);
  fprintf(out,"vgota: control running=%d online=%d active=%d downloading=%d error=%s\n",status.running,status.online,
          status.active,status.downloading,status.error[0] ? status.error : "none");
}

int vg_ota_service_start(void)
{
  pthread_mutex_lock(&g_start_lock);
  int ret=start_service_locked();
  pthread_mutex_unlock(&g_start_lock);
  return ret;
}
