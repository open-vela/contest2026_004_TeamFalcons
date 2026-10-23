#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <velaclaw/client.h>
#include "../vg_agent_round.h"

static uint32_t now = 100;
static void (*callback)(int, const char *, void *);
static void *cookie;
static unsigned abandoned;
static uint32_t submitted_deadline;
static char submitted_text[64];
int __wrap_clock_gettime(clockid_t clock, struct timespec *ts)
{ (void)clock; ts->tv_sec = now / 1000; ts->tv_nsec = (now % 1000) * 1000000; return 0; }
velaclaw_client_t *velaclaw_client_open(const char *name)
{ (void)name; return (velaclaw_client_t *)(uintptr_t)1; }
int velaclaw_ask_async(velaclaw_client_t *c, const velaclaw_ask_req_t *req,
                      void (*cb)(int, const char *, void *), void *ck)
{ (void)c; snprintf(submitted_text, sizeof(submitted_text), "%s", req->text);
  callback = cb; cookie = ck; return 0; }
int velaclaw_ask_async_deadline(velaclaw_client_t *c,
    const velaclaw_ask_req_t *req, uint32_t deadline_ms,
    void (*cb)(int, const char *, void *), void *ck)
{
  assert(req->timeout_ms == (int32_t)(deadline_ms - now));
  submitted_deadline = deadline_ms;
  return velaclaw_ask_async(c, req, cb, ck);
}
int velaclaw_ask_async_background(velaclaw_client_t *c,
    const velaclaw_ask_req_t *req,uint32_t deadline_ms,
    void (*cb)(int,const char*,void*),void *ck)
{
  assert(deadline_ms == now + VG_AGENT_ROUND_TIMEOUT_MS);
  return velaclaw_ask_async(c,req,cb,ck);
}
void velaclaw_client_abandon(velaclaw_client_t *c) { (void)c; abandoned++; }

int main(void)
{
  assert(!vg_agent_round_queue("old"));
  vg_agent_round_tick();
  assert(vg_agent_round_state() == VG_AGENT_ROUND_RUNNING);
  void (*old_cb)(int, const char *, void *) = callback;
  void *old_cookie = cookie;
  now += VG_AGENT_ROUND_TIMEOUT_MS;
  vg_agent_round_tick();
  assert(vg_agent_round_state() == VG_AGENT_ROUND_ERROR);
  vg_agent_round_clear();
  assert(!vg_agent_round_queue("new"));
  vg_agent_round_tick();
  old_cb(0, "stale", old_cookie);
  vg_agent_round_tick();
  assert(vg_agent_round_state() == VG_AGENT_ROUND_RUNNING);
  callback(0, "current", cookie);
  vg_agent_round_tick();
  assert(vg_agent_round_state() == VG_AGENT_ROUND_DONE);
  assert(vg_agent_round_queue("must consume first") < 0);
  vg_agent_round_clear();
  assert(!vg_agent_round_queue("recovered"));
  vg_agent_round_abandon();
  assert(!vg_agent_round_queue_owned("background", VG_AGENT_ROUND_OWNER_DAILY));
  vg_agent_round_tick();
  unsigned before = abandoned;
  vg_agent_round_abandon_owned(VG_AGENT_ROUND_OWNER_VOICE);
  assert(abandoned == before);
  assert(vg_agent_round_state() == VG_AGENT_ROUND_RUNNING);
  assert(vg_agent_round_owner() == VG_AGENT_ROUND_OWNER_DAILY);
  vg_agent_round_abandon();

  uint32_t deadline = now + 1000;
  assert(!vg_agent_round_queue_voice("voice", deadline));
  now += 300;  /* Queue time consumes the same budget. */
  vg_agent_round_tick();
  assert(submitted_deadline == deadline);
  now = deadline;
  vg_agent_round_tick();
  assert(vg_agent_round_state() == VG_AGENT_ROUND_ERROR);
  vg_agent_round_abandon_owned(VG_AGENT_ROUND_OWNER_VOICE);
  assert(vg_agent_round_state() == VG_AGENT_ROUND_IDLE);
  assert(vg_agent_round_queue_voice("expired", now) < 0);
  vg_agent_round_voice_release();
  assert(!vg_agent_round_queue("next background"));
  vg_agent_round_abandon();
  assert(!vg_agent_round_queue_owned("deferred report", VG_AGENT_ROUND_OWNER_DAILY));
  assert(!vg_agent_round_queue_voice("priority voice", now + 1000));
  assert(vg_agent_round_owner() == VG_AGENT_ROUND_OWNER_VOICE);
  assert(vg_agent_round_queue_voice("second pending", now + 1000) < 0);
  vg_agent_round_tick();
  callback(0, "voice result", cookie);
  vg_agent_round_tick();
  vg_agent_round_clear();
  assert(vg_agent_round_state() == VG_AGENT_ROUND_IDLE);
  assert(vg_agent_round_queue_owned("must wait until speech ends",VG_AGENT_ROUND_OWNER_ADVICE)<0);
  vg_agent_round_voice_release();
  assert(vg_agent_round_state() == VG_AGENT_ROUND_QUEUED);
  assert(vg_agent_round_owner() == VG_AGENT_ROUND_OWNER_DAILY);
  vg_agent_round_tick();
  assert(strcmp(submitted_text, "deferred report") == 0);
  assert(vg_agent_round_owner() == VG_AGENT_ROUND_OWNER_DAILY);
  before=abandoned;
  old_cb=callback; old_cookie=cookie;
  assert(!vg_agent_round_queue_voice("interrupt running report",now+1000));
  assert(abandoned==before+1 && vg_agent_round_owner()==VG_AGENT_ROUND_OWNER_VOICE);
  vg_agent_round_tick();
  old_cb(0,"late interrupted report",old_cookie);
  vg_agent_round_tick();
  assert(vg_agent_round_state()==VG_AGENT_ROUND_RUNNING);
  callback(0,"voice",cookie); vg_agent_round_tick(); vg_agent_round_clear();
  assert(vg_agent_round_state()==VG_AGENT_ROUND_IDLE);
  vg_agent_round_voice_release(); vg_agent_round_tick();
  assert(vg_agent_round_owner()==VG_AGENT_ROUND_OWNER_DAILY);
  assert(!strcmp(submitted_text,"deferred report"));
  before = abandoned;
  old_cb = callback; old_cookie = cookie;
  assert(!vg_agent_round_voice_reserve());
  assert(abandoned == before + 1);
  assert(vg_agent_round_state() == VG_AGENT_ROUND_IDLE);
  old_cb(0,"stale before ASR",old_cookie);
  assert(vg_agent_round_queue_owned("blocked during ASR",VG_AGENT_ROUND_OWNER_ADVICE)<0);
  assert(!vg_agent_round_queue_voice("after ASR",now+1000));
  vg_agent_round_abandon_owned(VG_AGENT_ROUND_OWNER_VOICE);
  vg_agent_round_voice_release(); vg_agent_round_tick();
  assert(!strcmp(submitted_text,"deferred report"));
  puts("test_agent_round: OK (timeout, stale submit, owner isolation, shared budget, voice priority, background resume)");
  return 0;
}
