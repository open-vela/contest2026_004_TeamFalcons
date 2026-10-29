/* SPDX-License-Identifier: Apache-2.0 */
#include "vg_ota_protocol.h"
#include "vg_fw_identity.h"
#include "vg_ota_manifest.h"
#ifdef __NuttX__
#include <netutils/cJSON.h>
#else
#include "cJSON.h"
#endif
#include "vg_ota_json.h"
#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <limits.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

int vg_ota_identifier(const char *s,size_t limit)
{
  if(!s || !s[0] || strnlen(s,limit)>=limit) return 0;
  for(;*s;s++) if(!(isalnum((unsigned char)*s) || *s=='_' || *s=='-' || *s=='.' || *s==':')) return 0;
  return 1;
}
static int reject(char *error,size_t n,const char *reason)
{ if(error && n) snprintf(error,n,"%s",reason); return -EINVAL; }
static int hex(char c)
{ return c>='0' && c<='9' ? c-'0' : c>='a' && c<='f' ? c-'a'+10 : c>='A' && c<='F' ? c-'A'+10 : -1; }
int vg_ota_offer_parse(const char *topic,const void *payload,size_t length,
                       const char *device_id,int secure,int egress,int retain,int qos,
                       struct vg_ota_offer *offer,char *error,size_t error_size)
{
  static const char *const names[]={"schema","device_id","task_id","version","manifest_url","image_url","image_size","sha256","expires_at"};
  cJSON *root=NULL,*item; const cJSON *fields[9]={0}; unsigned seen=0;
  char expected[VG_OTA_TOPIC_MAX],*copy=NULL;
  struct vg_ota_url url;
  int ret=-EINVAL;
  if(!secure) return reject(error,error_size,"secure_transport_required");
  if(egress!=1) return reject(error,error_size,egress==2 ? "wifi_not_supported" : "rj45_unavailable");
  if(retain || qos!=1) return reject(error,error_size,"offer_delivery_invalid");
  if(!topic || !payload || !offer || !vg_ota_identifier(device_id,40) || !length || length>VG_OTA_PAYLOAD_MAX || vg_ota_json_guard(payload,length)) return reject(error,error_size,"offer_invalid");
  snprintf(expected,sizeof(expected),"vg/%s/ota/offer",device_id);
  if(strcmp(topic,expected)) return reject(error,error_size,"ota_topic_forbidden");
  copy=malloc(length+1);
  if(!copy) return -ENOMEM;
  memcpy(copy,payload,length); copy[length]=0;
  root=cJSON_ParseWithOpts(copy,NULL,1);
  if(!cJSON_IsObject(root)) goto done;
  cJSON_ArrayForEach(item,root) {
    unsigned i;
    for(i=0;i<9;i++) if(item->string && !strcmp(item->string,names[i])) break;
    if(i==9 || (seen&(1u<<i))) goto done;
    seen|=1u<<i; fields[i]=item;
  }
  if(seen!=0x1ffu || !cJSON_IsNumber(fields[0]) || fields[0]->valuedouble!=1 ||
      !cJSON_IsString(fields[1]) || strcmp(fields[1]->valuestring,device_id)) goto done;
  for(unsigned i=2;i<=5;i++) if(!cJSON_IsString(fields[i])) goto done;
  if(!vg_ota_identifier(fields[2]->valuestring,sizeof(offer->task_id)) ||
      !vg_ota_identifier(fields[3]->valuestring,sizeof(offer->version)) || !vg_fw_version_valid(fields[3]->valuestring) ||
      vg_ota_url_parse(fields[4]->valuestring,&url) || vg_ota_url_parse(fields[5]->valuestring,&url)) goto done;
  if(!cJSON_IsNumber(fields[6]) || !isfinite(fields[6]->valuedouble) || fields[6]->valuedouble<8 ||
      fields[6]->valuedouble>VG_OTA_IMAGE_MAX_BYTES || floor(fields[6]->valuedouble)!=fields[6]->valuedouble ||
      !cJSON_IsString(fields[7]) || strlen(fields[7]->valuestring)!=64 ||
      !cJSON_IsNumber(fields[8]) || !isfinite(fields[8]->valuedouble) || fields[8]->valuedouble<1767225600 ||
      fields[8]->valuedouble>9007199254740991.0 || floor(fields[8]->valuedouble)!=fields[8]->valuedouble) goto done;
  memset(offer,0,sizeof(*offer));
  snprintf(offer->task_id,sizeof(offer->task_id),"%s",fields[2]->valuestring);
  snprintf(offer->version,sizeof(offer->version),"%s",fields[3]->valuestring);
  snprintf(offer->manifest_url,sizeof(offer->manifest_url),"%s",fields[4]->valuestring);
  snprintf(offer->image_url,sizeof(offer->image_url),"%s",fields[5]->valuestring);
  offer->image_size=(uint32_t)fields[6]->valuedouble;
  offer->expires_at=(uint64_t)fields[8]->valuedouble;
  for(unsigned i=0;i<32;i++) {
    int a=hex(fields[7]->valuestring[i*2]),b=hex(fields[7]->valuestring[i*2+1]);
    if(a<0 || b<0) goto done;
    offer->sha256[i]=(uint8_t)((a<<4)|b);
  }
  ret=0;
done:
  cJSON_Delete(root); free(copy);
  if(ret) return reject(error,error_size,"offer_invalid");
  if(error && error_size) error[0]=0;
  return 0;
}
static cJSON *base(const char *id,const char *task,const char *version,const char *slot,const char *error)
{
  cJSON *r=cJSON_CreateObject();
  if(!r) return NULL;
  if(!cJSON_AddNumberToObject(r,"schema",1) || !cJSON_AddStringToObject(r,"device_id",id ? id : "") ||
     !cJSON_AddStringToObject(r,"task_id",task ? task : "") || !cJSON_AddStringToObject(r,"version",version ? version : "") ||
     !cJSON_AddStringToObject(r,"slot",slot ? slot : "") || !cJSON_AddStringToObject(r,"error_code",error ? error : "none")) {
    cJSON_Delete(r); return NULL;
  }
  return r;
}
int vg_ota_status_json(char *out,size_t n,const char *id,const char *task,const char *version,const char *state,
                       const char *slot,uint32_t bytes,uint32_t size,const char *error)
{
  cJSON *r=base(id,task,version,slot,error); int ret=-ENOMEM;
  if(!r) return ret;
  unsigned progress=size ? (unsigned)((uint64_t)bytes*100u/size) : 0;
  if(progress>100) progress=100;
  if(cJSON_AddStringToObject(r,"state",state) && cJSON_AddNumberToObject(r,"downloaded_bytes",bytes) &&
     cJSON_AddNumberToObject(r,"progress",progress) && n<=INT_MAX && cJSON_PrintPreallocated(r,out,(int)n,0)) ret=0;
  cJSON_Delete(r); return ret;
}
int vg_ota_result_json(char *out,size_t n,const char *id,const char *task,const char *version,const char *result,
                       const char *slot,const char *error,const char *self_check)
{
  cJSON *r=base(id,task,version,slot,error); int ret=-ENOMEM;
  if(!r) return ret;
  if(cJSON_AddStringToObject(r,"result",result) && (!self_check || cJSON_AddStringToObject(r,"self_check",self_check)) &&
     n<=INT_MAX && cJSON_PrintPreallocated(r,out,(int)n,0)) ret=0;
  cJSON_Delete(r); return ret;
}
