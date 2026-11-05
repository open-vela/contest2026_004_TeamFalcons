/****************************************************************************
 * app/velaguard/vg_config_contract.h
 *
 * Shared bounded types for candidate validation, service state and UI.
 ****************************************************************************/

#ifndef __APP_VELAGUARD_VG_CONFIG_CONTRACT_H
#define __APP_VELAGUARD_VG_CONFIG_CONTRACT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "vg_discover.h"

#define VG_CONFIG_HEX32_LEN 33
#define VG_CONFIG_HASH_LEN 65
#define VG_CONFIG_BOOTSTRAP_CAPACITY VG_DISCOVER_MAX_POINTS
#define VG_CONFIG_REQUEST_MAX 16384
#define VG_CONFIG_STATE_MAX 4096
#define VG_CONFIG_LEASE_MS 600000u
#define VG_CONFIG_ID_MAX 65
#define VG_CONFIG_REQUEST_CACHE 32
#define VG_CONFIG_USED_TASKS 32

#endif
