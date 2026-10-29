#include "stm32_rng_core.h"
#include <assert.h>
#include <stdio.h>

struct fake {
  uint32_t regs[7];
  uint32_t now;
  uint32_t word;
  int repeat;
  int ready;
};
static uint32_t read_reg(void *ctx,enum stm32_rng_reg_e reg)
{
  struct fake *f=ctx;
  if(reg==RNG_REG_SR) return f->regs[reg] | (f->ready ? 1u : 0);
  if(reg==RNG_REG_DR) { if(!f->repeat) f->word+=0x1234567u; return f->word; }
  return f->regs[reg];
}
static void write_reg(void *ctx,enum stm32_rng_reg_e reg,uint32_t value)
{ ((struct fake *)ctx)->regs[reg]=value; }
static void update_reg(void *ctx,enum stm32_rng_reg_e reg,uint32_t clear,uint32_t set)
{ struct fake *f=ctx; f->regs[reg]=(f->regs[reg]&~clear)|set; }
static uint32_t now(void *ctx) { return ((struct fake *)ctx)->now; }
static void wait_reg(void *ctx) { ((struct fake *)ctx)->now++; }
static struct stm32_rng_io_s io_for(struct fake *f)
{ struct stm32_rng_io_s io={read_reg,write_reg,update_reg,now,wait_reg,f}; return io; }
static struct fake healthy(void)
{
  struct fake f={0}; f.regs[RNG_REG_RCC_CR]=1u<<13;
  f.regs[RNG_REG_RCC_D2CCIP2R]=0x5a5a5a5a; f.ready=1; return f;
}
int main(void)
{
  struct fake f=healthy(); struct stm32_rng_state_s state={0};
  struct stm32_rng_io_s io=io_for(&f); uint8_t out[13]; uint32_t before=f.regs[RNG_REG_RCC_D2CCIP2R];
  assert(stm32_rng_core_read(&io,&state,out,sizeof(out))==sizeof(out));
  assert(state.initialized && state.previous==0x1234567u*5);
  assert(f.regs[RNG_REG_CR]==4 && (f.regs[RNG_REG_RCC_CR]&(1u<<12)));
  assert((f.regs[RNG_REG_RCC_D2CCIP2R]&~(3u<<8))==(before&~(3u<<8)));
  assert((f.regs[RNG_REG_RCC_D2CCIP2R]&(3u<<8))==0);
  assert(stm32_rng_core_read(&io,&state,out,4)==4 && state.previous==0x1234567u*6);
  f.repeat=1;
  assert(stm32_rng_core_read(&io,&state,out,4)==-EIO);
  assert(f.regs[RNG_REG_CR]==0 && out[0]==0 && out[3]==0);
  f.repeat=0;
  assert(stm32_rng_core_read(&io,&state,out,4)==-EIO);
  for(unsigned fault=2;fault<=0x40;fault<<=1) if(fault==2 || fault==4 || fault==0x20 || fault==0x40) {
    f=healthy(); memset(&state,0,sizeof(state)); f.regs[RNG_REG_SR]=fault;
    assert(stm32_rng_core_read(&io,&state,out,sizeof(out))==-EIO);
    for(unsigned i=0;i<sizeof(out);i++) assert(out[i]==0);
  }
  f=healthy(); memset(&state,0,sizeof(state)); f.ready=0;
  assert(stm32_rng_core_read(&io,&state,out,4)==-ETIMEDOUT && f.now==100);
  f=healthy(); memset(&state,0,sizeof(state)); f.regs[RNG_REG_RCC_CR]=0;
  assert(stm32_rng_core_read(&io,&state,out,4)==-ETIMEDOUT && f.now==20);
  f=healthy(); memset(&state,0,sizeof(state)); f.now=UINT32_MAX-10;
  assert(stm32_rng_core_read(&io,&state,out,0)==0);
  assert(stm32_rng_core_read(&io,&state,out,4097)==-EINVAL);
  assert(stm32_rng_core_read(&io,&state,out,4)==4);
  puts("test_ota_entropy: OK (H750 register seam, timeout, health latch, no PRNG fallback)");
  return 0;
}
