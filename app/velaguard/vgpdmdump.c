/****************************************************************************
 * app/velaguard/vgpdmdump.c
 *
 * BVO-03b 板载麦克风原始 PDM 采集验证工具。
 *
 * 直接从 SAI 设备取一段定长的原始 PDM 位流，写到文件，并打印足以判断
 * 通路是否真的工作的统计量。这只用于 bring-up，不属于产品音频路径：
 * 不做抽取滤波，不接语音，不改变任何配置或点表。
 *
 * 为什么不用 /dev/i2schar0：i2schar 的 read() 把缓冲区交给驱动后立刻
 * 返回，并在自己的回调里释放缓冲区，数据从不回到调用者。要拿到位流，
 * 只能自己持有缓冲区并等驱动回调。
 *
 * 判据说明。DMA 中断递增或缓冲区非零，只能证明总线上的传输在动。要认定
 * 麦克风真的在工作，还需要同时看到：
 *   - 置位比特占比接近 0.5（PDM 位流在静音时占空比约一半）；
 *   - 字节取值不是单一常量（全 0x00 或 0xFF 说明数据线被拉死）；
 *   - 说话与静音两种条件下的统计量有可观察差异。
 * 因此本工具打印直方图和熵，而不只打印首字节。
 ****************************************************************************/

#include <nuttx/config.h>

#ifdef CONFIG_VG_PDM_DUMP

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <math.h>
#include <time.h>

#include <nuttx/audio/i2s.h>
#include <nuttx/cache.h>
#include <nuttx/semaphore.h>

#include <arch/board/board.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* 采集长度上界。16 kHz 采样率、64 倍抽取下，每毫秒约 128 字节 PDM。
 * 32 KiB 约等于 256 ms，够判断通路，又不至于在 NSH 里等太久。
 */

#define VG_PDM_MINBYTES   (64)

/* 单次 DMA 传输的上限。DMA 的传输计数寄存器只有 16 位，计数值最大 65535，
 * 而 PDM 按 8 位传输，所以单次最多 65535 字节。取 32768（约半秒）留足余量：
 * 这个值同时是 2 的幂，按任何位宽对齐都不会踩到计数上限。
 */

#define VG_PDM_MAXBYTES   (32768)

/* 进内存的字节率，换算成 bit/s 供显示用。PLL2P 49.143 MHz 经 MCKDIV 12
 * 得到 4.095 MHz 位时钟，32 位帧即 128 kHz 帧率；PDM 每帧向内存交出一个
 * 字节，所以字节率是 128 KB/s。这个值与 64 倍抽取到 16 kHz 自洽：
 * 128 KB/s ÷ 8 = 128 kHz 采样，÷ 8（每帧 8 个有效比特）… 实测核对过
 * 32768 字节耗时 270 ms，与 128 KB/s 相符。
 *
 * 只用于把字节数折算成时长，不参与任何采集决策。
 */

#define VG_PDM_BITRATE    (1024000.0)

/* 单次接收的等待上限。按最坏情况留足余量：32 KiB 在 1.024 MHz 位流下
 * 约 256 ms，超时给到 5 秒。
 */

#define VG_PDM_TIMEOUT_MS (5000)

/****************************************************************************
 * Private Types
 ****************************************************************************/

/* 分块长度，单位字节。PDM 每帧向内存交出一个字节，帧率 128 kHz，所以
 * 128 字节正好是 1 ms。声音包络的起伏在 10~100 ms 量级，1 ms 的分辨率
 * 足够看清它。
 */

#define VG_PDM_BLOCK_BYTES  (128)

/* 一次采集的统计量。只做只读统计，不含结论性判断。
 */

