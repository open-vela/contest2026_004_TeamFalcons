/****************************************************************************
 * app/velaguard/vg_voice.h
 *
 * Licensed under the Apache License, Version 2.0.
 * BVO-03/05 本地录放诊断接口。实际人声可辨性须由真机听音确认。
 ****************************************************************************/

#ifndef __VELAGUARD_VG_VOICE_H
#define __VELAGUARD_VG_VOICE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define VG_VOICE_MAX_MS         15000u
#define VG_VOICE_RATE_HZ        16000u
#define VG_VOICE_RING_BYTES     16384
#define VG_VOICE_PCM_MAX_BYTES  (VG_VOICE_RATE_HZ * 2u * 15500u / 1000u)
#define VG_VOICE_WAV_PATH       "/data/velaguard/voice/clip.wav"

enum vg_voice_state
{
  VG_VOICE_IDLE = 0,
  VG_VOICE_RECORDING,
  VG_VOICE_PLAYING,
  VG_VOICE_FAULT
};

enum vg_voice_result
{
  VG_VOICE_OK = 0,
  VG_VOICE_CANCELLED,
  VG_VOICE_TOO_SHORT,
  VG_VOICE_ERR_START,
  VG_VOICE_ERR_DMA,
  VG_VOICE_ERR_ALLOC,
  VG_VOICE_ERR_FILE,
  VG_VOICE_ERR_PLAY,
  VG_VOICE_ERR_BUSY
};

/* 只发布数字摘要。samples 与实际 PDM 字节数核对，人声内容不进入日志。
 * 耗时来自 CLOCK_MONOTONIC；当前系统分辨率为 10000 us，0 表示小于一 tick。
 */

struct vg_voice_stats
{
  enum vg_voice_state state;
  uint32_t generation;
  uint32_t queue_highwater;
  uint32_t copy_max_us;
  uint32_t filter_max_us;
  uint32_t rounds;
  uint32_t played;
  uint32_t cancelled;
  uint32_t too_short;
  uint32_t failures;
  uint32_t timeouts;
  uint32_t dma_dropped;
  uint32_t dma_overruns;
  uint32_t dma_clockerrors;
  uint32_t dma_errors;
  uint32_t last_ms;
  uint32_t last_bytes;
  uint32_t last_samples;
  enum vg_voice_result last_result;
  uint32_t peak_abs;
  uint32_t rms;
  uint32_t stage_ms;
  uint32_t round_ms;
  uint32_t temp_files_left;
  uint32_t worker_pid;
  uint32_t cap_tailbytes;
  uint32_t cap_halves;
  uint32_t cap_wraps;
  uint32_t cap_elapsed_ms;
  uint32_t cap_rate_bps;
  uint32_t cap_overwrites;
  uint32_t cap_drained;
  uint32_t zcr;
  uint32_t clip_samples;
};

/* 常驻任务独占音频设备。初始化分配固定 PCM 与队列，关停确认任务退出后
 * 才释放。失败关停保留缓冲，并拒绝新录音，以免仍活动的硬件访问已释放内存。
 */
int vg_voice_init(void);
int vg_voice_shutdown(void);

/* 请求排队且有代际编号；快速按放不会让结束请求覆盖开始请求。
 * record_for 用于独立录音前置测试，100..15000 ms，结束后从耳机口回放。
 */
int vg_voice_record_begin(void);
int vg_voice_record_begin_owned(uint32_t *generation);
enum vg_voice_result vg_voice_finish_owned(uint32_t generation,
                                            bool cancelled);
int vg_voice_record_for(uint32_t ms);
enum vg_voice_result vg_voice_record_end(bool cancelled);
enum vg_voice_result vg_voice_cancel(void);
enum vg_voice_result vg_voice_finish(bool cancelled);
bool vg_voice_wait_idle(uint32_t limit_ms);
void vg_voice_get_stats(struct vg_voice_stats *out);
enum vg_voice_state vg_voice_state(void);
const char *vg_voice_state_name(enum vg_voice_state state);
const char *vg_voice_result_name(enum vg_voice_result result);

#endif
