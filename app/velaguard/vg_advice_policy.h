/****************************************************************************
 * app/velaguard/vg_advice_policy.h
 *
 * Decisions for the board-side per-point alarm advice, kept apart from the
 * threads, the filesystem and the model so app/velaguard/host_tests can
 * exercise them.
 *
 * The rule this module exists to hold: whether the page may show AI text
 * depends on whether a validated entry matches the alarm episode on screen.
 * It never depends on how the last round went, so a refresh that fails
 * cannot blank advice that is still correct for the alarm the reader is
 * looking at.
 *
 * Deliberately free of LVGL, cJSON and NuttX headers, same as
 * vg_ai_contract.h.
 ****************************************************************************/

#ifndef __APP_VELAGUARD_VG_ADVICE_POLICY_H
#define __APP_VELAGUARD_VG_ADVICE_POLICY_H

#include <stdbool.h>
#include <stdint.h>

#include "vg_ai_contract.h"

/* One point of an alarm episode, as the page identifies it. */

typedef struct
{
  const char *id;
  uint32_t    epoch;
} vg_advice_episode_t;

/****************************************************************************
 * Hit rule
 *
 * Single source of truth for "does the page have advice for this alarm".
 * Note what is absent from the signature: the state of the last round.  A
 * round that timed out or was rejected leaves the loaded document in place,
 * and that document is still the right answer for the alarm it was written
 * about.
 ****************************************************************************/

bool vg_advice_lookup(bool loaded, const vg_ai_advice_doc_t *doc,
                      const char *id, uint32_t epoch,
                      vg_ai_advice_entry_t *out);

/****************************************************************************
 * Coverage
 *
 * True when the loaded document has an entry for every episode in the alarm
 * set on screen.  Derived from the document and the alarm set themselves
 * rather than from remembered signatures: that is what lets a document
 * written for an earlier round still count, as long as the episodes it names
 * are the ones active now.
 ****************************************************************************/

bool vg_advice_doc_covers_set(bool loaded, const vg_ai_advice_doc_t *doc,
                              const vg_advice_episode_t *set, int n);

/****************************************************************************
 * What the page is told after a round ended without a document the parser
 * accepted, or with one that does not answer the current alarm set.
 ****************************************************************************/

typedef enum
{
  /* Advice on hand answers the current alarm set: show it.  The round
   * changed nothing the reader can see. */

  VG_ADV_AFTER_ROUND_READY = 0,

  /* Nothing behind this alarm set, so the page says advice is unavailable
   * and falls back to the rule summary. */

  VG_ADV_AFTER_ROUND_ERROR
} vg_advice_after_round_t;

vg_advice_after_round_t vg_advice_after_round(bool covered);

/****************************************************************************
 * What the page has, at any moment.
 *
 * The invariant this pins down: coverage outranks the round's state.  Advice
 * that answers the alarm set on screen is what the page shows, so a round in
 * flight -- a refresh of our own, or the daily report holding the shared
 * channel -- must not relabel it as still generating.  Only when there is
 * nothing behind the alarm set does the state say anything, and then it
 * distinguishes "on its way" from "nothing to show".
 *
 * Kept here rather than spelled out at each call site in vg_advice.c: the
 * call sites drifted apart once and the page ended up reporting a state its
 * own cache contradicted.
 ****************************************************************************/

typedef enum
{
  VG_ADV_PRESENCE_SHOW = 0,  /* covered: the page shows AI text */
  VG_ADV_PRESENCE_WAITING,   /* nothing yet, and a round is in flight */
  VG_ADV_PRESENCE_NONE,      /* nothing, and nothing in flight */

  /* Nothing, and no round can help because the agent has no LLM credentials.
   * Kept separate from NONE on purpose: both leave the page with the rule
   * summary, but the reader's next move differs.  NONE means wait, this one
   * means the board has to be provisioned again, and saying "unavailable"
   * for both sent the 2026-09-17 investigation after the wrong feature. */

  VG_ADV_PRESENCE_NO_CRED
} vg_advice_presence_t;

vg_advice_presence_t vg_advice_presence(bool covered, bool round_in_flight,
                                        bool credentials_ready);

/****************************************************************************
 * Has the advice on hand aged past the refresh window?
 *
 * Takes both timestamps rather than reading a clock, so the wrap-around case
 * can be pinned: the load that stamps last_ok_ms runs inside the same tick
 * that took the `now` reading, so last_ok_ms is normally the later of the
 * two.  An unsigned subtraction there wraps to about 2^32 and reports fresh
 * advice as long expired, which asks for a new round on every tick.
 ****************************************************************************/

bool vg_advice_refresh_due(bool loaded, uint32_t now_ms, uint32_t last_ok_ms,
                           uint32_t window_ms);

/****************************************************************************
 * Whether to spend another LLM round
 ****************************************************************************/

typedef struct
{
  bool        covered;          /* advice on hand answers this alarm set */
  bool        refresh_due;      /* ... and has aged past the refresh time */
  bool        asked_once;       /* an ask has happened since boot */
  bool        backoff_armed;    /* a retry pause is set */
  bool        backoff_expired;  /* ... and its time is up */
  bool        backoff_same_set; /* ... and it belongs to this very set */
  uint32_t    since_last_ask_ms;
  uint32_t    min_gap_ms;
} vg_advice_gate_t;

typedef enum
{
  VG_ADV_ASK = 0,
  VG_ADV_SKIP_COVERED,    /* have it, and it is not stale yet */
  VG_ADV_SKIP_BACKOFF,    /* this very set already came back empty */
  VG_ADV_SKIP_MIN_GAP     /* asked too recently */
} vg_advice_decision_t;

vg_advice_decision_t vg_advice_decide(const vg_advice_gate_t *g);

/****************************************************************************
 * Queue outcome classification.
 *
 * -EBUSY is the other flow holding the single round channel for one tick.
 * It says nothing about this flow's advice, so it must not be reported as a
 * failure: the page would blink to "unavailable" every time the daily report
 * took the channel.
 ****************************************************************************/

bool vg_advice_queue_is_transient(int rc);

#endif /* __APP_VELAGUARD_VG_ADVICE_POLICY_H */
