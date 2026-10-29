#include "vg_ota_link.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
struct fake { unsigned address_calls,reads,delays; int busy,error; uint16_t phy,first,second; };
static int address(void *ctx,uint16_t *phy)
{ struct fake *f=ctx; f->address_calls++; if(f->busy-->0) return -EAGAIN; *phy=f->phy; return f->error; }
static int read_phy(void *ctx,uint16_t phy,uint16_t reg,uint16_t *out)
{ struct fake *f=ctx; assert(phy==f->phy && reg==1); *out=(f->reads++%2) ? f->second : f->first; return 0; }
static void delay(void *ctx,unsigned ms) { struct fake *f=ctx; assert(ms==1); f->delays++; }
int main(void)
{
  struct fake f={.phy=1,.first=0,.second=4};
  struct vg_ota_link_ports p={&f,address,read_phy,delay};
  struct vg_ota_link_info info;
  assert(vg_ota_link_check(&p,&info)==1 && f.reads==2 && info.phy==1);
  f.first=4; f.second=0; assert(vg_ota_link_check(&p,&info)==0);
  f.second=0xffff; assert(vg_ota_link_check(&p,&info)==-EIO);
  f.second=4; f.first=0xffff; assert(vg_ota_link_check(&p,&info)==-EIO);
  f.first=0; f.phy=32; assert(vg_ota_link_check(&p,&info)==-EIO);
  f.phy=7; f.busy=2; f.delays=0; assert(vg_ota_link_check(&p,&info)==1 && f.delays==2);
  f.busy=9; f.address_calls=f.delays=0;
  assert(vg_ota_link_check(&p,&info)==-EAGAIN && f.address_calls==3 && f.delays==3);
  f.busy=0; f.error=-ETIMEDOUT; assert(vg_ota_link_check(&p,&info)==-ETIMEDOUT);
  assert(vg_ota_link_check(NULL,&info)==-EINVAL);
  assert(vg_ota_link_probe(NULL)==-EINVAL);
  assert(vg_ota_link_probe(&info)==-ENOTSUP && !vg_ota_link_ready());
  puts("test_ota_link: OK (double BMSR, stale link, bus errors, bounded busy retry)"); return 0;
}
