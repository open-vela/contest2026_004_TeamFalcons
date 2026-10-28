/****************************************************************************
 * app/velaguard/host_tests/test_ptt.c
 *
 * USER1 按住说话状态机的主机时序测试（BVO-02）。
 *
 * 用显式毫秒驱动 vg_ptt_update()，不依赖真实时钟，因此以下工况可重复：
 * 按下/松手、单击抖动、长按到上限、上限后继续按住、上电即按住、时钟回绕。
 ****************************************************************************/

#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "vg_ptt.h"

static int g_fail;

static void expect(int cond, const char *msg)
{
  if (!cond)
    {
      fprintf(stderr, "FAIL: %s\n", msg);
      g_fail++;
    }
}

/* 驱动器：把一次 update 的返回值记下来，便于按顺序断言事件流 */

struct driver
{
  struct vg_ptt ptt;
  uint32_t now;
  int begins;
  int submits;
  int timeouts;
  int releases;
  uint32_t last_duration;
  uint32_t last_press;
};

static void driver_init(struct driver *d, bool level, uint32_t now)
{
  memset(d, 0, sizeof(*d));
  d->now = now;
  vg_ptt_init(&d->ptt, level, now);
}

static enum vg_ptt_event feed(struct driver *d, bool level, uint32_t advance)
{
  struct vg_ptt_event_record rec;
  enum vg_ptt_event ev;

  d->now += advance;
  ev = vg_ptt_update(&d->ptt, level, d->now, &rec);

  if (rec.type != ev)
    {
      fprintf(stderr, "FAIL: return %d disagrees with record %d\n",
              (int)ev, (int)rec.type);
      g_fail++;
    }

  if (ev == VG_PTT_EV_BEGIN)
    {
      d->begins++;
      d->last_press = rec.press_ms;
    }
  else if (ev == VG_PTT_EV_SUBMIT)
    {
      d->submits++;
      d->last_duration = rec.duration_ms;
      d->last_press = rec.press_ms;
      if (rec.reason == VG_PTT_SUBMIT_TIMEOUT)
        {
          d->timeouts++;
        }
      else
        {
          d->releases++;
        }
    }

  return ev;
}

/* 一次干净的单按：去抖后开始，松手提交，时长等于按住部分 */

static void test_single_press(void)
{
  struct driver d;

  driver_init(&d, false, 1000);

  expect(feed(&d, true, 5) == VG_PTT_EV_NONE, "press edge not accepted yet");
  expect(d.ptt.state == VG_PTT_IDLE, "still idle inside debounce window");
  expect(feed(&d, true, 20) == VG_PTT_EV_BEGIN, "press accepted after 20ms");
  expect(d.ptt.state == VG_PTT_RECORDING, "recording after begin");
  expect(d.begins == 1, "one begin");

  expect(feed(&d, false, 1000) == VG_PTT_EV_NONE, "release inside debounce");
  expect(feed(&d, false, 20) == VG_PTT_EV_SUBMIT, "release submitted");
  expect(d.submits == 1 && d.releases == 1 && d.timeouts == 0,
         "release submission counted once");
  expect(d.ptt.state == VG_PTT_IDLE, "idle after release submit");

  /* 按下边沿在 1005，松手边沿在 2025：时长按边沿算，不含确认延迟 */

  expect(d.last_duration == 1020, "duration uses edges not confirmations");
  expect(d.last_press == 1005, "press time reported from the edge");
}

/* 上电即按住：不产生事件，先松手才接受下一次按下 */

static void test_boot_held(void)
{
  struct driver d;

  driver_init(&d, true, 0);

  expect(feed(&d, true, 500) == VG_PTT_EV_NONE, "boot-held level is silent");
  expect(d.ptt.state == VG_PTT_IDLE, "boot-held stays idle");

  expect(feed(&d, false, 5) == VG_PTT_EV_NONE, "boot-held release is silent");
  expect(feed(&d, false, 20) == VG_PTT_EV_NONE, "release confirmed, no submit");
  expect(d.submits == 0, "no submit from a boot-held press");

  expect(feed(&d, true, 100) == VG_PTT_EV_NONE, "new press edge");
  expect(feed(&d, true, 20) == VG_PTT_EV_BEGIN, "new press accepted");
}

/* 单击抖动：多个短跳变只算一次按下，不产生额外会话 */

