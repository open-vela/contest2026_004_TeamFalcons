/* Rules for the alarm page's per-point AI advice.
 *
 * These decisions used to live inline in vg_advice.c, where nothing on the
 * host could reach them.  The field symptom they caused: advice that had
 * already been validated and cached stopped reaching the page as soon as any
 * later round failed, and the page read "AI advice unavailable, showing the
 * rule summary" for an alarm the board actually had advice for.
 */

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "../vg_advice_policy.h"

static int expect_true(int cond, const char *msg)
{
  if (!cond)
    {
      fprintf(stderr, "FAIL: %s\n", msg);
      return 1;
    }

  return 0;
}

static int expect_false(int cond, const char *msg)
{
  return expect_true(!cond, msg);
}

static int expect_int(int got, int want, const char *msg)
{
  if (got != want)
    {
      fprintf(stderr, "FAIL: %s (got %d, want %d)\n", msg, got, want);
      return 1;
    }

  return 0;
}

static vg_ai_advice_doc_t doc_with(const char *id, uint32_t epoch,
                                   const char *sum)
{
  vg_ai_advice_doc_t doc;

  memset(&doc, 0, sizeof(doc));
  doc.boot = 0x30bd1132u;
  doc.req = 7;
  doc.n = 1;
  snprintf(doc.e[0].id, sizeof(doc.e[0].id), "%s", id);
  doc.e[0].epoch = epoch;
  doc.e[0].sev = VG_AI_SEV_OFFLINE;
  snprintf(doc.e[0].sum, sizeof(doc.e[0].sum), "%s", sum);
  return doc;
}

/* The regression this module was written for: the round state must not be
 * part of the hit test. */
static int test_hit_ignores_round_state(void)
{
  int fails = 0;
  vg_ai_advice_doc_t doc = doc_with("temp", 1, "check the intake");
  vg_ai_advice_entry_t out;

  memset(&out, 0, sizeof(out));

  fails += expect_true(vg_advice_lookup(true, &doc, "temp", 1, &out),
                       "loaded document answers the current episode");
  fails += expect_true(strcmp(out.sum, "check the intake") == 0,
                       "entry is copied out whole");

  /* Nothing in this call knows whether the last round failed.  A rejected
   * refresh leaves the document loaded, and the page must still find it. */

  fails += expect_true(vg_advice_lookup(true, &doc, "temp", 1, &out),
                       "a failed refresh cannot hide a matching entry");
  return fails;
}

static int test_hit_requires_identity(void)
{
  int fails = 0;
  vg_ai_advice_doc_t doc = doc_with("temp", 1, "check the intake");
  vg_ai_advice_entry_t out;

  memset(&out, 0, sizeof(out));

  fails += expect_false(vg_advice_lookup(true, &doc, "temp", 2, &out),
                        "next episode of the same point misses");
  fails += expect_false(vg_advice_lookup(true, &doc, "humid", 1, &out),
                        "another point misses");
  fails += expect_false(vg_advice_lookup(false, &doc, "temp", 1, &out),
                        "nothing loaded misses even on a matching id");
  fails += expect_false(vg_advice_lookup(true, NULL, "temp", 1, &out),
                        "NULL document misses");
  fails += expect_false(vg_advice_lookup(true, &doc, NULL, 1, &out),
                        "NULL id misses");
  fails += expect_false(vg_advice_lookup(true, &doc, "temp", 1, NULL),
                        "NULL out misses");
  return fails;
}

/* Coverage is derived from the document and the alarm set, so a document
 * written for an earlier round still counts when the episodes match.  That is
 * what keeps a timed-out round's file usable one round later. */
static int test_coverage(void)
{
  int fails = 0;
  vg_ai_advice_doc_t doc;
  vg_advice_episode_t one[1];
  vg_advice_episode_t two[2];
  vg_advice_episode_t missing[2];

  memset(&doc, 0, sizeof(doc));
  doc.boot = 0x30bd1132u;
  doc.n = 2;
  snprintf(doc.e[0].id, sizeof(doc.e[0].id), "temp");
  doc.e[0].epoch = 1;
  snprintf(doc.e[1].id, sizeof(doc.e[1].id), "humid");
  doc.e[1].epoch = 1;

  one[0].id = "temp";
  one[0].epoch = 1;

  two[0].id = "temp";
  two[0].epoch = 1;
  two[1].id = "humid";
  two[1].epoch = 1;

  missing[0].id = "temp";
  missing[0].epoch = 1;
  missing[1].id = "flood";
  missing[1].epoch = 1;

  fails += expect_true(vg_advice_doc_covers_set(true, &doc, one, 1),
                       "a one-point set is covered by a wider document");
  fails += expect_true(vg_advice_doc_covers_set(true, &doc, two, 2),
                       "a full set is covered");
  fails += expect_false(vg_advice_doc_covers_set(true, &doc, missing, 2),
                        "one unanswered point breaks coverage");
  fails += expect_false(vg_advice_doc_covers_set(true, &doc, NULL, 0),
                        "an empty alarm set is not covered");
  fails += expect_false(vg_advice_doc_covers_set(false, &doc, one, 1),
                        "an unloaded document covers nothing");
  fails += expect_false(vg_advice_doc_covers_set(true, NULL, one, 1),
                        "a NULL document covers nothing");

  /* A later episode of a covered point is not covered. */

  one[0].epoch = 2;
  fails += expect_false(vg_advice_doc_covers_set(true, &doc, one, 1),
                        "the next episode of a covered point is not covered");
  return fails;
}

