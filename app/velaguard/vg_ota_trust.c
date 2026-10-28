#ifdef __NuttX__
#include <nuttx/config.h>
#endif
#include <errno.h>
#include <string.h>
#include "vg_ota_trust.h"
#include "vg_ota_sha256.h"
#if defined(__has_include)
# if __has_include("vg_ota_trust_build.h")
#  include "vg_ota_trust_build.h"
# endif
#endif
#ifndef VG_OTA_CA_PEM
#define VG_OTA_CA_PEM ""
#define VG_OTA_SIGNING_PUBLIC_PEM ""
#define VG_OTA_SIGNING_KEY_ID ""
#endif
#if defined(__NuttX__) || defined(VG_OTA_CRYPTO_MBEDTLS)
#include <mbedtls/pk.h>
#include <mbedtls/ecdsa.h>
#endif

const char *vg_ota_trust_ca(void) { return VG_OTA_CA_PEM; }
const char *vg_ota_trust_key_id(void) { return VG_OTA_SIGNING_KEY_ID; }
int vg_ota_trust_configured(void)
{
  return VG_OTA_CA_PEM[0] && VG_OTA_SIGNING_PUBLIC_PEM[0] && VG_OTA_SIGNING_KEY_ID[0];
}

int vg_ota_trust_verify_with_key(const struct vg_ota_manifest *m,
                                const char *key_id, const char *pem)
{
  if (!m || !key_id || !pem || !pem[0] || !key_id[0]) return -ENOKEY;
  if (m->flags != 0 || strcmp(m->key_id, key_id)) return -EKEYREJECTED;
#if defined(__NuttX__) || defined(VG_OTA_CRYPTO_MBEDTLS)
  uint8_t wire[VG_OTA_MANIFEST_BYTES], digest[32];
  struct vg_ota_sha256 hash;
  mbedtls_pk_context key;
  mbedtls_mpi r, s;
  int ret;
  if (vg_ota_manifest_encode(m, wire) != 0) return -EINVAL;
  vg_ota_sha256_init(&hash);
  vg_ota_sha256_update(&hash, wire, VG_OTA_MANIFEST_SIGNED_BYTES);
  vg_ota_sha256_final(&hash, digest);
  mbedtls_pk_init(&key);
  mbedtls_mpi_init(&r);
  mbedtls_mpi_init(&s);
  ret = mbedtls_pk_parse_public_key(&key, (const uint8_t *)pem, strlen(pem)+1);
  if (ret == 0 && (!mbedtls_pk_can_do(&key, MBEDTLS_PK_ECDSA) ||
      mbedtls_pk_ec(key)->MBEDTLS_PRIVATE(grp).id != MBEDTLS_ECP_DP_SECP256R1))
    ret = -1;
  if (ret == 0) ret = mbedtls_mpi_read_binary(&r, m->signature, 32);
  if (ret == 0) ret = mbedtls_mpi_read_binary(&s, m->signature+32, 32);
  if (ret == 0) ret = mbedtls_ecdsa_verify(&mbedtls_pk_ec(key)->MBEDTLS_PRIVATE(grp),
       digest, sizeof(digest), &mbedtls_pk_ec(key)->MBEDTLS_PRIVATE(Q), &r, &s);
  mbedtls_mpi_free(&s);
  mbedtls_mpi_free(&r);
  mbedtls_pk_free(&key);
  memset(digest, 0, sizeof(digest));
  return ret == 0 ? 0 : -EKEYREJECTED;
#else
  return -ENOKEY;
#endif
}

int vg_ota_trust_verify_manifest(const struct vg_ota_manifest *m)
{
  return vg_ota_trust_verify_with_key(m, VG_OTA_SIGNING_KEY_ID, VG_OTA_SIGNING_PUBLIC_PEM);
}