static void test_bounce(void)
{
  struct driver d;

  driver_init(&d, false, 0);

  expect(feed(&d, true, 3) == VG_PTT_EV_NONE, "bounce high 1");
  expect(feed(&d, false, 3) == VG_PTT_EV_NONE, "bounce low 1");
  expect(feed(&d, true, 4) == VG_PTT_EV_NONE, "bounce high 2");
  expect(feed(&d, false, 2) == VG_PTT_EV_NONE, "bounce low 2");
  expect(d.begins == 0, "no begin from bouncing");

  /* 最后一次高电平边沿之后还要保持整个去抖窗口才算确认 */

  expect(feed(&d, true, 30) == VG_PTT_EV_NONE, "press edge still settling");
  expect(feed(&d, true, 20) == VG_PTT_EV_BEGIN, "settled press accepted");
  expect(d.begins == 1, "exactly one begin for one press");

  expect(feed(&d, false, 30) == VG_PTT_EV_NONE, "release still settling");
  expect(feed(&d, false, 20) == VG_PTT_EV_SUBMIT, "settled release");
  expect(d.submits == 1, "exactly one submit");
}

/* 长按到 15 秒：自动提交一次，继续按住不重复触发 */

static void test_hold_to_cap(void)
{
  struct driver d;

  driver_init(&d, false, 0);

  feed(&d, true, 10);
  expect(feed(&d, true, 20) == VG_PTT_EV_BEGIN, "begin at 30ms");

  /* 上限从按下边沿（10 ms）起算，因此到 15010 ms 才到期 */

  expect(feed(&d, true, 14970) == VG_PTT_EV_NONE, "still recording at 15.00s");
  expect(feed(&d, true, 40) == VG_PTT_EV_SUBMIT, "cap reached at 15.01s");
  expect(d.timeouts == 1 && d.releases == 0, "timeout submission");
  expect(d.last_duration == VG_PTT_MAX_HOLD_MS, "cap duration reported");
  expect(d.ptt.state == VG_PTT_HELD, "held waiting for release");

  /* 继续按住：不因超时再提交，也不开始新会话 */

  expect(feed(&d, true, 500) == VG_PTT_EV_NONE, "still held, quiet");
  expect(feed(&d, true, 60000) == VG_PTT_EV_NONE, "long hold stays quiet");
  expect(d.submits == 1 && d.begins == 1, "no repeats while held");

  /* 松手：回到空闲，但不产生第二次提交 */

  expect(feed(&d, false, 10) == VG_PTT_EV_NONE, "release inside debounce");
  expect(feed(&d, false, 20) == VG_PTT_EV_NONE, "release after cap is silent");
  expect(d.submits == 1, "release after cap does not resubmit");
  expect(d.ptt.state == VG_PTT_IDLE, "idle after release");

  /* 释放后才接受下一次按下 */

  expect(feed(&d, true, 100) == VG_PTT_EV_NONE, "next press edge");
  expect(feed(&d, true, 20) == VG_PTT_EV_BEGIN, "next press accepted");
}

/* 松手边沿正好落在上限时刻：按松手提交一次，不补超时提交 */

static void test_release_at_cap(void)
{
  struct driver d;

  driver_init(&d, false, 0);

  feed(&d, true, 10);
  expect(feed(&d, true, 20) == VG_PTT_EV_BEGIN, "begin at 30ms");

  /* 松手边沿落在按下边沿 + 15000，也就是上限到期的同一时刻。确认窗口
   * 结束时已过上限，但用户已经松手，仍应按松手提交。 */

  expect(feed(&d, false, 14980) == VG_PTT_EV_NONE, "release edge at cap");
  expect(feed(&d, false, 20) == VG_PTT_EV_SUBMIT, "single submit");
  expect(d.submits == 1, "no double submit at the cap");
  expect(d.releases == 1 && d.timeouts == 0,
         "release wins over timeout when both land together");
  expect(d.last_duration == VG_PTT_MAX_HOLD_MS, "duration is the full hold");
}

/* 两次按放之间必须有完整松手；按住不放不产生第二个会话 */

static void test_no_repeat_while_held(void)
{
  struct driver d;

  driver_init(&d, false, 0);

  feed(&d, true, 10);
  feed(&d, true, 20);
  feed(&d, false, 10);
  expect(feed(&d, false, 20) == VG_PTT_EV_SUBMIT, "first press submitted");
  expect(d.begins == 1 && d.submits == 1, "one session so far");

  /* 松手后再次按下：新的会话 */

  feed(&d, true, 500);
  expect(feed(&d, true, 20) == VG_PTT_EV_BEGIN, "second session begins");
  feed(&d, false, 800);
  expect(feed(&d, false, 20) == VG_PTT_EV_SUBMIT, "second session submits");
  expect(d.begins == 2 && d.submits == 2, "two independent sessions");
}

