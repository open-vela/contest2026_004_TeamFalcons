# BVO-02 USER1 按键事件验收记录

- 日期：2026-10-21
- 任务：BVO-02，依据 `board-voice-execution-plan.md` 第 5 节与 ADR 0009
- 状态：代码完成，主机回归通过，板端四类工况实测通过
- 证据目录：本目录

## 1. 任务范围

BVO-02 的完成条件是「至少 30 次按放无漏发/重复提交；长按只提交一次；应用无忙轮询」。本轮只做事件层：把 USER1 的按下与释放变成「开始录音」和「提交」两个事件，带上去抖、15 秒上限与释放门控。不录音、不联网、不做界面。

## 2. 改动

| 仓库 / 分支 | 文件 | 内容 |
|---|---|---|
| `../nuttx` `velaguard/ptt-user1-stm32h750b-dk` | `boards/arm/stm32h7/stm32h750b-dk/src/stm32_buttons.c`（新增） | `board_button_initialize` / `board_buttons` / `board_button_irq` |
| 同上 | `.../src/stm32h750b-dk.h` | 新增 `GPIO_BTN_USER`：PC13、输入、下拉、EXTI |
| 同上 | `.../include/board.h` | 修正按键注释：原写 NUCLEO 的 PI11，实为 PC13 |
| 同上 | `.../Kconfig` | 新增 `STM32H750B_DK_BUTTONS`，select 两个 `ARCH_HAVE_*BUTTONS` |
| 同上 | `.../src/Makefile`、`.../src/CMakeLists.txt` | 按 `CONFIG_ARCH_BUTTONS` 编入新文件 |
| 同上 | `.../src/stm32_bringup.c` | 注册 `/dev/buttons` |
| 竞赛仓 | `app/velaguard/vg_ptt.c`、`vg_ptt.h`（新增） | 纯逻辑状态机，无 NuttX 依赖，可主机测试 |
| 竞赛仓 | `app/velaguard/vg_ptt_board.c`、`.h`（新增） | `/dev/buttons` 适配层与 `vgptt` 任务 |
| 竞赛仓 | `app/velaguard/vgptt.c`（新增） | NSH 诊断命令 `vgptt start\|stop\|status\|reset\|level` |
| 竞赛仓 | `app/velaguard/Kconfig`、`Makefile`、`velaguard.c` | `CONFIG_VG_PTT` 与开机自动启动 |
| 竞赛仓 | `scripts/configs/velaguard-lvgl.defconfig` | 打开按键与 `CONFIG_VG_PTT` |
| 竞赛仓 | `scripts/build.sh` | 预设校验增加按键与 PTT 符号，避免换 defconfig 后静默漏配 |
| 竞赛仓 | `app/velaguard/host_tests/test_ptt.c`、`test_ptt_cycles.c`（新增） | 主机时序回归 |

## 3. 硬件结论

引脚与极性来自 BVO-01 的 M-05 实测与 ST BSP v3.3.4：

- B1 USER 接 PC13，按下为高。BSP 用 `GPIO_PULLDOWN` 加 `GPIO_MODE_IT_RISING`。
- 原理图 `STM32_microcontroller_IOs.SchDoc` 中 PC13 标注 `Wakeup & Key Button`，与 BSP 一致。
- 本轮另核对：`Arduino_Connector.SchDoc` 的 LD8 是 Arduino 连接器上的用户 LED，接 PD3（NuttX 的 `GPIO_LD3`），与按键无关。不能用它判断按键状态。

## 4. 主机回归

`make -C app/velaguard/host_tests test` 全部通过，其中与本任务相关的两个目标：

| 目标 | 覆盖 |
|---|---|
| `test_ptt` | 单次按放；上电即按住；按下与松手抖动；长按到 15 秒上限；松手边沿落在上限整点（不得重复提交）；连续两次独立会话；32 位时钟回绕；等待超时查询；名称映射；空指针容忍 |
| `test_ptt_cycles` | 30 次正常按放；10 次带抖动的按放；10 次超过上限的长按；上电即按住后恢复正常。逐次断言事件数量精确匹配 |

