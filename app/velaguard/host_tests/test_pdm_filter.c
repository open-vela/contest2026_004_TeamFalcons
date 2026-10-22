/****************************************************************************
 * app/velaguard/host_tests/test_pdm_filter.c
 *
 * PDM 抽取的主机回归。
 *
 * 覆盖四件事：
 *   1. 与生成系数用的参考模型逐样本一致（黄金数据由同一脚本产生，但
 *      参考模型是独立的 Python 实现，不是把 C 输出回灌）。
 *   2. 跨块连续：把同一段输入任意切分，输出序列必须完全相同。这是连续
 *      录音的前提，逐块重置滤波器状态会在块边界产生不连续。
 *   3. 增益与钳位：满调制到满幅，不越界。
 *   4. 直流阻塞：固定偏置（模拟 0.48 的静音密度）衰到接近零。
 ****************************************************************************/

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vg_pdm_filter.h"
#include "pdm_golden.h"

static int g_failures;

#define CHECK(cond, fmt, ...)                                    \
  do                                                             \
    {                                                            \
      if (!(cond))                                               \
        {                                                        \
          printf("FAIL %s:%d: " fmt "\n", __FILE__, __LINE__,     \
                 ##__VA_ARGS__);                                 \
          g_failures++;                                          \
        }                                                        \
    }                                                            \
  while (0)

static void test_golden(void)
{
  struct vg_pdm_filter f;
  int16_t out[PDM_GOLDEN_IN_LEN / 8];
  size_t n;
  size_t i;

  vg_pdm_filter_init(&f);
  n = vg_pdm_filter_run(&f, g_pdm_golden_in, PDM_GOLDEN_IN_LEN,
                        out, sizeof(out) / sizeof(out[0]));

  CHECK(n == PDM_GOLDEN_OUT_LEN, "golden length %zu, expected %u",
        n, (unsigned)PDM_GOLDEN_OUT_LEN);

  for (i = 0; i < n && i < PDM_GOLDEN_OUT_LEN; i++)
    {
      CHECK(out[i] == g_pdm_golden_out[i],
            "golden sample %zu = %d, expected %d",
            i, (int)out[i], (int)g_pdm_golden_out[i]);
    }

  printf("golden: %zu samples compared\n", i);
}

static void test_chunked(void)
{
  /* Same input, split at several awkward boundaries including 1 byte, a
   * prime, and a chunk that does not end on a PCM sample boundary. */

  static const size_t cuts[] = { 1, 3, 7, 64, 65, 127, 128, 129, 511 };
  struct vg_pdm_filter whole;
  int16_t ref[PDM_GOLDEN_IN_LEN / 8];
  size_t reflen;
  size_t c;

  vg_pdm_filter_init(&whole);
  reflen = vg_pdm_filter_run(&whole, g_pdm_golden_in, PDM_GOLDEN_IN_LEN,
                             ref, sizeof(ref) / sizeof(ref[0]));
  CHECK(reflen == PDM_GOLDEN_OUT_LEN, "reference length %zu", reflen);

  for (c = 0; c < sizeof(cuts) / sizeof(cuts[0]); c++)
    {
      struct vg_pdm_filter f;
      int16_t out[PDM_GOLDEN_IN_LEN / 8];
      size_t off = 0;
      size_t produced = 0;

      vg_pdm_filter_init(&f);

      while (off < PDM_GOLDEN_IN_LEN)
        {
          size_t len = cuts[c];
          size_t got;

          if (off + len > PDM_GOLDEN_IN_LEN)
            {
              len = PDM_GOLDEN_IN_LEN - off;
            }

          got = vg_pdm_filter_run(&f, g_pdm_golden_in + off, len,
                                  out + produced,
                                  PDM_GOLDEN_OUT_LEN - produced);
          produced += got;
          off += len;
        }

      CHECK(produced == reflen, "chunk %zu produced %zu, expected %zu",
            cuts[c], produced, reflen);

      if (produced == reflen)
        {
          CHECK(memcmp(out, ref, reflen * sizeof(ref[0])) == 0,
                "chunk %zu differs from unchunked output", cuts[c]);
        }
    }

  /* One-byte-at-a-time is the tightest case: it exercises a call that
   * produces no output and a call that crosses both decimation phases. */

  printf("chunked: %zu boundaries compared\n",
         sizeof(cuts) / sizeof(cuts[0]));
}

static void test_gain(void)
{
  static uint8_t ones[4096];
  static uint8_t zeros[4096];
  struct vg_pdm_filter f;
  int16_t out[512];
  size_t n;
  size_t i;
  int peak_hi = 0;
  int peak_lo = 0;

  memset(ones, 0xff, sizeof(ones));
  memset(zeros, 0x00, sizeof(zeros));

  /* Without the DC blocker the rails would sit at full scale.  With it they
   * decay, so look near the start of the settled region instead of the end.
   * What this asserts is the clamp and the polarity, not a steady value. */

  vg_pdm_filter_init(&f);
  n = vg_pdm_filter_run(&f, ones, sizeof(ones), out, sizeof(out) / sizeof(out[0]));
  CHECK(n == 512, "ones produced %zu", n);
  for (i = 0; i < n; i++)
    {
      if (out[i] > peak_hi)
        {
          peak_hi = out[i];
        }
    }

  CHECK(peak_hi > 8000 && peak_hi <= 32767,
        "all-ones peak %d outside the expected band", peak_hi);

  vg_pdm_filter_init(&f);
  n = vg_pdm_filter_run(&f, zeros, sizeof(zeros), out,
                        sizeof(out) / sizeof(out[0]));
  for (i = 0; i < n; i++)
    {
      if (out[i] < peak_lo)
        {
          peak_lo = out[i];
        }
    }

  CHECK(peak_lo < -8000 && peak_lo >= -32768,
        "all-zeros peak %d outside the expected band", peak_lo);

  /* The offset has to keep shrinking, which is what the DC blocker is for. */

  CHECK(abs(out[n - 1]) < abs(out[n / 4]),
        "DC blocker did not decay: %d then %d",
        (int)out[n / 4], (int)out[n - 1]);

  printf("gain: all-ones peak %d, all-zeros peak %d, tail %d\n",
         peak_hi, peak_lo, (int)out[n - 1]);
}

static void test_dc_offset(void)
{
  /* 0.48 ones density, close to what this board's microphone shows in
   * silence.  After the transient the output must be a small DC level, and
   * the DC blocker must bring it near zero. */

  struct vg_pdm_filter f;
  int16_t out[512];
  uint8_t quiet[4096];
  size_t i;
  size_t n;
  long sum_tail = 0;
  size_t tail_count = 0;

  /* Repeating 0x3d3d... is not a realistic bitstream (it is periodic), so
   * use a deterministic pseudo-random stream with the wanted density. */

  {
    uint32_t state = 0x9e3779b9u;
    size_t ones = 0;
    size_t want = (size_t)(0.48 * sizeof(quiet) * 8);

    memset(quiet, 0, sizeof(quiet));
    while (ones < want)
      {
        state = 1103515245u * state + 12345u;
        {
          size_t bit = (state >> 16) % (sizeof(quiet) * 8);
          size_t byte = bit / 8;
          uint8_t mask = (uint8_t)(1u << (bit % 8));

          if ((quiet[byte] & mask) == 0)
            {
              quiet[byte] |= mask;
              ones++;
            }
        }
      }
  }

  vg_pdm_filter_init(&f);
  n = vg_pdm_filter_run(&f, quiet, sizeof(quiet), out,
                        sizeof(out) / sizeof(out[0]));
  CHECK(n == 512, "quiet produced %zu", n);

  for (i = n / 2; i < n; i++)
    {
      sum_tail += out[i];
      tail_count++;
    }

  {
    long mean = sum_tail / (long)tail_count;

    printf("dc: 0.48 density, mean of the tail %ld\n", mean);
    CHECK(labs(mean) < 400,
          "0.48 density left a mean of %ld after DC blocking", mean);
  }
}

static void test_short_input(void)
{
  struct vg_pdm_filter f;
  int16_t out[4];
  uint8_t small[8];
  size_t n;

  /* 8 bytes are exactly one output sample (64 bits), so that call does
   * produce one; 7 bytes are short and must produce none.  Neither may
   * corrupt the state. */

  memset(small, 0x5a, sizeof(small));

  vg_pdm_filter_init(&f);
  n = vg_pdm_filter_run(&f, small, 7, out, 4);
  CHECK(n == 0, "7 bytes produced %zu samples", n);

  n = vg_pdm_filter_run(&f, small, 1, out, 4);
  CHECK(n == 1, "the missing 8th byte produced %zu samples, expected 1", n);

  /* Capacity too small must reject the whole call, not silently drop the
   * samples it did compute: dropping would advance the state and leave a
   * gap in the recording that still adds up to the right length. */

  {
    static uint8_t block[128];
    struct vg_pdm_filter g;
    struct vg_pdm_filter h;
    int16_t big[16];
    size_t full;
    size_t partial;

    memset(block, 0xa3, sizeof(block));

    vg_pdm_filter_init(&g);
    full = vg_pdm_filter_run(&g, block, sizeof(block), big, 16);
    CHECK(full == 16, "128 bytes produced %zu samples", full);

    vg_pdm_filter_init(&h);
    partial = vg_pdm_filter_run(&h, block, sizeof(block), big, 4);
    CHECK(partial == 0, "a short buffer was partially accepted: %zu", partial);
    CHECK(h.bit_phase == 0 && h.samples == 0,
          "a rejected call changed the filter state");

    partial = vg_pdm_filter_run(&h, block, sizeof(block), big, 16);
    CHECK(partial == full, "retry after rejection produced %zu", partial);
    CHECK(memcmp(&g, &h, sizeof(g)) == 0,
          "retry after rejection diverged from the accepted run");
  }

  n = vg_pdm_filter_run(&f, NULL, 0, out, 4);
  CHECK(n == 0, "NULL source produced %zu samples", n);

  n = vg_pdm_filter_run(&f, small, sizeof(small), NULL, 4);
  CHECK(n == 0, "NULL destination produced %zu samples", n);

  printf("short: refused empty calls\n");
}

/* 非零抽取相位时，容量不足必须保持全部滤波状态。旧实现忽略了
 * sample_phase，会消耗输入并丢失一个输出样本。
 */
static void test_phase_capacity(void)
{
  struct vg_pdm_filter f;
  struct vg_pdm_filter before;
  int16_t out[8];
  size_t n;
  vg_pdm_filter_init(&f);
  n = vg_pdm_filter_run(&f, g_pdm_golden_in, 7, out, 8);
  CHECK(n == 0, "phase prelude emitted %zu", n);
  before = f;
  n = vg_pdm_filter_run(&f, g_pdm_golden_in + 7, 9, out, 1);
  CHECK(n == 0, "short phase buffer accepted %zu", n);
  CHECK(memcmp(&f, &before, sizeof(f)) == 0,
        "rejected phase buffer advanced state");
  n = vg_pdm_filter_run(&f, g_pdm_golden_in + 7, 9, out, 2);
  CHECK(n == 2, "phase retry emitted %zu", n);
  CHECK(out[0] == g_pdm_golden_out[0] && out[1] == g_pdm_golden_out[1],
        "phase retry differs from reference");
}

int main(void)
{
  test_phase_capacity();
  test_golden();
  test_chunked();
  test_gain();
  test_dc_offset();
  test_short_input();

  if (g_failures != 0)
    {
      printf("test_pdm_filter: %d failure(s)\n", g_failures);
      return EXIT_FAILURE;
    }

  printf("test_pdm_filter: OK\n");
  return EXIT_SUCCESS;
}
