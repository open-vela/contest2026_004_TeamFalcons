/****************************************************************************
 * app/velaguard/host_tests/test_ptt_cycles.c
 *
 * BVO-02 完成条件里的按放序列回归：30 次正常按放、10 次抖动、10 次长按，
 * 每种都要求事件数量精确匹配，且不出现重复提交或多出来的会话。
 *
 * 每个用例用固定步长的采样序列驱动，覆盖 NuttX 侧 10 ms tick 下的实际
 * 采样密度，而不是只用理想边沿。
 ****************************************************************************/

#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "vg_ptt.h"

/* 板端 tick 为 10 ms；驱动线程按 5 ms 采样，密度高于实际即可验证边界 */

#define SAMPLE_MS       5u
#define RELEASE_GAP_MS  300u
#define SHORT_HOLD_MS   1500u
#define LONG_HOLD_MS    (VG_PTT_MAX_HOLD_MS + 2000u)

static int g_fail;

static void expect(int cond, const char *msg, int index)
{
  if (!cond)
    {
      fprintf(stderr, "FAIL[%d]: %s\n", index, msg);
      g_fail++;
    }
}

struct counter
{
  struct vg_ptt ptt;
  uint32_t now;
  int begins;
  int submits;
  int timeouts;
  int releases;
};

static void reset(struct counter *c, bool level)
{
  memset(c, 0, sizeof(*c));
  c->now = 1000;
  vg_ptt_init(&c->ptt, level, c->now);
}

/* 逐样本推进 ms 毫秒，level 保持给定值 */

static void hold(struct counter *c, bool level, uint32_t ms)
{
  uint32_t elapsed = 0;

  while (elapsed < ms)
    {
      struct vg_ptt_event_record rec;

      c->now += SAMPLE_MS;
      elapsed += SAMPLE_MS;

      if (vg_ptt_update(&c->ptt, level, c->now, &rec) == VG_PTT_EV_BEGIN)
        {
          c->begins++;
        }
      else if (rec.type == VG_PTT_EV_SUBMIT)
        {
          c->submits++;
          if (rec.reason == VG_PTT_SUBMIT_TIMEOUT)
            {
              c->timeouts++;
            }
          else
            {
              c->releases++;
            }
        }
    }
}

/* 30 次正常按放：每次一个开始、一个松手提交 */

static void test_thirty_normal_cycles(void)
{
  struct counter c;
  int i;

  reset(&c, false);

  for (i = 0; i < 30; i++)
    {
      int before_begins = c.begins;
      int before_submits = c.submits;

      hold(&c, true, SHORT_HOLD_MS);
      expect(c.begins - before_begins == 1, "one begin per press", i);
      expect(c.submits - before_submits == 0, "no submit while held", i);

      hold(&c, false, RELEASE_GAP_MS);
      expect(c.submits - before_submits == 1, "one submit per release", i);
      expect(c.ptt.state == VG_PTT_IDLE, "idle after each cycle", i);
    }

  expect(c.begins == 30, "exactly 30 sessions", 99);
  expect(c.submits == 30, "exactly 30 submissions", 99);
  expect(c.timeouts == 0, "no timeout submissions", 99);
  expect(c.releases == 30, "all 30 came from release", 99);
}

/* 10 次抖动按放：抖动段不产生事件，整段只算一次 */

static void test_bounce_cycles(void)
{
  struct counter c;
  int i;

  reset(&c, false);

  for (i = 0; i < 10; i++)
    {
      int before_begins = c.begins;
      int before_submits = c.submits;

      /* 按下时的抖动：高 5ms、低 5ms 交替三次，短于 20ms 去抖窗口 */

      hold(&c, true, 5);
      hold(&c, false, 5);
      hold(&c, true, 5);
      hold(&c, false, 5);
      expect(c.begins == before_begins, "bounce produces no begin", i);

      /* 稳定按住 */

      hold(&c, true, SHORT_HOLD_MS);
      expect(c.begins - before_begins == 1, "settled press begins once", i);

      /* 松手时的抖动 */

      hold(&c, false, 5);
      hold(&c, true, 5);
      hold(&c, false, RELEASE_GAP_MS);

      expect(c.submits - before_submits == 1, "one submit after bounce", i);
      expect(c.ptt.state == VG_PTT_IDLE, "idle after bounced cycle", i);
    }

  expect(c.begins == 10, "10 sessions despite bouncing", 99);
  expect(c.submits == 10, "10 submissions despite bouncing", 99);
}

/* 10 次超过上限的长按：每次只提交一次，松手不再补提交 */

static void test_long_hold_cycles(void)
{
  struct counter c;
  int i;

  reset(&c, false);

  for (i = 0; i < 10; i++)
    {
      int before_begins = c.begins;
      int before_submits = c.submits;
      int before_timeouts = c.timeouts;
      int submits_at_release;

      hold(&c, true, LONG_HOLD_MS);

      expect(c.begins - before_begins == 1, "one begin per long hold", i);
      expect(c.submits - before_submits == 1, "one submit while held", i);
      expect(c.timeouts - before_timeouts == 1, "submit came from the cap", i);
      expect(c.ptt.state == VG_PTT_HELD, "waiting for release after cap", i);

      submits_at_release = c.submits;
      hold(&c, false, RELEASE_GAP_MS);

      expect(c.submits == submits_at_release,
             "release after the cap adds no submit", i);
      expect(c.ptt.state == VG_PTT_IDLE, "idle after release", i);
    }

  expect(c.begins == 10, "10 long-hold sessions", 99);
  expect(c.submits == 10, "10 long-hold submissions", 99);
  expect(c.timeouts == 10, "all 10 from the cap", 99);
  expect(c.releases == 0, "no release submissions in this set", 99);
}

/* 上电时按钮已按住：整个按住过程不产生会话 */

static void test_boot_held_then_normal(void)
{
  struct counter c;

  reset(&c, true);

  hold(&c, true, LONG_HOLD_MS);
  expect(c.begins == 0 && c.submits == 0, "boot-held press is ignored", 0);

  hold(&c, false, RELEASE_GAP_MS);
  expect(c.begins == 0 && c.submits == 0, "boot release is ignored", 0);

  hold(&c, true, SHORT_HOLD_MS);
  hold(&c, false, RELEASE_GAP_MS);

  expect(c.begins == 1 && c.submits == 1, "normal cycle after boot-held", 1);
}

int main(void)
{
  test_thirty_normal_cycles();
  test_bounce_cycles();
  test_long_hold_cycles();
  test_boot_held_then_normal();

  if (g_fail == 0)
    {
      printf("test_ptt_cycles: all checks passed\n");
      return 0;
    }

  fprintf(stderr, "test_ptt_cycles: %d check(s) failed\n", g_fail);
  return 1;
}
