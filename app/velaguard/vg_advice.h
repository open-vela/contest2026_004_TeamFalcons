/****************************************************************************
 * app/velaguard/vg_advice.h
 *
 * Board side of the per-point AI alarm advice.
 ****************************************************************************/

#ifndef __APP_VELAGUARD_VG_ADVICE_H
#define __APP_VELAGUARD_VG_ADVICE_H

#ifdef __cplusplus
extern "C" {
#endif

/* Publish the current alarm set.  MUST be called from the UI thread, which
 * is the only owner of vg_model; the file worker never reads the model
 * directly, because every live acquisition update mutates it. */

void vg_advice_note_alarms(void);

/* Drive the advice round: ask when the alarm set changes, read and validate
 * the agent's file when the round ends.  Called from the HMI file worker so
 * that no UI thread ever touches the agent or the filesystem. */

void vg_advice_tick(void);

/* Read-only dump of what the alarm page is being handed, for the `vgagent
 * advice` NSH probe: the round state, the cached document's identity, and one
 * hit/miss line per active alarm.  This is the acceptance evidence for "the
 * page shows advice, not the rule summary"; it reads state and prints,
 * nothing else.  Safe from any thread. */

void vg_advice_probe_dump(void);

#ifdef __cplusplus
}
#endif

#endif /* __APP_VELAGUARD_VG_ADVICE_H */
