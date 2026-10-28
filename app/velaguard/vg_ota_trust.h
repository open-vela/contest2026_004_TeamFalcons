#ifndef __VG_OTA_TRUST_H
#define __VG_OTA_TRUST_H
#include <stddef.h>
#include <stdint.h>
#include "vg_ota_manifest.h"

/* 公共信任材料由构建时的受控文件生成。未配置时拒绝生产 OTA。 */
const char *vg_ota_trust_ca(void);
const char *vg_ota_trust_key_id(void);
int vg_ota_trust_configured(void);
int vg_ota_trust_verify_manifest(const struct vg_ota_manifest *manifest);
/* 与生产路径使用同一验证器；调用者不能改变生产构建内的信任根。 */
int vg_ota_trust_verify_with_key(const struct vg_ota_manifest *manifest,
                                const char *key_id, const char *public_pem);
#endif