struct vg_pdm_stats
{
  size_t   nbytes;        /* 实际收到的字节数 */
  size_t   nzero;         /* 值为 0x00 的字节数 */
  size_t   nff;           /* 值为 0xFF 的字节数 */
  size_t   nbits;         /* 置位的比特总数 */
  double   ones_ratio;    /* 置位比特占比，静音时接近 0.5 */
  double   entropy;       /* 每字节香农熵，上限 8 bit */
  size_t   dup_pairs;     /* 相邻两字节相同的次数 */
  size_t   halfword_dups; /* 相邻两个半字相同的次数 */
  uint8_t  minbyte;
  uint8_t  maxbyte;
  uint32_t hist[16];      /* 高 4 位直方图，16 个桶 */

  /* 以下按 1 ms 分块统计。PDM 把信号幅度编码成本地密度偏离 0.5 的幅度，
   * 所以块内密度偏离量就是该毫秒的瞬时电平。整段平均会因为静音段被稀释，
   * 而声音的包络变化只能在这个尺度上看到。
   */

  size_t   nblocks;       /* 完整的块数 */
  double   dc;            /* 整段的置位密度，作为信号零点 */
  double   level_rms;     /* 各块密度减去直流分量后的均方根 */
  double   level_peak;    /* 最大的单块偏离绝对值 */
  double   blk_min;       /* 最小的单块置位密度 */
  double   blk_max;       /* 最大的单块置位密度 */
};


/****************************************************************************
 * Private Data
 ****************************************************************************/

static sem_t     g_done;
static int       g_result;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: vg_pdm_usage
 ****************************************************************************/

static void vg_pdm_usage(void)
{
  printf("usage: vgpdmdump [-n bytes] [-f path] [-q] [-x count] [-r rounds]\n"
         "  -n bytes   capture length, %d..%d, default 16384\n"
         "  -f path    output file, default /data/pdm.raw\n"
         "  -q         do not write a file, statistics only\n"
         "  -x count   also hex dump the first count bytes (max 256)\n"
         "  -r rounds  repeat: one line per round showing the level, no file\n"
         "             each round is about a quarter second (32768 bytes)\n",
         VG_PDM_MINBYTES, VG_PDM_MAXBYTES);
}

/****************************************************************************
 * Name: vg_pdm_hexdump
 *
 * Description:
 *   以十六进制打印一段前缀。板上没有 xxd 一类工具，需要把原始字节带回
 *   主机核对时只能靠这个输出。
 *
 ****************************************************************************/

static void vg_pdm_hexdump(FAR const uint8_t *buf, size_t nbytes)
{
  size_t i;

  for (i = 0; i < nbytes; i++)
    {
      if ((i & 15) == 0)
        {
          printf("  %04zu:", i);
        }

      printf(" %02x", buf[i]);

      if ((i & 15) == 15 || i + 1 == nbytes)
        {
          printf("\n");
        }
    }
}

/****************************************************************************
 * Name: vg_pdm_stats
 *
 * Description:
 *   统计一段 PDM 位流。所有字段从同一遍扫描得出，避免各字段来自缓冲区
 *   的不同状态。
 *
 ****************************************************************************/

