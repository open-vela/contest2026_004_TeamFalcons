#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>
#include <velaclaw/client.h>
#include "../vg_agent_round.h"

static uint32_t now = 100;
static void (*callback)(int, const char *, void *);
static void *cookie;
int __wrap_clock_gettime(clockid_t clock, struct timespec *ts)
{ (void)clock; ts->tv_sec = now / 1000; ts->tv_nsec = (now % 1000) * 1000000; return 0; }
velaclaw_client_t *velaclaw_client_open(const char *name)
{ (void)name; return (velaclaw_client_t *)(uintptr_t)1; }
int velaclaw_ask_async(velaclaw_client_t *c, const velaclaw_ask_req_t *req,
                      void (*cb)(int, const char *, void *), void *ck)
{ (void)c; (void)req; callback = cb; cookie = ck; return 0; }

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
  puts("test_agent_round: OK (timeout, late callback, result ownership, recovery)");
  return 0;
}
