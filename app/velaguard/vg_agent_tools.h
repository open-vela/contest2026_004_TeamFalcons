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

/* Fill the agent-facing view of the confirmed table: metadata joined with the
 * live snapshot.  Returns 0 when values are available, or a
 * vg_agent_query_rc reason (NO_TABLE, NO_SAMPLE).  Used by the voice path to
 * resolve a spoken point name before it asks the agent. */
struct vg_agent_point;
int vg_agent_tools_collect(struct vg_agent_point *out, int max, int *n_out);

/* Monotonic count of vg tool executions since boot (rate-limit rejections
 * excluded: no data was served).  The voice round snapshots it before
 * submitting a question and rejects a reply that quotes numbers without any
 * vg tool having run for it - a model answer with invented readings never
 * reaches the screen. */

uint32_t vg_agent_tools_exec_count(void);

#ifdef VG_AGENT_TOOLS_HOST_TEST
/* Test build only: clear the read-only call window between cases. */
void vg_agent_tools_reset_rate(void);
#endif

#endif /* CONFIG_EXAMPLES_AI_AGENT_VELA */

#endif /* __APP_VELAGUARD_VG_AGENT_TOOLS_H */
