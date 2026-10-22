/****************************************************************************
 * app/velaguard/vg_pdm_filter.h
 *
 * PDM 位流到 16 kHz PCM 的两级抽取。
 *
 * 输入是 SAI 按 8 位宽度交给内存的 PDM 字节流，每字节 8 个采样，LSB 在前。
 * 输出是 16 kHz、单声道、16 位有符号 PCM。
 *
 * 为什么分两级：从 1.024 Mbit/s 一步抽到 16 kHz，要同时保证 3.4 kHz 通带
 * 平坦和 8 kHz 以上的阻带抑制，过渡带只有采样率的 0.4%，需要上千阶。第一级
 * 用二阶滑动平均（boxcar）抽 8，代价是每个输入位几次加减，其零点正好落在
 * 会折叠回音频带的位置；第二级 129 阶 Kaiser 窗 sinc 再抽 8，只需压住
 * 8..64 kHz。系数由 scripts/gen_pdm_filter.py 生成，可复现。
 *
 * 滤波器状态跨块连续：滑动窗口、FIR 历史环形缓冲和直流阻塞器的状态都留在
 * 结构体里，所以任意切分输入缓冲区都不会改变输出序列。这是连续录音必须
 * 满足的条件——逐块重置状态会在块边界产生不连续。
 *
 * 直流阻塞必须有：这块板的麦克风静音时置位密度约 0.48，不是 0.5，不处理
 * 就是每个样本上的固定偏置。
 ****************************************************************************/

#ifndef __VELAGUARD_VG_PDM_FILTER_H
#define __VELAGUARD_VG_PDM_FILTER_H

#include <stddef.h>
#include <stdint.h>

#include "vg_pdm_filter_coeffs.h"

/* 输入每位对应输出多少分之一采样：64 位 -> 1 个 16 kHz 采样 */
#define VG_PDM_BITS_PER_SAMPLE  (VG_PDM_DECIM_STAGE1 * VG_PDM_DECIM_STAGE2)

struct vg_pdm_filter
{
  /* 第一级：两路级联的二阶 boxcar，各保留一个抽头延迟线 */

  int      d1[VG_PDM_DECIM_STAGE1];
  int      d2[VG_PDM_DECIM_STAGE1];
  int      s1;
  int      s2;
  unsigned pos1;
  unsigned bit_phase;        /* 距下一个第一级输出的位数 */

  /* 第二级：FIR 历史按采样计数索引，写完一个再判断是否抽头 */

  int16_t  hist[VG_PDM_TAPS];
  unsigned hist_pos;
  unsigned sample_phase;     /* 距下一个第二级输出的第一级样本数 */

  /* 直流阻塞（一阶高通） */

  int32_t  dc_x;
  int32_t  dc_y;

  /* 统计，只增不清，便于诊断与验收 */

  uint32_t samples;          /* 已产出的 PCM 样本数 */
  uint32_t saturations;      /* 输出被钳位到 int16 范围的次数 */
};

/**
  * @brief  清零全部滤波状态。
  * @param  f  滤波器状态；NULL 直接返回。
  * @retval None
  */
void vg_pdm_filter_init(struct vg_pdm_filter *f);

/**
  * @brief  把一段 PDM 字节转成 PCM。
  * @note   源与目标不得重叠。容量不足时整块拒绝，滤波状态保持不变。
  *         所需容量须包含上次保留的抽取相位。
  * @param  f       滤波器状态。
  * @param  src     PDM 字节，每字节 8 位，LSB 在前。
  * @param  nbytes  输入字节数。
  * @param  out     输出 PCM 缓冲。
  * @param  outcap  输出容量，单位样本。
  * @retval 实际写入的样本数。
  */
size_t vg_pdm_filter_run(struct vg_pdm_filter *f,
                         const uint8_t *src, size_t nbytes,
                         int16_t *out, size_t outcap);

#endif /* __VELAGUARD_VG_PDM_FILTER_H */
