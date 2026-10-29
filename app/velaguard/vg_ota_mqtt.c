/* SPDX-License-Identifier: Apache-2.0 */
#ifdef __NuttX__
#include <nuttx/config.h>
#endif
#include "vg_ota_mqtt.h"
#include "vg_ota_https.h"
#include "vg_ota_protocol.h"
#include "vg_ota_trust.h"
#include <mqtt.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static struct mqtt_client g_client;
static struct vg_ota_https g_tls;
static uint8_t g_tx[3072],g_rx[2048];
static char g_device[40];
static int g_initialized,g_online;
static vg_ota_mqtt_offer_fn g_callback;
static void *g_context;

int vg_ota_mqtt_pal_send(int fd,const void *bytes,size_t length,ssize_t *out)
{
  if(fd!=VG_OTA_MQTT_HANDLE || !out) return -1;
  size_t done=0;
  while(done<length) {
    ssize_t sent=vg_ota_tls_send(&g_tls,(const uint8_t *)bytes+done,length-done);
    if(sent<=0) { *out=MQTT_ERROR_SOCKET_ERROR; return 0; }
    done+=(size_t)sent;
  }
  *out=(ssize_t)done; return 0;
}
int vg_ota_mqtt_pal_recv(int fd,void *bytes,size_t length,ssize_t *out)
{
  if(fd!=VG_OTA_MQTT_HANDLE || !out) return -1;
  ssize_t got=vg_ota_tls_receive(&g_tls,bytes,length,20);
  *out=got==-EAGAIN ? 0 : got<0 ? MQTT_ERROR_SOCKET_ERROR : got;
  return 0;
}
static void inbound(void **state,struct mqtt_response_publish *publish)
{
  char topic[VG_OTA_TOPIC_MAX];
  (void)state;
  if(!g_callback || publish->topic_name_size>=sizeof(topic) || publish->application_message_size>VG_OTA_PAYLOAD_MAX) return;
  memcpy(topic,publish->topic_name,publish->topic_name_size); topic[publish->topic_name_size]=0;
  g_callback(g_context,topic,publish->application_message,publish->application_message_size,
             publish->retain_flag,publish->qos_level);
}
void vg_ota_mqtt_close(void)
{
  vg_ota_https_close(&g_tls);
  g_online=0;
  memset(g_tx,0,sizeof(g_tx)); memset(g_rx,0,sizeof(g_rx));
}
int vg_ota_mqtt_open(const struct vg_ota_config *config,const char *device_id,
                      vg_ota_mqtt_offer_fn callback,void *ctx)
{
  char topic[VG_OTA_TOPIC_MAX],client_id[48];
  int ret;
  if(!config || !vg_ota_identifier(device_id,sizeof(g_device)) || !callback) return -EINVAL;
  vg_ota_mqtt_close();
  vg_ota_https_init(&g_tls,vg_ota_trust_ca());
  ret=vg_ota_tls_connect(&g_tls,config->broker,config->port);
  if(ret) return ret;
  snprintf(g_device,sizeof(g_device),"%s",device_id);
  snprintf(client_id,sizeof(client_id),"%s-ota",device_id);
  g_callback=callback; g_context=ctx;
  if(!g_initialized) {
    mqtt_init(&g_client,VG_OTA_MQTT_HANDLE,g_tx,sizeof(g_tx),g_rx,sizeof(g_rx),inbound);
    g_initialized=1;
  } else mqtt_reinit(&g_client,VG_OTA_MQTT_HANDLE,g_tx,sizeof(g_tx),g_rx,sizeof(g_rx));
  g_client.send_offset=0;
  g_client.response_timeout=10;
  ret=mqtt_connect(&g_client,client_id,NULL,NULL,0,config->username,config->password,MQTT_CONNECT_CLEAN_SESSION,30);
  if(ret!=MQTT_OK) goto failed;
  for(unsigned i=0;i<100 && !g_client.event_connect && g_client.error==MQTT_OK;i++) {
    mqtt_sync(&g_client); usleep(20000);
  }
  if(!g_client.event_connect || g_client.error!=MQTT_OK) { ret=-EACCES; goto failed; }
  snprintf(topic,sizeof(topic),"vg/%s/ota/offer",device_id);
  ret=mqtt_subscribe(&g_client,topic,1);
  if(ret!=MQTT_OK) goto failed;
  uint16_t subscribe_ticket=g_client.pid_lfsr;
  int subscribed=0;
  for(unsigned i=0;i<100 && g_client.error==MQTT_OK;i++) {
    mqtt_sync(&g_client);
    struct mqtt_queued_message *queued=mqtt_mq_find(&g_client.mq,MQTT_CONTROL_SUBSCRIBE,&subscribe_ticket);
    if(!queued || queued->state==MQTT_QUEUED_COMPLETE) { subscribed=1; break; }
    usleep(20000);
  }
  if(g_client.error!=MQTT_OK || !subscribed) { ret=-EIO; goto failed; }
  g_online=1;
  return 0;
failed:
  vg_ota_mqtt_close();
  return ret<0 ? ret : -EIO;
}
int vg_ota_mqtt_poll(void)
{
  if(!g_online) return -ENOTCONN;
  if(mqtt_sync(&g_client)!=MQTT_OK || g_client.error!=MQTT_OK) { vg_ota_mqtt_close(); return -EIO; }
  return 0;
}
int vg_ota_mqtt_publish(enum vg_ota_mqtt_kind kind,const char *json,uint16_t *ticket)
{
  static const char *names[]={"status","result","confirm"};
  char topic[VG_OTA_TOPIC_MAX];
  if(!g_online || !json || strlen(json)>1024 || (unsigned)kind>2) return -EINVAL;
  snprintf(topic,sizeof(topic),"vg/%s/ota/%s",g_device,names[kind]);
  if(mqtt_publish(&g_client,topic,json,strlen(json),MQTT_PUBLISH_QOS_1)!=MQTT_OK) return -EIO;
  if(ticket) *ticket=g_client.pid_lfsr;
  return 0;
}
int vg_ota_mqtt_online(void) { return g_online; }

int vg_ota_mqtt_acked(uint16_t ticket)
{
  if(!g_online || !ticket) return -ENOTCONN;
  struct mqtt_queued_message *message=mqtt_mq_find(&g_client.mq,MQTT_CONTROL_PUBLISH,&ticket);
  return !message || message->state==MQTT_QUEUED_COMPLETE;
}