static int test_after_round(void)
{
  int fails = 0;

  fails += expect_int(vg_advice_after_round(true), VG_ADV_AFTER_ROUND_READY,
                      "a failed round keeps covering advice on screen");
  fails += expect_int(vg_advice_after_round(false), VG_ADV_AFTER_ROUND_ERROR,
                      "an uncovered set reports the fallback");
  return fails;
}

/* The refresh window has to survive its two timestamps arriving out of order.
 * The load that stamps last_ok_ms runs inside the same tick that took the
 * `now` reading, so an unsigned subtraction wraps and reports fresh advice as
 * long expired: the board asked for a new round on every tick, one LLM round
 * after another, while the page already had advice to show. */
static int test_refresh_window(void)
{
  int fails = 0;

  fails += expect_false(vg_advice_refresh_due(true, 1000000u, 1000000u,
                                              300000u),
                        "advice just loaded is fresh");
  fails += expect_false(vg_advice_refresh_due(true, 1000000u, 1000500u,
                                              300000u),
                        "a stamp later than now is fresh, not wrapped");
  fails += expect_false(vg_advice_refresh_due(true, 1000000u, 999000u,
                                              300000u),
                        "one second of age is fresh");
  fails += expect_true(vg_advice_refresh_due(true, 1400000u, 1000000u,
                                             300000u),
                       "past the window is due");
  fails += expect_true(vg_advice_refresh_due(true, 1300000u, 1000000u,
                                             300000u),
                       "exactly at the window is due");
  fails += expect_false(vg_advice_refresh_due(false, 1400000u, 1000000u,
                                              300000u),
                        "nothing loaded is never due");
  fails += expect_false(vg_advice_refresh_due(true, 1400000u, 0, 300000u),
                        "a zero stamp means never loaded");
  return fails;
}

static vg_advice_gate_t gate_for(bool covered)
{
  vg_advice_gate_t g;

  memset(&g, 0, sizeof(g));
  g.covered = covered;
  g.min_gap_ms = 20000u;
  return g;
}

static int test_ask_gate(void)
{
  int fails = 0;
  vg_advice_gate_t g;

  /* Covered and fresh: nothing to spend. */

  g = gate_for(true);
  g.asked_once = true;
  fails += expect_int(vg_advice_decide(&g), VG_ADV_SKIP_COVERED,
                      "covering advice is not re-asked");

  /* Covered but aged out: refresh it. */

  g.refresh_due = true;
  fails += expect_int(vg_advice_decide(&g), VG_ADV_ASK,
                      "aged advice is refreshed");

  /* Nothing covers the set, no pause armed: ask. */

  g = gate_for(false);
  fails += expect_int(vg_advice_decide(&g), VG_ADV_ASK,
                      "an alarm set with no advice is asked about");

  /* The set that already came back empty is not re-asked inside its pause. */

  g.asked_once = true;
  g.backoff_armed = true;
  g.backoff_same_set = true;
  g.since_last_ask_ms = 300000u;
  fails += expect_int(vg_advice_decide(&g), VG_ADV_SKIP_BACKOFF,
                      "the set that failed waits out its pause");

  /* A different set is a different question, so it does not wait out the
   * previous set's pause.  The minimum gap still applies to every ask, and
   * the timings here are the realistic ones: a round runs about 195 s. */

  g = gate_for(false);
  g.asked_once = true;
  g.backoff_armed = true;
  g.backoff_expired = false;
  g.backoff_same_set = false;
  g.since_last_ask_ms = 195000u;
  fails += expect_int(vg_advice_decide(&g), VG_ADV_ASK,
                      "a changed alarm set is asked without the pause");

  /* Pause expired for the same set: ask again. */

  g = gate_for(false);
  g.asked_once = true;
  g.backoff_armed = true;
  g.backoff_expired = true;
  g.backoff_same_set = true;
  g.since_last_ask_ms = 300000u;
  fails += expect_int(vg_advice_decide(&g), VG_ADV_ASK,
                      "the pause expires back into asking");

  /* The minimum gap bounds an alarm set that flaps, which would otherwise ask
   * on every change. */

  g = gate_for(false);
  g.asked_once = true;
  g.backoff_armed = true;
  g.backoff_same_set = false;
  g.since_last_ask_ms = 3000u;
  fails += expect_int(vg_advice_decide(&g), VG_ADV_SKIP_MIN_GAP,
                      "a changed set inside the minimum gap still waits");

  g = gate_for(false);
  g.asked_once = true;
  g.since_last_ask_ms = 5000u;
  fails += expect_int(vg_advice_decide(&g), VG_ADV_SKIP_MIN_GAP,
                      "two asks stay a minimum gap apart");

  g.since_last_ask_ms = 20000u;
  fails += expect_int(vg_advice_decide(&g), VG_ADV_ASK,
                      "the minimum gap releases the ask");

  fails += expect_int(vg_advice_decide(NULL), VG_ADV_SKIP_BACKOFF,
                      "NULL gate asks for nothing");

  return fails;
}

