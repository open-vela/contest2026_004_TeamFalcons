/****************************************************************************
 * VelaGuard eMMC provision store (encrypted LLM credentials).
 ****************************************************************************/

#ifndef __VG_PROVISION_H
#define __VG_PROVISION_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define VG_PROVISION_UID_LEN     12
#define VG_PROVISION_HOST_MAX    96
#define VG_PROVISION_PATH_MAX    96
#define VG_PROVISION_PORT_MAX    8
#define VG_PROVISION_MODEL_MAX   32
#define VG_PROVISION_API_KEY_MAX 192

/* Largest plaintext the crypto layer will seal.  vg_provision_crypto_seal()
 * enforces the same bound; keeping it here lets the credential build report an
 * oversized blob itself instead of failing inside the seal.
 */

#define VG_PROVISION_PLAIN_MAX   512

#define VG_PROVISION_FILE        "/data/velaguard/provision/llm_secrets.v1"

struct vg_llm_credentials
{
  char host[VG_PROVISION_HOST_MAX];
  char path[VG_PROVISION_PATH_MAX];
  char port[VG_PROVISION_PORT_MAX];
  char model[VG_PROVISION_MODEL_MAX];
  char api_key[VG_PROVISION_API_KEY_MAX];
};

int vg_provision_read_uid(uint8_t uid[VG_PROVISION_UID_LEN]);

bool vg_provision_is_present(void);

int vg_provision_seal(const struct vg_llm_credentials *cred);

int vg_provision_load(struct vg_llm_credentials *out);

int vg_provision_apply_llm_config(void);

void vg_provision_boot_apply_if_needed(void);

/* 0 when credits written to `path` would survive a reboot, -ENODEV when the
 * mount is RAM-backed (tmpfs or the pseudo filesystem), other negative on a
 * statfs failure.  Callers use this to refuse a write that only looks
 * successful because /data fell back to RAM.
 */

int vg_provision_store_is_persistent(const char *path);

/* True when /data/agent/config/config.json carries a non-empty host and key,
 * i.e. the agent has a backend it can reach.  False means every AI request
 * will end without a network call, and callers should say so rather than
 * blaming the feature that consumes the answer.
 */

bool vg_llm_credentials_ready(void);

/* The same test applied to JSON already in hand, so it can be exercised
 * without a board: the real predicate reads a fixed /data path.
 */

bool vg_provision_creds_ready_in(const char *json);

int vg_provision_parse_endpoint(const char *endpoint,
                                char *host, size_t hostsz,
                                char *path, size_t pathsz,
                                char *port, size_t portsz);

int vg_provision_crypto_seal(const uint8_t uid[VG_PROVISION_UID_LEN],
                             const uint8_t *plain, size_t plain_len,
                             uint8_t *out, size_t outsz, size_t *out_len);

int vg_provision_crypto_open(const uint8_t uid[VG_PROVISION_UID_LEN],
                             const uint8_t *blob, size_t blob_len,
                             uint8_t *plain, size_t plainsz, size_t *plain_len);

#endif /* __VG_PROVISION_H */
