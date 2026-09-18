/****************************************************************************
 * host_tests/stubs/nuttx/config.h
 *
 * Minimal stand-in for the generated board config, so the device-side tool
 * layer (vg_agent_tools.c) can be compiled and exercised on the host.  Only
 * the switches that file actually consults appear here; the value of this
 * test is the tools JSON and the table/snapshot join, not the board config.
 ****************************************************************************/

#ifndef __HOST_TEST_STUB_NUTTX_CONFIG_H
#define __HOST_TEST_STUB_NUTTX_CONFIG_H

#define CONFIG_EXAMPLES_AI_AGENT_VELA 1

/* Mirrors the velaguard-lvgl preset: frame stats and the point table are
 * both present, so both tools are compiled and advertised. */
#define CONFIG_VG_FRAME_STATS 1
#define CONFIG_VG_BUS_DISCOVER 1
#define CONFIG_VG_HMI_DISCOVER 1

#endif /* __HOST_TEST_STUB_NUTTX_CONFIG_H */
