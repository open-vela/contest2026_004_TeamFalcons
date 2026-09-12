/****************************************************************************
 * app/velaguard/vg_time_sync.h
 *
 * 板端时间同步：eMMC 墙钟持久化 + 开机恢复 + 联网 SNTP 校北京时间。
 ****************************************************************************/

#ifndef __VELAGUARD_VG_TIME_SYNC_H
#define __VELAGUARD_VG_TIME_SYNC_H

/**
  * @brief  启动时间同步后台线程（幂等不设防，仅由入口调用一次）。
  * @note   线程内：先从 CONFIG_VG_TIME_SYNC_PATH 恢复墙钟；每 3 分钟
  *         持久化一次；RJ45 有 IP 时按 60s 失败重试 / 30min 周期校时。
  *         系统时钟统一跑北京时间墙钟（UTC+8），HMI/日志直接可见。
  * @retval None
  */
void vg_time_sync_start(void);

#endif /* __VELAGUARD_VG_TIME_SYNC_H */