static void vg_pdm_stats(FAR const uint8_t *buf, size_t nbytes,
                         FAR struct vg_pdm_stats *st)
{
  uint32_t counts[256];
  size_t   i;
  double   h;

  memset(st, 0, sizeof(*st));
  memset(counts, 0, sizeof(counts));

  st->nbytes  = nbytes;
  st->minbyte = 0xff;
  st->maxbyte = 0x00;

  for (i = 0; i < nbytes; i++)
    {
      uint8_t b = buf[i];

      counts[b]++;
      st->nbits += (size_t)__builtin_popcount(b);

      if (b == 0x00)
        {
          st->nzero++;
        }

      if (b == 0xff)
        {
          st->nff++;
        }

      if (b < st->minbyte)
        {
          st->minbyte = b;
        }

      if (b > st->maxbyte)
        {
          st->maxbyte = b;
        }

      st->hist[b >> 4]++;
    }

  if (nbytes > 0)
    {
      st->ones_ratio = (double)st->nbits / ((double)nbytes * 8.0);
    }

  /* 香农熵按字节取值计算。真实位流接近 8 bit；常量数据线为 0。
   */

  h = 0.0;
  for (i = 0; i < 256; i++)
    {
      if (counts[i] != 0)
        {
          double p = (double)counts[i] / (double)nbytes;
          h -= p * (log(p) / log(2.0));
        }
    }

  st->entropy = h;

  /* 字节级重复检测。半字里高低字节相同，往往说明 DMA 传输宽度大于外设
   * 实际给出的有效位宽：多出来的高位只是重复或补零。这比单看熵更能区分
   * “数据是常量”与“数据格式不对”。
   */

  for (i = 1; i < nbytes; i++)
    {
      if (buf[i] == buf[i - 1])
        {
          st->dup_pairs++;
        }
    }

  for (i = 2; i + 1 < nbytes; i += 2)
    {
      uint16_t a = (uint16_t)buf[i] | ((uint16_t)buf[i + 1] << 8);
      uint16_t b = (uint16_t)buf[i - 2] | ((uint16_t)buf[i - 1] << 8);

      if (a == b)
        {
          st->halfword_dups++;
        }
    }

  /* 按毫秒分块求瞬时电平。PDM 的输出密度等于模拟输入归一化到 [0,1]，
   * 所以一块内“1”的占比就是这一毫秒的瞬时电平。
   *
   * 偏离量必须相对整段的均值算，不能相对 0.5：麦克风有固定的直流偏置，
   * 这块板实测静音时密度落在 0.48 而不是 0.5。相对 0.5 算会把偏置当成
   * 信号，静音基线被抬高一个量级。
   *
   * 用偏离的均方根而不是峰峰值：峰值对单个异常块太敏感。
   */

  st->blk_min = 1.0;
  st->blk_max = 0.0;

  {
    size_t nblocks = nbytes / VG_PDM_BLOCK_BYTES;
    double sumsq = 0.0;
    double dc;
    size_t b;

    /* 直流分量用整段的置位密度，与判据里读到的 ones 是同一个量 */

    dc = (double)st->nbits / ((double)nbytes * 8.0);
    st->dc = dc;

    for (b = 0; b < nblocks; b++)
      {
        const uint8_t *p = &buf[b * VG_PDM_BLOCK_BYTES];
        size_t ones = 0;
        double dens;
        double dev;
        size_t k;

        for (k = 0; k < VG_PDM_BLOCK_BYTES; k++)
          {
            ones += (size_t)__builtin_popcount(p[k]);
          }

        dens = (double)ones /
               ((double)VG_PDM_BLOCK_BYTES * 8.0);
        dev  = dens - dc;

        sumsq += dev * dev;

        if (dev < 0.0)
          {
            dev = -dev;
          }

        if (dev > st->level_peak)
          {
            st->level_peak = dev;
          }

        if (dens < st->blk_min)
          {
            st->blk_min = dens;
          }

        if (dens > st->blk_max)
          {
            st->blk_max = dens;
          }
      }

    st->nblocks = nblocks;

    if (nblocks > 0)
      {
        st->level_rms = sqrt(sumsq / (double)nblocks);
      }
  }
}

/****************************************************************************
 * Name: vg_pdm_report
 ****************************************************************************/

