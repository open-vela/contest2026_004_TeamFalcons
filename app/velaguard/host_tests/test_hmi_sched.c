/* Host tests for vg_hmi_bound_idle_ms (task 09-13-hmi-touch-acceptance T1). */

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

#include "vg_hmi_sched.h"

static int s_fails;

static bool expect(bool cond, const char * what)
{
  printf("%s %s\n", cond ? "[PASS]" : "[FAIL]", what);
  if(!cond) {
      s_fails++;
    }
  return cond;
}

int main(void)
{
  expect(vg_hmi_bound_idle_ms(0) == VG_HMI_SLEEP_MIN_MS,
         "zero idle sleeps minimum");
  expect(vg_hmi_bound_idle_ms(VG_HMI_LV_NO_TIMER_READY) == VG_HMI_SLEEP_MIN_MS,
         "no-timer-ready sleeps minimum");
  expect(vg_hmi_bound_idle_ms(1) == 1, "1 ms stays 1");
  expect(vg_hmi_bound_idle_ms(5) == 5, "5 ms stays 5");
  expect(vg_hmi_bound_idle_ms(10) == 10, "10 ms stays 10");
  expect(vg_hmi_bound_idle_ms(11) == VG_HMI_SLEEP_MAX_MS,
         "11 ms clamps to max");
  expect(vg_hmi_bound_idle_ms(1000) == VG_HMI_SLEEP_MAX_MS,
         "large idle clamps to max");
  expect(VG_HMI_INDEV_READ_MS == 10, "indev read target is 10 ms");
  expect(VG_HMI_SLEEP_MAX_MS == VG_HMI_INDEV_READ_MS,
         "sleep max matches indev read period");

  if(s_fails) {
      printf("test_hmi_sched: %d FAIL\n", s_fails);
      return 1;
    }
  printf("test_hmi_sched: ALL PASS\n");
  return 0;
}
