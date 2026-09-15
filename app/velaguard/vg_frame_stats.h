/****************************************************************************
 * app/velaguard/vg_frame_stats.h
 *
 * Per-slave sliding-window Modbus / RS485 frame quality counters (MVP B).
 ****************************************************************************/

#ifndef __APP_VELAGUARD_VG_FRAME_STATS_H
#define __APP_VELAGUARD_VG_FRAME_STATS_H

#include <stdint.h>

#ifndef VG_FS_WINDOW
#  define VG_FS_WINDOW 64
#endif

#ifndef VG_FS_MAX_SLAVES
#  define VG_FS_MAX_SLAVES 8
#endif

enum vg_fs_result
{
  VG_FS_OK = 0,
  VG_FS_CRC,
  VG_FS_TIMEOUT,
  VG_FS_ECHO,
  VG_FS_OTHER
};

struct vg_fs_summary
{
  uint32_t total;
  uint32_t ok;
  uint32_t crc_err;
  uint32_t timeout;
  uint32_t echo;
  uint32_t other;
  uint32_t lat_min_ms;
  uint32_t lat_max_ms;
  uint32_t lat_avg_ms;
};

/* Since-boot cumulative counters (not windowed). Latency fields cover OK
 * frames only, same as the sliding window summary. */
struct vg_fs_boot_summary
{
  uint32_t total;
  uint32_t ok;
  uint32_t crc_err;
  uint32_t timeout;
  uint32_t echo;
  uint32_t other;
  uint32_t lat_min_ms;
  uint32_t lat_max_ms;
  uint32_t lat_sum_ms; /* for avg = sum/ok when ok>0 */
};

void vg_fs_init(void);

int vg_fs_record(uint8_t slave, enum vg_fs_result result, uint32_t latency_ms);

int vg_fs_summary(uint8_t slave, struct vg_fs_summary *out);

int vg_fs_boot_summary(uint8_t slave, struct vg_fs_boot_summary *out);

/* Iterate occupied slave buckets. *idx starts at 0; returns slave addr or 0. */
uint8_t vg_fs_slave_at(int idx);

int vg_fs_reset(uint8_t slave);

int vg_fs_inject(uint8_t slave, enum vg_fs_result result, uint32_t latency_ms);

const char *vg_fs_result_name(enum vg_fs_result result);

#endif /* __APP_VELAGUARD_VG_FRAME_STATS_H */
