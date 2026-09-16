# 跑飞特征与监控实测（2026-09-15）

## 交付：常驻监控 `scripts/board_watch_recover.ps1`

定时探活（默认 20 s 发 CR 等 `nsh>`），认两种跑飞形态：

1. 打印 `HARDFAULT` / `Assertion failed` / `panic:` / `stack_dump:` / `dump_tasks:`；
2. 静默挂死——串口一个字节都不吐，连 CR 都不回 `nsh>`。

命中就抓现场（把缓冲里残余的字节先写进日志），再走 **SWD** 复位（VCP 的 DTR 脉冲推不动这块板子），
复位后复核 `eMMC mounted at /mnt/emmc`，没挂上再补一条 `vgcfg probe` 定性。全程带时间戳写
`.debug/board_watch_recover.log`。

实测一轮（`-ProbeSec 10 -ProbeTimeoutSec 6 -MaxRecoveries 1`）：

```
start port=COM3 probe=10s
HARDFAULT: CFSR=00008200 HFSR=40000000 BFAR=00000010
PANIC detected (#1)
RECOVER #1 reason=panic
eMMC mounted at /mnt/emmc
RECOVER #1 done, booted, eMMC mounted=yes
stop panics=2 wedges=0 recoveries=1
```

自动恢复可用。踩过的坑：Windows PowerShell 按 ANSI 码页读 `.ps1`，UTF-8 中文注释会被解码成
乱码引号、把语法打断——脚本必须纯 ASCII。函数里改计数要用 `$script:recoveries++`，否则只改局部副本。

## 跑飞的稳定特征

两次崩溃的 PC 相同，调用方不同：

| 次 | PC | LR | 故障任务 |
| --- | --- | --- | --- |
| 1 | `wd_start_abstick` (`0x90002a22`) | `mmcsd_write` (`mmcsd_sdio.c`) | vghmi |
| 2 | `wd_start_abstick` (`0x90002a22`) | `work_dispatch` (`kwork_thread.c`) | lpwork |

`CFSR=00008200`（BFARVALID + PRECISERR），`BFAR=00000010`。同期 `dump_tasks` 里出现
`Invalid` 任务项（`STACKBASE 0`、`STACKSIZE 0`），说明就绪任务链表也被写坏了。

**触发路径是 `mmcsd_write`**：SDMMC 块写会起一个超时看门狗，走到 `wd_start` 就炸。

## 真凶方向：`g_wdactivelist` 被写坏

`../nuttx/sched/wdog/wd_start.c` 有一份**未提交**改动，注释自己写明了同一个崩溃：

```
/* g_wdactivelist.next == NULL is list_is_clear, not empty. Empty is
 * next == &list. 2026-09-14: vg_net_thread nxsem_clockwait walked a
 * cleared list and HARDFAULT at wd_start_abstick [NULL,#16] BFAR=0x10.
 */
```

它加的是 **补救**：在 `wd_expiration()` 和 `wd_insert()` 里发现链表被清零就 `list_initialize()` 重建。
但今天仍然以完全相同的 PC/BFAR 崩溃——说明补救没覆盖住，或者根本没找到是谁清空的。

同类前科：09-14 `ai_agent` 在 `mm_forcefree` 里的 HARDFAULT，以及
`fix(stm32h750b-dk): move SDRAM heap above LVGL fb reserve; mm_foreach corruption guard`。
指向同一个病根：**有东西在写坏内核全局结构**（看门狗链表、就绪任务链表、堆 freelist）。

## 病根定位：SDMMC 私有结构被写坏（2026-09-15 夜）

崩溃日志里**每次都是这两行紧挨着**（第 2/3 行与第 234/235 行）：

```
ERROR: RX buf 0x38010000 rem 4294929716 outside SRAM, drop
HARDFAULT: CFSR=00008200 HFSR=40000000 BFAR=00000010
```

- `0x38010000` 就是 09-14 那次 busfault 报的地址，正好是 SRAM4 的末尾。
- `4294929716` = `0xFFFFFDF4` = 有符号 -524 —— `priv->remaining` **下溢**了。

`priv->remaining` / `priv->buffer` 都属于 `struct stm32_dev_s`（`g_sdmmcdev1` @ `0x24003420`，
大小 `0x160`）。这两个字段同时是垃圾值，说明**整个 SDMMC 私有结构被写坏**。之后 `mmcsd_write`
拿它里面已经损坏的 `waitwdog` 去 `wd_start`，把全局看门狗链表（`g_wdactivelist` @ `0x240061a4`）
一起带崩——崩溃 PC 落在 `wd_start_abstick`，与 09-14 记录的那次完全同址。

`g_sdmmcdev1`（0x24003420）与 `g_wdactivelist`（0x240061a4）相距 0x2D84，不是相邻溢出。

### 最可能的机制：PIO FIFO 状态被两个上下文同时改

`stm32_recvfifo(priv)` 有三个调用点：

- `stm32_sdmmc_fifo_monitor()`（`stm32_sdmmc.c:1684`）——跑在 **hpwork** 上
- 中断处理里（`:1782`）
- 以及 monitor 自己重新入队（`:1696`）

它们都不加锁地改 `priv->buffer` 和 `priv->remaining`。monitor 上的守卫是
`priv->remaining && priv->remaining < FIFO_SIZE_IN_BYTES / 2`，用的是无符号比较：
一旦在中途被 ISR 抢走并重复扣减，`remaining` 就会下溢成巨大值，`buffer` 一路走过 SRAM4 末尾。

这也正好解释了为什么 09-14 打的补丁（`remaining != 0` 才重新入队）压不住：
它挡的是 hpwork 空转，没挡 ISR 与 monitor 的并发。

## 下一步（未做）

1. 定位 `g_wdactivelist` 与就绪任务链表所在的 RAM 段，看它们相邻的缓冲区是谁。
2. 查 `mmcsd_write` → `wd_start` 这条路径上，谁在临界区外操作看门狗节点。
3. 是否有指针越界把 `.bss` 里的链表清零（SDRAM 堆、LVGL 缓冲、SDMMC PIO 缓冲都是嫌疑）。
4. 监控已就位，可以长跑统计崩溃间隔，判断是"开机必崩"还是"跑一段时间才崩"，
   以及是否与 eMMC 负载相关（崩点固定在 `mmcsd_write`，这条线索很强）。
