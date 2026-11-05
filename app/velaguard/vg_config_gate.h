/****************************************************************************
 * app/velaguard/vg_config_gate.h
 *
 * Process-wide ownership gate for point-table configuration workflows.
 ****************************************************************************/

#ifndef __APP_VELAGUARD_VG_CONFIG_GATE_H
#define __APP_VELAGUARD_VG_CONFIG_GATE_H

#include <stdbool.h>
#include <stdint.h>

enum vg_config_gate_owner_e
{
  VG_CONFIG_GATE_NONE = 0,
  VG_CONFIG_GATE_NSH,
  VG_CONFIG_GATE_REMOTE,
  VG_CONFIG_GATE_SCAN,
  VG_CONFIG_GATE_OTA
};

int vg_config_gate_acquire(enum vg_config_gate_owner_e owner,
                           uint32_t *token, bool *created);
int vg_config_gate_release(enum vg_config_gate_owner_e owner,
                           uint32_t token);
int vg_config_gate_token(enum vg_config_gate_owner_e owner,
                         uint32_t *token);
bool vg_config_gate_owns(enum vg_config_gate_owner_e owner,
                         uint32_t token);
enum vg_config_gate_owner_e vg_config_gate_current(void);
bool vg_config_gate_is_idle(void);
const char *vg_config_gate_owner_name(enum vg_config_gate_owner_e owner);

#endif
