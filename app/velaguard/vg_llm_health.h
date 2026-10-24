/****************************************************************************
 * app/velaguard/vg_llm_health.h
 *
 * LLM link health monitor behind the status bar MiMo chip.
 *
 * A resident thread probes the configured LLM endpoint every
 * CONFIG_VG_LLM_HEALTH_INTERVAL_MS with agent_http_probe: a fixed invalid
 * test credential that never touches the provisioned API key.  A 401 from
 * the endpoint proves DNS, TCP, TLS and the HTTP service are all alive, so
 * the link is healthy even though the test credential is rejected.
 *
 * A finished ask/advice round is stronger evidence than any probe - real
 * credentials, real model, real reply - so a round that ends DONE marks the
 * link healthy immediately; a round that ends ERROR only triggers an
 * out-of-cycle probe and the probe verdict decides.
 *
 * The endpoint is read from the same agent config the LLM chain itself
 * uses (llm_host / llm_path / llm_port), so the probe measures the path
 * the product actually depends on.
 ****************************************************************************/

#ifndef __APP_VELAGUARD_VG_LLM_HEALTH_H
#define __APP_VELAGUARD_VG_LLM_HEALTH_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifdef CONFIG_VG_LLM_HEALTH

/* Idempotent.  Spawns the monitor thread; the first probe runs at once. */

void vg_llm_health_start(void);

/* A real LLM round finished with a reply.  Marks the link healthy at once;
 * the next periodic probe re-verifies as usual. */

void vg_llm_health_note_round_ok(void);

/* Ask for an out-of-cycle probe (a round just failed, say).  The verdict
 * still belongs to the probe, not to the failure that asked for it. */

void vg_llm_health_kick(void);

/* Outcome of the last finished probe.  Returns false while no probe has
 * finished this boot and *ok is left untouched; the caller falls back to
 * its own signal (the MQTT session, before the first probe lands). */

bool vg_llm_health_status(bool *ok);

#else

static inline void vg_llm_health_start(void) { }
static inline void vg_llm_health_note_round_ok(void) { }
static inline void vg_llm_health_kick(void) { }
static inline bool vg_llm_health_status(bool *ok)
{
  (void)ok;
  return false;
}

#endif /* CONFIG_VG_LLM_HEALTH */

#ifdef __cplusplus
}
#endif

#endif /* __APP_VELAGUARD_VG_LLM_HEALTH_H */
