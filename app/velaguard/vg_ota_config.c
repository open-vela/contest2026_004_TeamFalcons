/* SPDX-License-Identifier: Apache-2.0 */
#include "vg_ota_config.h"
#include "vg_ota_http.h"
#ifdef __NuttX__
#include <netutils/cJSON.h>
#else
#include "cJSON.h"
#endif
#include "vg_ota_json.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int text(const cJSON *v,char *out,size_t n)
{
  if(!cJSON_IsString(v) || !v->valuestring[0] || strlen(v->valuestring)>=n) return -EINVAL;
  for(const unsigned char *s=(const unsigned char *)v->valuestring;*s;s++) if(*s<32 || *s==127) return -EINVAL;
  strcpy(out,v->valuestring); return 0;
}
static void clear_values(cJSON *node)
{
  if(!node) return;
  if(node->valuestring) { volatile char *p=node->valuestring; size_t n=strlen(node->valuestring); while(n--) *p++=0; }
  for(cJSON *child=node->child;child;child=child->next) clear_values(child);
}
int vg_ota_config_parse(const void *json,size_t length,struct vg_ota_config *out)
{
  static const char *names[]={"broker","port","username","password","allowed_hosts"};
  const cJSON *fields[5]={0}; cJSON *root=NULL,*item;
  char *copy=NULL,url[128]; struct vg_ota_url endpoint;
  int ret=-EINVAL; unsigned seen=0;
  if(!json || !out || !length || length>VG_OTA_CONFIG_JSON_MAX || vg_ota_json_guard(json,length)) return ret;
  memset(out,0,sizeof(*out));
  copy=malloc(length+1); if(!copy) return -ENOMEM;
  memcpy(copy,json,length); copy[length]=0;
  root=cJSON_ParseWithOpts(copy,NULL,1); if(!cJSON_IsObject(root)) goto done;
  cJSON_ArrayForEach(item,root) {
    unsigned i; for(i=0;i<5;i++) if(item->string && !strcmp(item->string,names[i])) break;
    if(i==5 || (seen&(1u<<i))) goto done;
    seen|=1u<<i; fields[i]=item;
  }
  if(seen!=31 || text(fields[0],out->broker,sizeof(out->broker)) || text(fields[1],out->port,sizeof(out->port)) ||
      text(fields[2],out->username,sizeof(out->username)) || text(fields[3],out->password,sizeof(out->password)) ||
      !cJSON_IsArray(fields[4]) || cJSON_GetArraySize(fields[4])<1 || cJSON_GetArraySize(fields[4])>2) goto done;
  snprintf(url,sizeof(url),"https://%s:%s/",out->broker,out->port);
  if(vg_ota_url_parse(url,&endpoint) || strcmp(endpoint.host,out->broker) || strcmp(endpoint.port,out->port)) goto done;
  out->allowed_count=(unsigned)cJSON_GetArraySize(fields[4]);
  for(unsigned i=0;i<out->allowed_count;i++) {
    if(text(cJSON_GetArrayItem(fields[4],(int)i),out->allowed_hosts[i],sizeof(out->allowed_hosts[i]))) goto done;
    snprintf(url,sizeof(url),"https://%s/",out->allowed_hosts[i]);
    if(vg_ota_url_parse(url,&endpoint)) goto done;
  }
  ret=0;
done:
  clear_values(root); cJSON_Delete(root);
  if(copy) { memset(copy,0,length); free(copy); }
  if(ret) memset(out,0,sizeof(*out));
  return ret;
}