static void vg_pdm_report(FAR const struct vg_pdm_stats *st)
{
  int i;

  printf("vgpdmdump: %zu bytes\n", st->nbytes);

  /* 电平行放在最前面：这是唯一能区分声音有无的量，其余都是数据完整性检查。
   */

  printf("  level: rms=%.4f peak=%.4f  dc=%.3f blk min=%.3f max=%.3f "
         "(%zu blocks)\n",
         st->level_rms, st->level_peak, st->dc,
         st->blk_min, st->blk_max, st->nblocks);

  printf("  zero=%.1f%% ones=%.3f entropy=%.3f bit/byte\n",
         (st->nbytes > 0) ?
           (100.0 * (double)st->nzero / (double)st->nbytes) : 0.0,
         st->ones_ratio, st->entropy);
  printf("  min=0x%02x max=0x%02x allff=%zu\n",
         st->minbyte, st->maxbyte, st->nff);
  printf("  dup: byte %.1f%% (of %zu)  halfword %.1f%% (of %zu)\n",
         (st->nbytes > 1) ?
           (100.0 * (double)st->dup_pairs / (double)(st->nbytes - 1)) : 0.0,
         (st->nbytes > 1) ? st->nbytes - 1 : (size_t)0,
         (st->nbytes > 3) ?
           (100.0 * (double)st->halfword_dups /
            (double)((st->nbytes - 2) / 2)) : 0.0,
         (st->nbytes > 3) ? (st->nbytes - 2) / 2 : (size_t)0);
  printf("  nibble histogram:");

  for (i = 0; i < 16; i++)
    {
      printf(" %lu", (unsigned long)st->hist[i]);
      if (i != 15)
        {
          printf(",");
        }
    }

  printf("\n");
}

/****************************************************************************
 * Name: vg_pdm_callback
 *
 * Description:
 *   I2S 接收完成回调。在驱动 worker 线程里执行，只记录结果并唤醒主线程，
 *   不做统计、不打印、不分配内存。
 *
 ****************************************************************************/

static void vg_pdm_callback(FAR struct i2s_dev_s *dev,
                            FAR struct ap_buffer_s *apb,
                            FAR void *arg, int result)
{
  (void)dev;
  (void)apb;
  (void)arg;

  g_result = result;
  nxsem_post(&g_done);
}

/****************************************************************************
 * Name: vg_pdm_capture
 *
 * Description:
 *   投递一次接收并等待完成，把结果统计到 st 里。缓冲区由调用者提供，
 *   以便 -r 模式复用同一块内存而不反复分配。
 *
 * Returned Value:
 *   0 成功；负的 errno 失败。
 *
 ****************************************************************************/

