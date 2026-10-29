#include "vg_ota_config.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static const char config[]={"{\"broker\":\"mqtt.example\",\"port\":\"8883\",\"username\":\"per-device-user\",\"password\":\"synthetic-password-do-not-log\",\"allowed_hosts\":[\"updates.example:443\"]}"};
int main(int argc,char **argv)
{
  struct vg_ota_config out; uint8_t uid[12],other[12],nonce[12],blob[VG_OTA_CONFIG_BLOB_MAX],copy[VG_OTA_CONFIG_BLOB_MAX]; size_t n;
  memset(uid,0xab,sizeof(uid)); memset(other,0xac,sizeof(other));
  for(unsigned i=0;i<12;i++) nonce[i]=(uint8_t)i;
  assert(!vg_ota_config_parse(config,strlen(config),&out));
  assert(!strcmp(out.broker,"mqtt.example") && out.allowed_count==1);
  assert(!vg_ota_config_seal(uid,nonce,config,strlen(config),blob,sizeof(blob),&n));
  assert(!vg_ota_config_open(uid,blob,n,&out));
  assert(!strcmp(out.password,"synthetic-password-do-not-log"));
  assert(vg_ota_config_open(other,blob,n,&out)<0 && !out.password[0]);
  unsigned offsets[]={4,8,20,32,40,(unsigned)n-1};
  for(unsigned i=0;i<sizeof(offsets)/sizeof(offsets[0]);i++) {
    memcpy(copy,blob,n); copy[offsets[i]]^=1;
    assert(vg_ota_config_open(uid,copy,n,&out)<0 && !out.password[0]);
  }
  assert(vg_ota_config_open(uid,blob,n-1,&out)<0);
  nonce[0]^=1; assert(!vg_ota_config_seal(uid,nonce,config,strlen(config),copy,sizeof(copy),&n));
  assert(memcmp(blob,copy,n));
  if(argc==2) {
    FILE *f=fopen(argv[1],"rb"); assert(f); n=fread(blob,1,sizeof(blob),f); fclose(f);
    assert(!vg_ota_config_open(uid,blob,n,&out));
    assert(!strcmp(out.broker,"mqtt.example"));
  }
  memset(&out,0,sizeof(out));
  puts("test_ota_config: OK (device-bound AES-GCM, tamper, nonce and optional Python interoperability)");
  return 0;
}