关键断言：长按到上限只提交一次，随后松手不再补提交；松手边沿与上限同刻时按松手提交，只产生一个提交。

## 5. 构建

`bash scripts/build.sh`（TARGET=velaguard-lvgl）通过，`stm32_buttons.c` 参与编译。ELF 中可见：

```text
board_button_initialize   board_buttons   board_button_irq
btn_lower_initialize      vg_ptt_start    vg_ptt_update    vg_ptt_stop
vgptt_main
```

## 6. 板端

固件：nuttx `1b5e71d2f87-dirty`（本任务改动未提交时的构建），`nuttx.bin` SHA256 `61e9e2fddacb6f7ee8b2c95e363563602d45dfc8580143d0612e36b33ee1a401`，烧录文件 `.debug/nuttx.hex` SHA256 `a6d73bf36cc91406b40e8a518807cbbca2940fb5cd2f0d9b40270e0b18766bae`。串口 COM3，115200。测试在 2026-10-21 由操作者手动按放，记录取自 `vgptt status`。

### 6.1 前置事实

- `/dev/buttons` 节点存在；`vgptt` 作为独立 Task 运行（`ps` 类型 Task，优先级 90），开机自动启动。
- 按键边沿能唤醒 `poll`：一次 110 ms 的按下被完整记录为 `begin` 与 `submit`，时长与操作一致。

### 6.2 工况与结果

| 工况 | 操作 | 实测 | 判定 |
|---|---|---|---|
| 单次按放 | 按住约 1 秒 | `begins=5 submits=5 timeouts=0 last_ms=1100` | 通过。时长与操作一致 |
| 连续按放（完成条件 30 次） | 手动连续按放 43 次，每次约 1 秒 | `begins=43 submits=43 timeouts=0`，结束时 `state=idle` | 通过。开始与提交严格 1:1，无漏发、无重复 |
| 长按上限 | 按住 18 秒以上再松手 | 按住期间 `begins=1 submits=1 timeouts=1 last_ms=15000 state=held`；松手后仍 `submits=1`，`state=idle` | 通过。到上限只提交一次，松手不追加 |
| 空闲 | 20 秒不触碰 | `samples` 增加 27（约 1.5 Hz），`events=0 begins=0` | 通过。无忙轮询，无误触发 |

单次按放那一轮的唤醒统计：`samples=270`、`timeouts_wake=260`、`events=10`。差值 10 与 5 次按下加 5 次松开完全对应，说明这些采样由按键中断触发，而不是靠周期窗口发现。

### 6.3 判定

执行方案第 5 节对 BVO-02 的完成条件是「至少 30 次按放无漏发/重复提交；长按只提交一次；应用无忙轮询」，三条均有上述实测支持。阶段 A 其余项（录音、回放、录放互斥）属于 BVO-03 至 BVO-05，不在本项范围。

### 6.4 过程中被推翻的判断

本轮有两处中途假设经实测否定，记录以免后续复用错误结论：

1. 最初用 pthread 承载按键线程。NSH 的 builtin 命令跑在自己的任务组里，命令返回后同组 pthread 一起被回收，`vgptt start` 之后线程并不存活。改为 `task_create` 后稳定。
2. 曾怀疑 EXTI 没有唤醒 `poll`，据此加了 10 Hz 定时采样。后来用一次 110 ms 的按下被完整记录否定了这个假设：周期采样不可能量出这个时长。定时采样已退回按需等待。

另需记录：`Arduino_Connector.SchDoc` 里的 LD8 是 Arduino 连接器上的用户 LED，接 PD3（NuttX 的 `GPIO_LD3`），与按键状态无关，不能用它判断按键。早前几轮「按键时 LED 亮」的观察因此不能作为按键已被受理的证据，相关轮次的数据视为无效。

## 7. 未做与边界

- 不录音、不联网、不显示界面；事件只打印一行并驱动一颗 LED。
- 不修改 `AGENTS.md`、ADR 0009、执行方案原文。
- `VG_PTT` 依赖 `INPUT_BUTTONS` 与 `ARCH_IRQBUTTONS`，其它预设未开启该组合。
