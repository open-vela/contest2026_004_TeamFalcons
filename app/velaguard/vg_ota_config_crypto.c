/* SPDX-License-Identifier: Apache-2.0 */
#ifdef __NuttX__
#include <nuttx/config.h>
#endif
#include "vg_ota_config.h"
#include "vg_ota_sha256.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#if defined(__NuttX__) || defined(VG_OTA_CRYPTO_MBEDTLS)
#include <mbedtls/gcm.h>
#include <mbedtls/platform_util.h>
static void derive(const uint8_t uid[12],uint8_t key[32])
{
  static const uint8_t domain[]="VelaGuard:OTA:credentials:v2";
  struct vg_ota_sha256 sha;
  vg_ota_sha256_init(&sha);
  vg_ota_sha256_update(&sha,domain,sizeof(domain));
  vg_ota_sha256_update(&sha,uid,12);
  vg_ota_sha256_final(&sha,key);
}
#endif
static void put32(uint8_t *p,uint32_t v)
{ p[0]=(uint8_t)v; p[1]=(uint8_t)(v>>8); p[2]=(uint8_t)(v>>16); p[3]=(uint8_t)(v>>24); }
static uint32_t get32(const uint8_t *p)
{ return p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24); }
int vg_ota_config_seal(const uint8_t uid[12],const uint8_t nonce[12],const void *json,size_t length,
                       uint8_t *blob,size_t capacity,size_t *written)
{
  struct vg_ota_config config;
  if(!uid || !nonce || !json || !blob || !written || length>VG_OTA_CONFIG_JSON_MAX || capacity<length+52 ||
      vg_ota_config_parse(json,length,&config)) return -EINVAL;
  memset(&config,0,sizeof(config));
#if defined(__NuttX__) || defined(VG_OTA_CRYPTO_MBEDTLS)
  mbedtls_gcm_context cipher;
  uint8_t key[32];
  memset(blob,0,36); memcpy(blob,"VGOC",4); blob[4]=2;
  memcpy(blob+8,uid,12); memcpy(blob+20,nonce,12); put32(blob+32,(uint32_t)length);
  derive(uid,key); mbedtls_gcm_init(&cipher);
  int ret=mbedtls_gcm_setkey(&cipher,MBEDTLS_CIPHER_ID_AES,key,256);
  if(!ret) ret=mbedtls_gcm_crypt_and_tag(&cipher,MBEDTLS_GCM_ENCRYPT,length,nonce,12,blob,36,
                                       json,blob+36,16,blob+36+length);
  mbedtls_gcm_free(&cipher); mbedtls_platform_zeroize(key,sizeof(key));
  if(ret) { memset(blob,0,capacity); return -EIO; }
  *written=length+52;
  return 0;
#else
  (void)put32;
  return -ENOSYS;
#endif
}
int vg_ota_config_open(const uint8_t uid[12],const uint8_t *blob,size_t length,struct vg_ota_config *out)
{
  if(!uid || !blob || !out) return -EINVAL;
  memset(out,0,sizeof(*out));
  if(length<52 || length>VG_OTA_CONFIG_BLOB_MAX || memcmp(blob,"VGOC",4) || blob[4]!=2 ||
     blob[5] || blob[6] || blob[7] || memcmp(blob+8,uid,12) || get32(blob+32)!=length-52) return -EBADMSG;
#if defined(__NuttX__) || defined(VG_OTA_CRYPTO_MBEDTLS)
  mbedtls_gcm_context cipher;
  uint8_t key[32],*plain=calloc(1,length-52+1);
  if(!plain) return -ENOMEM;
  derive(uid,key); mbedtls_gcm_init(&cipher);
  int ret=mbedtls_gcm_setkey(&cipher,MBEDTLS_CIPHER_ID_AES,key,256);
  if(!ret) ret=mbedtls_gcm_auth_decrypt(&cipher,length-52,blob+20,12,blob,36,
                                       blob+length-16,16,blob+36,plain);
  if(!ret) ret=vg_ota_config_parse(plain,length-52,out);
  mbedtls_gcm_free(&cipher);
  mbedtls_platform_zeroize(plain,length-52+1); free(plain);
  mbedtls_platform_zeroize(key,sizeof(key));
  if(ret) { memset(out,0,sizeof(*out)); return -EKEYREJECTED; }
  return 0;
#else
  return -ENOSYS;
#endif
}