/* What the page is told must never contradict what it can show.  Coverage
 * outranks the round state: a refresh in flight, or the daily report holding
 * the shared channel, must not relabel advice the page is already showing as
 * "still generating".  The call sites in vg_advice.c drifted apart once and
 * produced a pending state over a fully covered alarm set. */
static int test_presence(void)
{
  int fails = 0;

  fails += expect_int(vg_advice_presence(true, true, true),
                      VG_ADV_PRESENCE_SHOW,
                      "covered during a round still shows advice");
  fails += expect_int(vg_advice_presence(true, false, true),
                      VG_ADV_PRESENCE_SHOW,
                      "covered with no round shows advice");
  fails += expect_int(vg_advice_presence(false, true, true),
                      VG_ADV_PRESENCE_WAITING,
                      "uncovered with a round in flight is waiting");
  fails += expect_int(vg_advice_presence(false, false, true),
                      VG_ADV_PRESENCE_NONE,
                      "uncovered with no round has nothing to show");

  /* No LLM credentials.  This is the 2026-09-17 board state, where the page
   * said "AI 建议不可用" and the reader went looking for a fault in the advice
   * feature while the credentials were simply missing from the eMMC. */

  fails += expect_int(vg_advice_presence(false, false, false),
                      VG_ADV_PRESENCE_NO_CRED,
                      "no credentials is its own state, not a plain miss");

  /* A round already running without credentials is doomed, so the page must
   * not sit on "generating" waiting for an answer that cannot arrive. */

  fails += expect_int(vg_advice_presence(false, true, false),
                      VG_ADV_PRESENCE_NO_CRED,
                      "no credentials outranks a round in flight");

  /* Advice already on hand still wins: a credential that disappeared must not
   * blank a document that is correct for the alarm on screen. */

  fails += expect_int(vg_advice_presence(true, false, false),
                      VG_ADV_PRESENCE_SHOW,
                      "cached advice survives missing credentials");
  fails += expect_int(vg_advice_presence(true, true, false),
                      VG_ADV_PRESENCE_SHOW,
                      "cached advice survives missing credentials mid-round");

  return fails;
}

static int test_queue_classification(void)
{
  int fails = 0;

  fails += expect_true(vg_advice_queue_is_transient(-EBUSY),
                       "-EBUSY is the other flow holding the channel");
  fails += expect_false(vg_advice_queue_is_transient(0),
                        "success is not a conflict");
  fails += expect_false(vg_advice_queue_is_transient(-EIO),
                        "a dead bus is not transient");
  fails += expect_false(vg_advice_queue_is_transient(-E2BIG),
                        "an oversized request is not transient");
  fails += expect_false(vg_advice_queue_is_transient(-ENODEV),
                        "a missing agent is not transient");
  return fails;
}

int main(void)
{
  int fails = 0;

  fails += test_hit_ignores_round_state();
  fails += test_hit_requires_identity();
  fails += test_coverage();
  fails += test_after_round();
  fails += test_refresh_window();
  fails += test_presence();
  fails += test_ask_gate();
  fails += test_queue_classification();

  if (fails)
    {
      fprintf(stderr, "%d fail(s)\n", fails);
      return 1;
    }

  printf("test_advice_policy: ok\n");
  return 0;
}