static int vg_pdm_capture(FAR struct i2s_dev_s *i2s,
                          FAR struct ap_buffer_s *apb,
                          size_t nbytes,
                          FAR struct vg_pdm_stats *st,
                          FAR double *elapsed_ms)
{
  struct timespec t0;
  struct timespec t1;
  int ret;

  nxsem_init(&g_done, 0, 0);
  g_result = -EBUSY;

  /* 计时从投递开始到回调返回。与按字节率折算的时长对照，用来判断缓冲区
   * 内容是否连续。
   */

  clock_gettime(CLOCK_MONOTONIC, &t0);

  ret = I2S_RECEIVE(i2s, apb, vg_pdm_callback, NULL, VG_PDM_TIMEOUT_MS);
  if (ret < 0)
    {
      fprintf(stderr, "vgpdmdump: I2S_RECEIVE failed: %d\n", ret);
      nxsem_destroy(&g_done);
      return ret;
    }

  ret = nxsem_wait(&g_done);
  if (ret < 0)
    {
      fprintf(stderr, "vgpdmdump: wait failed: %d\n", ret);
      nxsem_destroy(&g_done);
      return ret;
    }

  clock_gettime(CLOCK_MONOTONIC, &t1);
  nxsem_destroy(&g_done);

  if (g_result < 0)
    {
      /* 传输失败时把驱动在完成时刻抓到的寄存器快照打出来。SAI 的溢出与
       * 时钟配置错误走 SR，不经 DMA，所以单看 -EIO 分不清是哪一个。
       */

      uint32_t regs[STM32_PDM_DIAG_NREGS];

      fprintf(stderr, "vgpdmdump: transfer failed: %d\n", g_result);

      if (stm32_pdm_staterecord(regs, STM32_PDM_DIAG_NREGS) == 0)
        {
          fprintf(stderr,
                  "  isr=0x%02x sr=0x%08lx cr1=0x%08lx frcr=0x%08lx "
                  "slotr=0x%08lx pdmcr=0x%08lx\n",
                  (unsigned int)regs[0],
                  (unsigned long)regs[1], (unsigned long)regs[2],
                  (unsigned long)regs[3], (unsigned long)regs[4],
                  (unsigned long)regs[5]);
          fprintf(stderr,
                  "  dma scr=0x%08lx sndtr=%lu spar=0x%08lx sm0ar=0x%08lx "
                  "req=%lu\n",
                  (unsigned long)regs[6], (unsigned long)regs[7],
                  (unsigned long)regs[8], (unsigned long)regs[9],
                  (unsigned long)regs[10]);
        }

      return g_result;
    }

  /* 回调返回后驱动已做过 cache 失效，这里读的就是外设写入内存的内容。
   */

  vg_pdm_stats(apb->samp, nbytes, st);

  *elapsed_ms = (double)(t1.tv_sec - t0.tv_sec) * 1000.0 +
                (double)(t1.tv_nsec - t0.tv_nsec) / 1.0e6;

  return 0;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: vgpdmdump_main
 *
 * Description:
 *   NSH 入口。打开采集设备，投递一次定长接收，等回调完成后统计并落盘。
 *
 ****************************************************************************/

int main(int argc, FAR char *argv[])
{
  FAR const char *path = "/data/pdm.raw";
  size_t nbytes = 16384;
  bool nsize_given = false;
  bool quiet = false;
  size_t dumpbytes = 0;
  int rounds = 0;
  double elapsed_ms = 0.0;
  struct vg_pdm_stats stats;
  struct audio_buf_desc_s bufdesc;
  FAR struct ap_buffer_s *apb = NULL;
  FAR struct i2s_dev_s *i2s;
  uint32_t bitrate;
  int ret;
  int i;
  int out;

  for (i = 1; i < argc; i++)
    {
      if (strcmp(argv[i], "-n") == 0 && i + 1 < argc)
        {
          long v = strtol(argv[++i], NULL, 0);

          if (v < VG_PDM_MINBYTES || v > VG_PDM_MAXBYTES)
            {
              fprintf(stderr, "vgpdmdump: -n out of range\n");
              return EXIT_FAILURE;
            }

          nbytes = (size_t)v;
          nsize_given = true;
        }
      else if (strcmp(argv[i], "-f") == 0 && i + 1 < argc)
        {
          path = argv[++i];
        }
      else if (strcmp(argv[i], "-q") == 0)
        {
          quiet = true;
        }
      else if (strcmp(argv[i], "-x") == 0 && i + 1 < argc)
        {
          long v = strtol(argv[++i], NULL, 0);

          if (v < 0 || v > 256)
            {
              fprintf(stderr, "vgpdmdump: -x out of range\n");
              return EXIT_FAILURE;
            }

          dumpbytes = (size_t)v;
        }
      else if (strcmp(argv[i], "-r") == 0 && i + 1 < argc)
        {
          long v = strtol(argv[++i], NULL, 0);

          if (v < 1 || v > 600)
            {
              fprintf(stderr, "vgpdmdump: -r out of range\n");
              return EXIT_FAILURE;
            }

          rounds = (int)v;
        }
      else
        {
          vg_pdm_usage();
          return EXIT_FAILURE;
        }
    }

  /* 长度取整到 16 位传输单位的整数倍，避免最后一拍被截掉。
   */

  nbytes &= ~(size_t)1;

  i2s = stm32_sai_pdm_device();
  if (i2s == NULL)
    {
      fprintf(stderr, "vgpdmdump: PDM capture is not available\n");
      return EXIT_FAILURE;
    }

  /* 设置采样率与位宽。sai_samplerate 会重新计算主时钟分频，
   * sai_datawidth 会设置数据宽度；两者都在接收前生效。
   */

  bitrate = I2S_RXSAMPLERATE(i2s, 16000);
  I2S_RXDATAWIDTH(i2s, 16);

  printf("vgpdmdump: RX bitrate %lu\n", (unsigned long)bitrate);

  /* -r 模式：连续采集，每轮只打一行电平。用来在板子上直观看到说话时电平
   * 抬升，不必反复敲命令。不写文件，免得反复擦写 eMMC。
   */

  if (rounds > 0)
    {
      int r;

      /* 用户没有显式给 -n 时，每轮用单次传输的上限（约 256 ms）。默认的
       * 16384 字节只有约 128 ms，一轮太短，看不出声音的包络。
       */

      if (!nsize_given)
        {
          nbytes = VG_PDM_MAXBYTES;
        }

      memset(&bufdesc, 0, sizeof(bufdesc));
      bufdesc.numbytes  = nbytes;
      bufdesc.u.pbuffer = &apb;
      ret = apb_alloc(&bufdesc);
      if (ret < 0)
        {
          fprintf(stderr, "vgpdmdump: apb_alloc failed: %d\n", ret);
          return EXIT_FAILURE;
        }

      printf("vgpdmdump: %d rounds, %zu bytes each\n", rounds, nbytes);
      printf("  round  level_rms  peak   ones\n");

      for (r = 0; r < rounds; r++)
        {
          double el;

          if (vg_pdm_capture(i2s, apb, nbytes, &stats, &el) < 0)
            {
              apb_free(apb);
              return EXIT_FAILURE;
            }

          printf("  %5d  %.4f     %.4f %.3f\n",
                 r, stats.level_rms, stats.level_peak, stats.ones_ratio);
        }

      apb_free(apb);
      return EXIT_SUCCESS;
    }

  memset(&bufdesc, 0, sizeof(bufdesc));
  bufdesc.numbytes  = nbytes;
  bufdesc.u.pbuffer = &apb;
  ret = apb_alloc(&bufdesc);
  if (ret < 0)
    {
      fprintf(stderr, "vgpdmdump: apb_alloc failed: %d\n", ret);
      return EXIT_FAILURE;
    }

  /* apb_alloc 按 cache 行对齐分配样本区，驱动的 cache 维护因此总是完整的
   * 行。这里再确认一次，避免依赖分配器的实现细节。
   */

  if (((uintptr_t)apb->samp & (up_get_dcache_linesize() - 1)) != 0)
    {
      fprintf(stderr, "vgpdmdump: buffer is not cache line aligned\n");
      apb_free(apb);
      return EXIT_FAILURE;
    }

  if (vg_pdm_capture(i2s, apb, nbytes, &stats, &elapsed_ms) < 0)
    {
      apb_free(apb);
      return EXIT_FAILURE;
    }

  vg_pdm_report(&stats);

  {
    double stream_ms = (double)nbytes * 8.0 * 1000.0 / VG_PDM_BITRATE;

    printf("  elapsed=%.1f ms  stream=%.1f ms  rate=%.1f KB/s\n",
           elapsed_ms, stream_ms,
           (elapsed_ms > 0.0) ?
             ((double)nbytes / 1024.0 / (elapsed_ms / 1000.0)) : 0.0);
  }

  if (dumpbytes > 0)
    {
      vg_pdm_hexdump(apb->samp, dumpbytes);
    }

  if (!quiet)
    {
      out = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
      if (out < 0)
        {
          fprintf(stderr, "vgpdmdump: open %s failed: %d\n", path, errno);
          apb_free(apb);
          return EXIT_FAILURE;
        }

      ret = write(out, apb->samp, nbytes);
      if (ret != (int)nbytes)
        {
          fprintf(stderr, "vgpdmdump: short write %d of %zu: %d\n",
                  ret, nbytes, errno);
          close(out);
          apb_free(apb);
          return EXIT_FAILURE;
        }

      close(out);
      printf("vgpdmdump: wrote %zu bytes to %s\n", nbytes, path);
    }

  apb_free(apb);
  return EXIT_SUCCESS;
}

#endif /* CONFIG_VG_PDM_DUMP */
