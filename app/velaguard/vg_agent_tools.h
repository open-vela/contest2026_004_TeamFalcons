/****************************************************************************
 * app/velaguard/vg_agent_tools.h
 *
 * Read-only data tools the on-board agent may call: the confirmed point
 * table's live values and the since-boot run report.
 *
 * These exist because the agent's only other data path is run_shell, and its
 * allowlist (packages/ai_agent tool_shell.c s_vg_readonly) blocks vgpoint by
 * design (AGENTS.md V5).  vgmodbus reads raw registers, so it cannot apply a
 * point's scale nor resolve a Chinese name to an id: for the demo table's
 * ups_load (scale 0.1) it reports the unscaled register value.
 ****************************************************************************/

#ifndef __APP_VELAGUARD_VG_AGENT_TOOLS_H
#define __APP_VELAGUARD_VG_AGENT_TOOLS_H

#ifdef CONFIG_EXAMPLES_AI_AGENT_VELA

/* Register the provider with the ai_agent tool registry.  Safe to call before
 * tool_registry_init() and idempotent; registering twice is a no-op. */

void vg_agent_tools_register(void);

#endif /* CONFIG_EXAMPLES_AI_AGENT_VELA */

#endif /* __APP_VELAGUARD_VG_AGENT_TOOLS_H */
