#include "vg_ota_sha256.h"

/* SHA-256 标准常量；实现不分配内存，不依赖操作系统。 */
static const uint32_t k[64] =
{
  0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
  0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
  0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
  0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
  0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
  0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
  0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
  0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
};

static uint32_t rr(uint32_t x, uint32_t n)
{
  return (x >> n) | (x << (32u - n));
}

static void transform(struct vg_ota_sha256 *s)
{
  uint32_t w[64];
  uint32_t a=s->h[0], b=s->h[1], c=s->h[2], d=s->h[3];
  uint32_t e=s->h[4], f=s->h[5], g=s->h[6], h=s->h[7];
  uint32_t i, t1, t2;
  for (i=0; i<16; i++)
    {
      const uint8_t *p = s->block + i*4;
      w[i] = ((uint32_t)p[0]<<24) | ((uint32_t)p[1]<<16) |
             ((uint32_t)p[2]<<8) | p[3];
    }
  for (i=16; i<64; i++)
    {
      uint32_t x=w[i-15], y=w[i-2];
      w[i] = w[i-16] + (rr(x,7)^rr(x,18)^(x>>3)) + w[i-7] +
             (rr(y,17)^rr(y,19)^(y>>10));
    }
  for (i=0; i<64; i++)
    {
      t1=h+(rr(e,6)^rr(e,11)^rr(e,25))+((e&f)^(~e&g))+k[i]+w[i];
      t2=(rr(a,2)^rr(a,13)^rr(a,22))+((a&b)^(a&c)^(b&c));
      h=g; g=f; f=e; e=d+t1; d=c; c=b; b=a; a=t1+t2;
    }
  s->h[0]+=a; s->h[1]+=b; s->h[2]+=c; s->h[3]+=d;
  s->h[4]+=e; s->h[5]+=f; s->h[6]+=g; s->h[7]+=h;
}

void vg_ota_sha256_init(struct vg_ota_sha256 *s)
{
  static const uint32_t initial[8] =
  {0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,
   0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
  uint32_t i;
  for (i=0; i<8; i++) s->h[i]=initial[i];
  s->bytes=0; s->used=0;
}

void vg_ota_sha256_update(struct vg_ota_sha256 *s, const uint8_t *p,
                          uint32_t len)
{
  s->bytes += len;
  while (len-- > 0)
    {
      s->block[s->used++] = *p++;
      if (s->used == 64) { transform(s); s->used=0; }
    }
}

void vg_ota_sha256_final(struct vg_ota_sha256 *s, uint8_t digest[32])
{
  uint64_t bits=s->bytes*8u;
  uint32_t i;
  s->block[s->used++]=0x80;
  if (s->used>56)
    {
      while (s->used<64) s->block[s->used++]=0;
      transform(s); s->used=0;
    }
  while (s->used<56) s->block[s->used++]=0;
  for (i=0; i<8; i++) { s->block[63-i]=(uint8_t)bits; bits>>=8; }
  transform(s);
  for (i=0; i<32; i++) digest[i]=(uint8_t)(s->h[i/4]>>(24-8*(i%4)));
}