/* 时钟回绕：单调毫秒越过 2^32 仍然正确计时 */

static void test_wrap(void)
{
  struct driver d;
  const uint32_t near_wrap = 0xFFFFFF00u;

  driver_init(&d, false, near_wrap - 1000);

  feed(&d, true, 10);
  expect(feed(&d, true, 20) == VG_PTT_EV_BEGIN, "begin before wrap");

  /* 跨过回绕点继续按住 15 秒 */

  expect(feed(&d, true, 14000) == VG_PTT_EV_NONE, "recording across wrap");
  expect(feed(&d, true, 1000) == VG_PTT_EV_SUBMIT, "cap reached across wrap");
  expect(d.timeouts == 1, "timeout detected across wrap");
}

/* 等待超时：没有待办时返回 -1，有待办时给出剩余时间且不为负 */

static void test_next_timeout(void)
{
  struct vg_ptt ptt;

  vg_ptt_init(&ptt, false, 0);
  expect(vg_ptt_next_timeout_ms(&ptt, 0) == -1, "idle waits forever");

  /* 按下边沿已记录、尚未确认：应等待去抖窗口剩余部分 */

  vg_ptt_update(&ptt, true, 5, NULL);
  expect(vg_ptt_next_timeout_ms(&ptt, 15) == 10,
         "pending debounce reports the remaining window");

  /* 确认后再查：录音中应等待上限，且从按下边沿（5 ms）起算 */

  vg_ptt_update(&ptt, true, 30, NULL);
  expect(ptt.state == VG_PTT_RECORDING, "recording for the timeout query");
  expect(vg_ptt_next_timeout_ms(&ptt, 1000) ==
         (int)(VG_PTT_MAX_HOLD_MS - 995), "cap wait is the remaining hold");

  /* 已过上限：返回 0 而不是负数 */

  expect(vg_ptt_next_timeout_ms(&ptt, 5 + VG_PTT_MAX_HOLD_MS + 500) == 0,
         "overdue cap reports zero, not a negative wait");
}

/* 名称映射：诊断输出不得出现空指针 */

static void test_names(void)
{
  expect(strcmp(vg_ptt_state_name(VG_PTT_IDLE), "idle") == 0, "idle name");
  expect(strcmp(vg_ptt_state_name(VG_PTT_RECORDING), "recording") == 0,
         "recording name");
  expect(strcmp(vg_ptt_state_name(VG_PTT_HELD), "held") == 0, "held name");
  expect(vg_ptt_state_name((enum vg_ptt_state)99) != NULL,
         "unknown state still has a name");
  expect(strcmp(vg_ptt_reason_name(VG_PTT_SUBMIT_RELEASE), "release") == 0,
         "release name");
  expect(strcmp(vg_ptt_reason_name(VG_PTT_SUBMIT_TIMEOUT), "timeout") == 0,
         "timeout name");
}

/* 空指针容忍：调用方拿不到记录时也不能崩 */

static void test_null_args(void)
{
  struct vg_ptt ptt;

  vg_ptt_init(NULL, false, 0);
  expect(vg_ptt_update(NULL, true, 0, NULL) == VG_PTT_EV_NONE,
         "null state is a no-op");
  expect(vg_ptt_next_timeout_ms(NULL, 0) == -1, "null state has no deadline");

  /* 未初始化即 update：按 init 处理，不产生事件 */

  memset(&ptt, 0, sizeof(ptt));
  expect(vg_ptt_update(&ptt, true, 0, NULL) == VG_PTT_EV_NONE,
         "first update initializes without an event");
  expect(ptt.started && ptt.state == VG_PTT_IDLE, "implicit init is idle");
}

int main(void)
{
  test_single_press();
  test_boot_held();
  test_bounce();
  test_hold_to_cap();
  test_release_at_cap();
  test_no_repeat_while_held();
  test_wrap();
  test_next_timeout();
  test_names();
  test_null_args();

  if (g_fail == 0)
    {
      printf("test_ptt: all checks passed\n");
      return 0;
    }

  fprintf(stderr, "test_ptt: %d check(s) failed\n", g_fail);
  return 1;
}
