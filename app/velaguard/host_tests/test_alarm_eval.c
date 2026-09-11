#include <stdio.h>
#include <string.h>

#include "../vg_alarm_eval.h"

static int expect_true(int cond, const char *msg)
{
  if (!cond)
    {
      fprintf(stderr, "FAIL: %s\n", msg);
      return 1;
    }

  return 0;
}

int main(void)
{
  int fails = 0;
  struct vg_alarm_rule temp;
  struct vg_alarm_rule flood;
  struct vg_alarm_rule empty;
  float thr;
  enum vg_alarm_kind k;

  memset(&temp, 0, sizeof(temp));
  snprintf(temp.cmp, sizeof(temp.cmp), "ge");
  temp.has_warn = 1;
  temp.warn = 40.0f;
  temp.has_crit = 1;
  temp.crit = 55.0f;
  temp.fail_n = 3;

  memset(&flood, 0, sizeof(flood));
  snprintf(flood.cmp, sizeof(flood.cmp), "eq");
  flood.has_crit = 1;
  flood.crit = 1.0f;
  flood.fail_n = 3;

  memset(&empty, 0, sizeof(empty));
  empty.fail_n = 3;

  k = vg_alarm_eval(&temp, 1, 0, 39.9f, &thr);
  fails += expect_true(k == VG_ALARM_KIND_NONE, "temp below warn");

  k = vg_alarm_eval(&temp, 1, 0, 40.0f, &thr);
  fails += expect_true(k == VG_ALARM_KIND_WARN && thr > 39.0f, "temp warn");

  k = vg_alarm_eval(&temp, 1, 0, 55.0f, &thr);
  fails += expect_true(k == VG_ALARM_KIND_CRIT && thr > 54.0f, "temp crit");

  k = vg_alarm_eval(&flood, 1, 0, 0.0f, &thr);
  fails += expect_true(k == VG_ALARM_KIND_NONE, "flood 0");

  k = vg_alarm_eval(&flood, 1, 0, 1.0f, &thr);
  fails += expect_true(k == VG_ALARM_KIND_CRIT && thr > 0.9f, "flood 1");

  k = vg_alarm_eval(&empty, 1, 0, 99.0f, &thr);
  fails += expect_true(k == VG_ALARM_KIND_NONE, "empty cmp no analog");

  k = vg_alarm_eval(&temp, 0, 2, 0.0f, &thr);
  fails += expect_true(k == VG_ALARM_KIND_NONE, "fail streak 2");

  k = vg_alarm_eval(&temp, 0, 3, 0.0f, &thr);
  fails += expect_true(k == VG_ALARM_KIND_OFFLINE, "fail streak 3");

  fails += expect_true(vg_alarm_fail_n(&empty) == 3, "default fail_n");
  fails += expect_true(vg_alarm_kind_rank(VG_ALARM_KIND_CRIT) >
                       vg_alarm_kind_rank(VG_ALARM_KIND_OFFLINE),
                       "crit ranks above offline");

  if (fails)
    {
      fprintf(stderr, "%d fail(s)\n", fails);
      return 1;
    }

  printf("test_alarm_eval: ok\n");
  return 0;
}
