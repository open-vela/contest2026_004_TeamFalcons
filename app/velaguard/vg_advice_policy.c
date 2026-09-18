/****************************************************************************
 * app/velaguard/vg_advice_policy.c
 *
 * See vg_advice_policy.h.  Pure functions, no state of their own: every
 * input arrives as an argument so host_tests can pin each rule.
 ****************************************************************************/

#include <errno.h>
#include <string.h>

#include "vg_advice_policy.h"

bool vg_advice_lookup(bool loaded, const vg_ai_advice_doc_t *doc,
                      const char *id, uint32_t epoch,
                      vg_ai_advice_entry_t *out)
{
  const vg_ai_advice_entry_t *e;

  if (!loaded || doc == NULL || id == NULL || out == NULL)
    {
      return false;
    }

  e = vg_ai_advice_find(doc, id, epoch);
  if (e == NULL)
    {
      return false;
    }

  *out = *e;
  return true;
}

bool vg_advice_doc_covers_set(bool loaded, const vg_ai_advice_doc_t *doc,
                              const vg_advice_episode_t *set, int n)
{
  int i;

  /* An empty alarm set has no advice to show, so nothing covers it. */

  if (!loaded || doc == NULL || set == NULL || n <= 0)
    {
      return false;
    }

  for (i = 0; i < n; i++)
    {
      if (set[i].id == NULL ||
          vg_ai_advice_find(doc, set[i].id, set[i].epoch) == NULL)
        {
          return false;
        }
    }

  return true;
}

vg_advice_after_round_t vg_advice_after_round(bool covered)
{
  return covered ? VG_ADV_AFTER_ROUND_READY : VG_ADV_AFTER_ROUND_ERROR;
}

vg_advice_presence_t vg_advice_presence(bool covered, bool round_in_flight,
                                        bool credentials_ready)
{
  if (covered)
    {
      return VG_ADV_PRESENCE_SHOW;
    }

  /* Checked before the in-flight case: without credentials the round that is
   * running is already doomed, and the page would otherwise sit on "generating"
   * through a call that cannot succeed. */

  if (!credentials_ready)
    {
      return VG_ADV_PRESENCE_NO_CRED;
    }

  return round_in_flight ? VG_ADV_PRESENCE_WAITING : VG_ADV_PRESENCE_NONE;
}

bool vg_advice_refresh_due(bool loaded, uint32_t now_ms, uint32_t last_ok_ms,
                           uint32_t window_ms)
{
  if (!loaded || last_ok_ms == 0)
    {
      return false;
    }

  /* Signed: last_ok_ms can be ahead of now_ms, see the header. */

  return ((int32_t)(now_ms - last_ok_ms) >= (int32_t)window_ms);
}

vg_advice_decision_t vg_advice_decide(const vg_advice_gate_t *g)
{
  if (g == NULL)
    {
      return VG_ADV_SKIP_BACKOFF;
    }

  /* Advice for the alarm set on screen is only worth replacing once it has
   * aged out. */

  if (g->covered)
    {
      return g->refresh_due ? VG_ADV_ASK : VG_ADV_SKIP_COVERED;
    }

  /* Nothing covers this set, so one round per set per pause.  The pause is
   * keyed to the set that came back empty: a set that changed while it was
   * armed is a different question and does not wait it out, which is what
   * keeps a re-raised alarm from sitting through the previous set's pause. */

  if (g->backoff_armed && g->backoff_same_set && !g->backoff_expired)
    {
      return VG_ADV_SKIP_BACKOFF;
    }

  if (g->asked_once && g->since_last_ask_ms < g->min_gap_ms)
    {
      return VG_ADV_SKIP_MIN_GAP;
    }

  return VG_ADV_ASK;
}

bool vg_advice_queue_is_transient(int rc)
{
  return (rc == -EBUSY);
}
