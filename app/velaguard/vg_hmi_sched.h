/****************************************************************************
 * app/velaguard/vg_hmi_sched.h
 *
 * HMI UI-thread scheduling helpers for touch-acceptance (T1):
 * - pointer indev read period target (10 ms)
 * - bound lv_timer_handler() idle before usleep
 *
 * Pure arithmetic; safe for host unit tests without LVGL.
 ****************************************************************************/

#ifndef VG_HMI_SCHED_H
#define VG_HMI_SCHED_H

#include <stdint.h>

/* Matches LV_NO_TIMER_READY when LVGL is available. */
#ifndef VG_HMI_LV_NO_TIMER_READY
#  define VG_HMI_LV_NO_TIMER_READY 0xFFFFFFFFu
#endif

#define VG_HMI_INDEV_READ_MS  10u
#define VG_HMI_SLEEP_MIN_MS   1u
#define VG_HMI_SLEEP_MAX_MS   10u

/* Cap idle wait to 1..10 ms. Zero and "no timer ready" sleep the minimum
 * so the loop never busy-spins and never multiplies UINT32_MAX into usleep. */
static inline uint32_t vg_hmi_bound_idle_ms(uint32_t idle_ms)
{
  if(idle_ms == 0u || idle_ms == VG_HMI_LV_NO_TIMER_READY)
    {
      return VG_HMI_SLEEP_MIN_MS;
    }

  if(idle_ms > VG_HMI_SLEEP_MAX_MS)
    {
      return VG_HMI_SLEEP_MAX_MS;
    }

  return idle_ms;
}

#endif /* VG_HMI_SCHED_H */
