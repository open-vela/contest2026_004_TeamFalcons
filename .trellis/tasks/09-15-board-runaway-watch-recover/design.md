# 设计：SDMMC PIO 状态的并发修复

## 边界

改动在 `../nuttx/arch/arm/src/stm32h7/stm32_sdmmc.c`。该文件当前已有未提交改动
（SDRAM/XIP 白名单、`syslog` 丢弃日志、FIFO monitor 的 `remaining != 0` 判断），本任务在其上继续。

## 问题

`stm32_recvfifo(priv)` 读改写 `priv->buffer` 与 `priv->remaining`：

```c
      if (priv->remaining >= sizeof(uint32_t))
        {
          memcpy(priv->buffer, &data.w, sizeof(uint32_t));
          priv->buffer     = (FAR uint32_t *)((uintptr_t)priv->buffer + 4);
          priv->remaining -= sizeof(uint32_t);
        }
```

调用点：

| 调用点 | 上下文 |
| --- | --- |
| `stm32_sdmmc_fifo_monitor()` `:1684` | **hpwork 线程** |
| 中断处理 `:1782` | ISR |
| monitor 重新入队 `:1696` | hpwork |

ISR 会抢占 hpwork 线程。线程读到 `remaining == 4`、刚做完比较还没扣减时被 ISR 抢走，
ISR 把 `remaining` 扣到 0 并推进 `buffer`；线程恢复后用**已经失效的** `remaining` 再扣一次，
`remaining` 变成 `0xFFFFFFFC`，之后每轮继续扣，最终落在观测到的 `0xFFFFFDF4`；
`buffer` 同步走过 SRAM4 末尾。

monitor 的守卫 `priv->remaining < FIFO_SIZE_IN_BYTES / 2` 是无符号比较，`remaining` 一旦
下溢成巨大值就永远为假，拦不住已经发生的事。

## 方案

### 选中：把 hpwork 路径的状态更新放进临界区

`stm32_sdmmc_fifo_monitor()` 里对 `priv->recvcount`/`priv->remaining`/`priv->buffer` 的**读与改**
必须在同一临界区内完成，ISR 不能插进来：

```c
static void stm32_sdmmc_fifo_monitor(void *arg)
{
  struct stm32_dev_s *priv = (struct stm32_dev_s *)arg;
  irqstate_t flags;

  /* The SDMMC interrupt handler runs stm32_recvfifo() on the same
   * buffer/remaining pair.  It can preempt this work-queue thread between
   * the test and the decrement, which drives remaining negative and walks
   * buffer past the end of SRAM4 (2026-09-15: remaining=0xFFFFFDF4,
   * buffer=0x38010000).  Take the whole decision and update together.
   */

  flags = enter_critical_section();

  if (priv->receivecnt && priv->remaining &&
      priv->remaining < FIFO_SIZE_IN_BYTES / 2)
    {
      stm32_recvfifo(priv);
    }

  ...requeue decision...

  leave_critical_section(flags);
}
```

要点：

- `stm32_recvfifo()` 内部会调 `sdmmc_getreg32()`/`putreg32()`，寄存器访问在临界区里没问题。
- 重新入队的判断也要放进同一个临界区，否则 `remaining` 在判断和使用之间同样会被改。
- `work_queue()` 放在临界区内调用是安全的（NuttX 的 work_queue 可在中断/临界区上下文调用）；
  若复核认为不妥，可只把**判断结果**存下来、出了临界区再入队。

### 未选

- **去掉 hpwork 轮询路径**：它是给 "lame FIFO" 打的补丁（构建期就有
  `#warning "Large Non-DMA transfer may result in RX overrun failures"`），删掉可能让
  RX overrun 复现。要删得先有证据说明它不再需要。
- **打开 `CONFIG_STM32H7_SDMMC_IDMA`**：绕开 PIO 是最彻底的方向，但涉及 DMA 通道、
  缓存一致性、与以太网/LTDC 的 DMA 资源竞争，属另一个量级，本任务不做。

## 契约

- 不改 `sdio.h` / `mmcsd` 上层接口，不改协议。
- 守卫白名单（`stm32_sdmmc_userbuf_ok`）继续只接受真实存在的内存窗口：
  DTCM / AXISRAM / SRAM123 / SRAM4 / FMC bank5+bank6（SDRAM）/ FMC bank4（XIP flash）。
  加新窗口时要一起看 `board.h` 与链接脚本。

## 风险与回滚

| 风险 | 处置 |
| --- | --- |
| 临界区把中断关久了，影响 SDMMC 吞吐或丢中断 | 临界区内只做状态更新与 FIFO 搬运，不等待；长跑看吞吐与是否出现新的 overrun |
| work_queue 在临界区内调用有副作用 | 复核 NuttX 实现；不行就改成"临界区内取判断结果、临界区外入队" |
| 并发不是唯一原因，改完仍崩 | 按 AC4 判断：日志里还有 `outside SRAM, drop` 就说明没修干净；`RECOVER` 仍频繁则回到证据链重查 |

回滚：单文件单函数改动，`git checkout` 该函数即可。

## 待验证的开放问题

- `stm32_recvfifo` 在 ISR 路径（`:1782`）是否已在关中断上下文——若是，线程侧加临界区就够了。
- `priv->remaining` 的类型（`size_t` 还是 `int`）决定下溢的具体算术，读代码确认后再写注释。
