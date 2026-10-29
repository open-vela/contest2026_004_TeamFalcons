/****************************************************************************
 * app/velaguard/vg_ota_clock.h
 *
 * OTA 协议时间统一用 Unix UTC 秒。板端系统时钟保存的是北京时间墙钟
 * （UTC+8，供 UI 和日志直接显示），所以这里在协议边界换算回 UTC。
 * host_tests 没有板端时区偏移，直接使用系统时钟。
 ****************************************************************************/

#ifndef __APP_VELAGUARD_VG_OTA_CLOCK_H
#define __APP_VELAGUARD_VG_OTA_CLOCK_H

#include <time.h>

#ifdef __NuttX__
#include <nuttx/config.h>
#endif

#include "vg_sntp.h"

static inline time_t vg_ota_utc_now(void)
{
#if defined(__NuttX__) && defined(CONFIG_VG_TIME_SYNC)
  return (time_t)vg_time_wall_to_utc((int64_t)time(NULL));
#else
  return time(NULL);
#endif
}

#endif /* __APP_VELAGUARD_VG_OTA_CLOCK_H */
