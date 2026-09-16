# 板子跑飞：监控 + 并发根因修复

证据链见 `research/runaway-signature.md`（含实测日志、符号解析、时序）。本文件不重复那些数据。

## 目标

板子会跑飞——既有打印 `HARDFAULT` 的，也有一个字节都不吐的静默挂死。先有能自动救回来的监控，
再修掉根因，让长跑稳定。

## 背景（已确认）

崩溃固定落在 `wd_start_abstick`（BFAR=0x10），调用方是 `mmcsd_write`。紧挨在崩溃之前的一行是
`stm32_sdmmc` 打的 `ERROR: RX buf 0x38010000 rem 4294929716 outside SRAM, drop`——
`priv->remaining` 下溢成 `0xFFFFFDF4`（-524），`priv->buffer` 走到了 SRAM4 末尾（0x38010000，
正是 09-14 报过的地址）。这两个字段同属 `struct stm32_dev_s`，说明整个 SDMMC 私有结构被写坏；
`mmcsd_write` 随后拿它里面已损坏的 `waitwdog` 去 `wd_start`，把全局看门狗链表一起带崩。

`stm32_recvfifo()` 有三个调用点，其中 `stm32_sdmmc_fifo_monitor()` 跑在 hpwork 线程上，
与中断处理并发地改 `priv->buffer` / `priv->remaining`，且都没有互斥。

## 需求

R1. **监控常驻**（已完成，`scripts/board_watch_recover.ps1`）：定时探活，认 panic 与静默挂死两种形态，
抓现场，走 SWD 复位并复核 `eMMC mounted`，全过程带时间戳落盘。脚本必须纯 ASCII
（Windows PowerShell 按 ANSI 码页读 `.ps1`，UTF-8 中文注释会打断语法）。

R2. **修掉 PIO 状态并发**：`priv->buffer` / `priv->remaining` 的读改写必须是原子的。
hpwork 线程不能被中断处理抢占到一半，否则 `remaining` 下溢、`buffer` 走出 SRAM4。

R3. **保留可见性**：守卫丢弃传输时必须打日志（已改为 `syslog(LOG_ERR, ...)`，不要退回 `mcerr`，
没有 `CONFIG_DEBUG_MEMCARD_ERROR` 时它是 `_none`）。

R4. **长跑验证**：改动后要用 R1 的监控连续跑，崩溃间隔显著拉长才算数；单次启动不崩不算证据。

## 验收标准

- [ ] AC1 监控可用（已达成：实测一轮 `panics=2 wedges=0 recoveries=1`，`RECOVER #1 done, booted, eMMC mounted=yes`）
- [ ] AC2 `stm32_recvfifo()` 在 hpwork 路径上的 `buffer`/`remaining` 更新有互斥，说明写进代码注释
- [ ] AC3 长跑（≥30 min）用 `scripts/board_watch_recover.ps1` 观察，`RECOVER` 次数为 0
- [ ] AC4 长跑日志里不再出现 `outside SRAM, drop`；若出现，说明 R2 没修干净
- [ ] AC5 eMMC 在长跑期间保持可用：`vgcfg probe` 回 `ok`、`vgpoint list` 回 `n=14`
- [ ] AC6 `bash scripts/build.sh` 编译通过，无新增警告

## 范围外

- 打开 `CONFIG_STM32H7_SDMMC_IDMA` 彻底绕开 PIO——方向对，但属于更大的改动，单独立项
- eMMC 总线宽度（硬件 8-bit / 驱动 1-bit）与时钟时序
- 上位机、点表协议、HMI 功能

## 关键决定

| 项 | 决定 |
| --- | --- |
| 首选修法 | 在 hpwork 路径上加临界区，让 PIO 状态更新原子化；不推倒重写驱动 |
| 守卫 | 保留，但必须能出声（`syslog`），它现在是最后一道防线 |
| 复位手段 | 只走 SWD；VCP 的 DTR 脉冲推不动这块板子 |
| 验证方式 | 以监控长跑为准，不以"烧完能起来"为准 |
