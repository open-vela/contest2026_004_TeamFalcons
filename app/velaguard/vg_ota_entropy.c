/* SPDX-License-Identifier: Apache-2.0 */
#include <nuttx/config.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/random.h>
#include <time.h>
#include <unistd.h>
#ifdef CONFIG_CRYPTO_MBEDTLS
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/entropy.h>
#endif
#include "vg_ota_entropy.h"

int vg_ota_entropy_probe(void)
{
#if !defined(CONFIG_CRYPTO_MBEDTLS) || !defined(CONFIG_STM32H7_RNG) || !defined(CONFIG_DEV_URANDOM_ARCH) || defined(CONFIG_DEV_URANDOM_XORSHIFT128)
  printf("vgota: entropy source=unverified refused\n");
  return -ENOTSUP;
#else
  struct entropy_probe {
    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context random;
  } *probe;
  struct timespec begin,end;
  uint8_t bytes[256], previous[256];
  int fd=-1,ret=-EIO;
  size_t total=0;
  clock_gettime(CLOCK_MONOTONIC,&begin);
  fd=open("/dev/random",O_RDONLY);
  if (fd<0) goto failed;
  for(unsigned i=0;i<16;i++) {
    if(read(fd,bytes,sizeof(bytes))!=sizeof(bytes)) goto failed;
    if(i && !memcmp(bytes,previous,sizeof(bytes))) goto failed;
    memcpy(previous,bytes,sizeof(bytes));
    total+=sizeof(bytes);
  }
  close(fd); fd=-1;
  for(unsigned i=0;i<16;i++) {
    if(getrandom(bytes,sizeof(bytes),0)!=sizeof(bytes)) goto failed;
    if(!memcmp(bytes,previous,sizeof(bytes))) goto failed;
    memcpy(previous,bytes,sizeof(bytes));
    total+=sizeof(bytes);
  }
  probe=calloc(1,sizeof(*probe));
  if(!probe) { ret=-ENOMEM; goto failed; }
  mbedtls_entropy_init(&probe->entropy);
  mbedtls_ctr_drbg_init(&probe->random);
  ret=mbedtls_ctr_drbg_seed(&probe->random,mbedtls_entropy_func,&probe->entropy,
                           (const uint8_t *)"VelaGuard-entropy-probe",22);
  if(!ret) ret=mbedtls_ctr_drbg_random(&probe->random,bytes,sizeof(bytes));
  mbedtls_ctr_drbg_free(&probe->random);
  mbedtls_entropy_free(&probe->entropy);
  memset(probe,0,sizeof(*probe)); free(probe);
  if(ret) goto failed;
  clock_gettime(CLOCK_MONOTONIC,&end);
  printf("vgota: entropy source=stm32h750-trng bytes=%u mbedtls_seed=pass elapsed_ms=%ld health=pass\n",
         (unsigned)total,(long)((end.tv_sec-begin.tv_sec)*1000+(end.tv_nsec-begin.tv_nsec)/1000000));
  memset(bytes,0,sizeof(bytes)); memset(previous,0,sizeof(previous));
  return 0;
failed:
  if(fd>=0) close(fd);
  memset(bytes,0,sizeof(bytes)); memset(previous,0,sizeof(previous));
  printf("vgota: entropy source=stm32h750-trng failed err=%d bytes=%u\n",ret,(unsigned)total);
  return ret;
#endif
}
