/****************************************************************************
 * app/velaguard/vg_pdm_filter.c
 *
 * PDM 位流到 16 kHz PCM 的两级抽取。接口与设计依据见 vg_pdm_filter.h。
 *
 * 纯整数实现，不依赖 NuttX，因此主机测试与板端跑的是同一份代码。
 ****************************************************************************/

#include "vg_pdm_filter.h"

#include <stddef.h>
#include <string.h>

/* VG_PDM_ACC_SHIFT 与 VG_PDM_DC_R_Q15 来自生成头文件，与参考模型共用同一
 * 组常量，避免两处各写一个值。
 *
 * 移位口径：Q15 系数和为 32768，boxcar 两级直流增益为 64（8 × 8），所以
 * 全 1 位流得到 32768 × 64 >> 6 = 32768，即 int16 满幅。位流以 ±1 表示，
 * 密度 d 对应 2d - 1 的幅度，占空比全 1 与全 0 分别是满幅的正负极限。
 *
 * 直流阻塞是一阶高通 y[n] = R·y[n-1] + x[n] - x[n-1]。必须有这一步：这块板
 * 的麦克风静音时置位密度约 0.48 而不是 0.5，不处理就是每个样本上的固定偏置。
 * 转折选 20 Hz，既让偏置在几百毫秒内衰到可忽略，也不削弱 300 Hz 以上的
 * 语音基频。
 */

static inline int16_t clamp16(int32_t v)
{
  if (v > 32767)
    {
      return 32767;
    }

  if (v < -32768)
    {
      return -32768;
    }

  return (int16_t)v;
}

void vg_pdm_filter_init(struct vg_pdm_filter *f)
{
  if (f == NULL)
    {
      return;
    }

  memset(f, 0, sizeof(*f));
}

size_t vg_pdm_filter_run(struct vg_pdm_filter *f,
                         const uint8_t *src, size_t nbytes,
                         int16_t *out, size_t outcap)
{
  size_t produced = 0;
  size_t i;
  unsigned bit;

  if (f == NULL || src == NULL || out == NULL || outcap == 0)
    {
      return 0;
    }

  /* 拒绝而不是截断。调用者按上一块的位相位决定这一块会产生多少样本，
   * 容量不足时若只写前面一部分，剩下的样本会被算出来又丢掉，同时状态
   * 已经推进——那等于在录音里挖掉一段，而时长还对得上。宁可整块拒绝，
   * 让调用者重新分配。
   */

  {
    size_t nbits = nbytes * 8u;
    size_t needed = (f->sample_phase * VG_PDM_DECIM_STAGE1 +
                     f->bit_phase + nbits) / VG_PDM_BITS_PER_SAMPLE;

    if (needed > outcap)
      {
        return 0;
      }
  }

  for (i = 0; i < nbytes; i++)
    {
      uint8_t byte = src[i];

      /* 位流按 LSB 在前交给内存。展开成 ±1 后进级联 boxcar：
       *
       *   s1 += x - x[8]
       *   s2 += s1 - s1[8]
       *
       * 两级串联，每级都是 8 点滑动和，等效一个三角窗低通，零点落在
       * 128 kHz 的整数倍上，正是第一级抽取后会折叠回音频带的位置。
       */

      for (bit = 0; bit < 8; bit++)
        {
          int x = (byte & (1u << bit)) ? 1 : -1;
          unsigned p = f->pos1;

          f->s1 += x - f->d1[p];
          f->d1[p] = x;

          f->s2 += f->s1 - f->d2[p];
          f->d2[p] = f->s1;

          f->pos1 = (p + 1) % VG_PDM_DECIM_STAGE1;

          if (++f->bit_phase < VG_PDM_DECIM_STAGE1)
            {
              continue;
            }

          f->bit_phase = 0;

          /* 一个第一级样本进 FIR 环形历史 */

          f->hist[f->hist_pos] = (int16_t)f->s2;
          f->hist_pos = (f->hist_pos + 1) % VG_PDM_TAPS;

          if (++f->sample_phase < VG_PDM_DECIM_STAGE2)
            {
              continue;
            }

          f->sample_phase = 0;

          /* 第二级：对 129 个历史值做 Q15 乘累加。历史里存的是第一级
           * 的原始整数和，不是归一化值，所以这里只按 Q15 移位，幅度换算
           * 交给下面的右移。 */

          {
            int32_t acc = 0;
            unsigned hp = f->hist_pos;
            unsigned k;

            for (k = 0; k < VG_PDM_TAPS; k++)
              {
                /* 从最近的一个样本往回取，与系数的对称中心对齐 */
                hp = (hp == 0) ? (VG_PDM_TAPS - 1) : (hp - 1);
                acc += (int32_t)g_vg_pdm_coeffs[k] * f->hist[hp];
              }

            {
              int32_t y = acc >> VG_PDM_ACC_SHIFT;
              int32_t raw = y;
              int32_t filtered;

              if (raw > 32767 || raw < -32768)
                {
                  f->saturations++;
                }

              y = clamp16(y);

              /* 直流阻塞。状态跨块保留，块边界不重置。 */

              filtered = (int32_t)(((int64_t)VG_PDM_DC_R_Q15 * f->dc_y +
                                    16384) >> 15) +
                         y - f->dc_x;
              f->dc_x = y;
              f->dc_y = filtered;

              y = clamp16(filtered);

              if (produced < outcap)
                {
                  out[produced++] = (int16_t)y;
                }

              f->samples++;
            }
          }
        }
    }

  return produced;
}
