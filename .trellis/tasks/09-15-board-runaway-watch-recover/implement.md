# 执行计划

环境：本仓 `/home/hello19y/openvela/contest2026_004_TeamFalcons`，驱动在 `../nuttx`。
板子走 COM3（Windows 侧，从 WSL 用 `/mnt/c/Windows/System32/WindowsPowerShell/v1.0/powershell.exe` 调）。
**复位只走 SWD**：`.debug/nsh_reset_only.ps1`（VCP 的 DTR 脉冲推不动这块板子）。

## 步骤

1. **读代码确认前提**（不要跳）
   - `stm32_recvfifo()` 在中断路径（`stm32_sdmmc.c:1782` 附近）是不是已经在关中断上下文。
   - `priv->remaining` 的类型（`size_t` / `int`），决定下溢算术与注释写法。
   - `work_queue()` 能否在临界区内调用。
   - 证据：`research/runaway-signature.md`。

2. **改 `stm32_sdmmc_fifo_monitor()`**，按 `design.md` 把 `priv->recvcount`/`remaining`/`buffer`
   的读改写与重新入队判断放进同一个 `enter_critical_section()` 区间，注释写明为什么
   （引用 2026-09-15 的 `remaining=0xFFFFFDF4` / `buffer=0x38010000`）。

3. **确认守卫仍然出声**：三处丢弃点保持 `syslog(LOG_ERR, ...)`，不要退回 `mcerr`。

4. **编译**：`bash scripts/build.sh`，确认无新增警告（既有的
   `Large Non-DMA transfer may result in RX overrun failures` 保留）。

5. **烧录**：`powershell.exe -ExecutionPolicy Bypass -File scripts\flash.ps1`

6. **长跑验证**（关键，不能省）：
   ```
   powershell.exe -ExecutionPolicy Bypass -File scripts\board_watch_recover.ps1 -ProbeSec 20
   ```
   至少 30 分钟。它是常驻的，会自己复位并记录；Ctrl-C 停。

7. **收工核对**：见下方验证门。

## 验证门

| 门 | 判据 |
| --- | --- |
| 编译 | `bash scripts/build.sh` 通过，无新增警告 |
| 崩溃 | 长跑日志里 `RECOVER` 次数为 0（AC3） |
| 丢弃 | 长跑日志里不再出现 `outside SRAM, drop`（AC4）；仍出现说明并发没修干净 |
| 存储 | 长跑后 `vgcfg probe` 回 `ok`、`vgpoint list` 回 `n=14`（AC5） |
| 单测 | `make -C app/velaguard/host_tests test` 仍全绿 |

## 材料

- 监控：`scripts/board_watch_recover.ps1`，日志 `.debug/board_watch_recover.log`
- 现场采集：`.debug/vg_storage_diag.ps1`（命令文件驱动）、`.debug/nsh_boot_capture.ps1`（复位+抓启动）
- 复位：`.debug/nsh_reset_only.ps1`（`-Hard` 走硬件复位）
- 符号解析：
  ```
  A2L=/home/hello19y/openvela/prebuilts/gcc/linux-x86_64/arm-none-eabi/bin/arm-none-eabi-addr2line
  $A2L -f -C -e .debug/nuttx.elf <addr>
  ```

## 回滚点

单函数改动，回滚就是 `git checkout ../nuttx/arch/arm/src/stm32h7/stm32_sdmmc.c` 的该函数；
但注意该文件**还带着两份前序未提交改动**（白名单、`syslog`），回滚时别一起丢掉。
