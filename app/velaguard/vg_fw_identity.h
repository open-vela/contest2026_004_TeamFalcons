/****************************************************************************
 * app/velaguard/vg_fw_identity.h
 *
 * Single firmware version and boot-slot identity shared by status, screen
 * and the OTA protocol.
 ****************************************************************************/

#ifndef __VG_FW_IDENTITY_H
#define __VG_FW_IDENTITY_H

#include <stddef.h>
#include <stdint.h>

#define VG_FW_SLOT_A_BASE 0x90000000u
#define VG_FW_SLOT_B_BASE 0x90800000u
#define VG_FW_SLOT_SIZE   (8u * 1024u * 1024u)

const char *vg_fw_version(void);
const char *vg_fw_build_id(void);

char vg_fw_slot(void);
const char *vg_fw_slot_name(void);
uint32_t vg_fw_link_base(void);

int vg_fw_version_valid(const char *version);
char vg_fw_slot_from_marker(uintptr_t marker, uintptr_t link_base);
int vg_fw_identity_format(char *out, size_t n);

#endif
