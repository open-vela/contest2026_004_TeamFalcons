/* SPDX-License-Identifier: Apache-2.0 */
#include "vg_ota_json.h"
#include <errno.h>
#include <stdint.h>
int vg_ota_json_guard(const void *json,size_t length)
{
  const uint8_t *p=json;
  int depth=0,string=0,escape=0;
  if(!p || !length) return -EINVAL;
  for(size_t i=0;i<length;i++) {
    uint8_t c=p[i];
    if(!c) return -EINVAL;
    if(string) {
      if(c<32) return -EINVAL;
      if(escape) {
        if(c=='u' && i+4<length && p[i+1]=='0' && p[i+2]=='0' && p[i+3]=='0' && p[i+4]=='0') return -EINVAL;
        escape=0;
      } else if(c=='\\') escape=1;
      else if(c=='"') string=0;
    } else {
      if(c=='"') string=1;
      else if(c=='{' || c=='[') { if(++depth>4) return -E2BIG; }
      else if(c=='}' || c==']') { if(--depth<0) return -EINVAL; }
    }
  }
  return depth || string || escape ? -EINVAL : 0;
}
