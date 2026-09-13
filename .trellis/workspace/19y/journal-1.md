# Journal - 19y (Part 1)

> AI development session journal
> Started: 2026-07-28

---



## Session 1: VelaGuard minimal bring-up config: resident main entry + parallel NSH shell, Keil-style build tasks

**Date**: 2026-08-14
**Task**: VelaGuard minimal bring-up config: resident main entry + parallel NSH shell, Keil-style build tasks
**Branch**: `learn_vela`

### Summary

velaguard-min preset complete and hardware-verified: stm32h750b-dk minimal config (console/NSH, LED, RS485, ESP-01S, PWM), VG_BRINGUP_TOOLS decoupled from LVGL; single-form architecture with velaguard_app_main as CONFIG_INIT_ENTRYPOINT running the persistent main loop (future LVGL) while hosting an NSH thread for shell debugging (vgpwm/vgrs485/vgesp); g_app_running re-entry guard; TIM15 CH2OUT typo patch; Keil-style VSCode tasks (Build/Rebuild All/Flash/Build & Flash) with build_minimal.sh self-healing config check (INIT_ENTRYPOINT/INIT_ENTRYNAME); removed OpenOCD/GDB bridge scripts; defconfig patch idempotent; trellis-check passed including INIT_ENTRYNAME fix.

### Git Commits

| Hash | Message |
|------|---------|
| `ac72b2a` | (see git log) |
| `ce52216` | (see git log) |
| `2eda6e5` | (see git log) |
| `6c82905` | (see git log) |
| `9621f15` | (see git log) |
| `b4bb152` | (see git log) |

### Status

[OK] **Completed**


## Session 2: 阶段1最小网络闭环：velaguard-net 预设 + vgmqtt（RJ45→MQTT→LWT）

**Date**: 2026-08-14
**Task**: 阶段1最小网络闭环：velaguard-net 预设 + vgmqtt（RJ45→MQTT→LWT）
**Branch**: `learn_vela`

### Summary

M1/M2 全部验收通过(AC1-AC5)：velaguard-net 预设(patch+幂等apply, velaguard-min+网络栈+MQTTC, 无LVGL)；vgmqtt(MQTT-C, status QoS0 retained+LWT, CONNACK/PUBACK 真实确认, -w 保持窗口)；build_minimal.sh 多目标 net|min|lvgl + 防呆清旧产物；VSCode 任务选择器。现场：DHCP 192.168.137.28(ICS), ping 网关0%丢包, status/LWT 双端对账(观察端151.3s/174.0s)。踩坑：kconfig-conf menu跨文件报错需kconfiglib；Windows网桥须含WiFi+USB双成员；QoS1 PUBACK回程不稳(阶段3复查)；mqtt_mq_length非完成信号(mqtt_mq_clean仅缓冲不足时调用)。

### Git Commits

| Hash | Message |
|------|---------|
| `4954841` | (see git log) |

### Status

[OK] **Completed**

## Session: Stage0 eMMC bring-up complete

**Date**: 2026-08-29
**Task**: `08-29-stage0-emmc`
**Branch**: `learn_vela`

### Summary

STM32H750B-DK eMMC (SDMMC1) bring-up PASS: velaguard-emmc preset, board stm32_sdmmc (no CD), mount `/mnt/emmc`, FAT LFN, probe file survives cold reset. Hardware SoT locked as BOUNDARY V11. Stock NuttX H7 path is 1-bit MMC (8-bit HW wired). Powerfail-store planning drafted next.

### Status

[OK] **Completed** (AC1–AC5). Awaiting human git commit; not archived yet.


## Session 3: stage0 powerfail-store board AC
<!-- trellis-session: v=2 fp=98a59bf62e3c6e54 -->

**Date**: 2026-08-29
**Task**: stage0 powerfail-store board AC
**Branch**: `learn_vela`

### Summary

Dual-slot FAT config store + vgcfg; fixed boot race, POSIX write, LIBC_SCANSET parse; AC1–AC5 board/host pass; archived task.

### Git Commits

| Hash | Message |
|------|---------|
| `3883537` | feat(velaguard): add dual-slot power-fail config store and vgcfg |
| `f258dcf` | docs(velaguard): record dual-slot config store contract and stage0 AC |
| `f8be1eb` | chore(build): keep emmc target wiring for vgcfg bring-up |

### Status

[OK] **Completed**


## Session 4: stage0 RS485 DIR/TC board AC
<!-- trellis-session: v=2 fp=880e7483d2fc4f37 -->

**Date**: 2026-08-29
**Task**: stage0 RS485 DIR/TC board AC
**Branch**: `learn_vela`

### Summary

stm32h7 RS485 tcdrain waits TXE|TC; dropped vgrs485 usleep; LA+new transceiver AC3/rx PASS; archived task.

### Git Commits

| Hash | Message |
|------|---------|
| `b7e41b1` | fix(velaguard): drop RS485 usleep after tcdrain waits for TC |
| `e3106dd` | docs(velaguard): record RS485 DIR/TC board bring-up and AC |

### Status

[OK] **Completed**


## Session 5: stage0 frame stats MVP B
<!-- trellis-session: v=2 fp=fd029fa2547408bf -->

**Date**: 2026-08-30
**Task**: stage0 frame stats MVP B
**Branch**: `learn_vela`

### Summary

vg_frame_stats ring window + vgstats NSH; vgmodbus hook; host tests pass; emmc build OK; AC2 board pending inject/vgmodbus verify

### Git Commits

| Hash | Message |
|------|---------|
| `86b694e` | feat(velaguard): add sliding-window frame stats and vgstats NSH |

### Status

[OK] **Completed**


## Session 6: stage1 agent-ops board AC
<!-- trellis-session: v=2 fp=stage1-agent-ops-ac -->

**Date**: 2026-08-30
**Task**: stage1-agent-ops + stage1-ai-agent + stage1-data-layout
**Branch**: `learn_vela`

### Summary

板端运营日报验收通过（build 13:19:33；`daily-20260228.md` 1796B；`END status=ok iters=6 tools=6 elapsed=115s`）。联调修复 LLM watchdog、daemon attach、symlink 路径、MiMo JSON、run_shell 输出、120s 超时。AC4 告警解读代码就绪，板测留可选 §4。三子任务 PRD/笔记更新并归档。

### Status

[OK] **Completed**

## Session: park vgpoint, lock demo MVP

**Date**: 2026-09-09
**Task**: `08-30-stage1-min-product` (active child still `08-30-stage1-lvgl-hmi`)
**Branch**: `learn_vela`

### Summary

COM3/NSH `vgpoint` 上位机写入待办 `09-09-nsh-vgpoint-host-editor`（P2，未 start）。9/20 演示 MVP 收成：CSV 点表阈值 → 注入水浸或离线 → HMI 告警 + `pending_alarm.txt`；双固件演示。不做运行时加点。

### Status

[OK] Backlog written; F1 not implemented this turn.


## Session 7: 点表数据趋势页恢复与增强（trend-page-live）
<!-- trellis-session: v=2 fp=2c1a951ee5db371a -->

**Date**: 2026-09-13
**Task**: 点表数据趋势页恢复与增强（trend-page-live）
**Branch**: `integrate-learn-vela-0912`

### Summary

解锁阶段2门禁的趋势页：板上点历史16到128（BSS实测+28.6KB，sram占41.4%），页内lv_dropdown按id选点并显示点位名，窗口改为最近60点/全部样本语义，无阈值点位Y轴自适应量程；新增无头验证gui/headless/trend_check_main.c全PASS（告警页回归ALL PASS）；板端build+烧录+hmi验收pass=15/fail=3（均为环境项）；LCD目视确认待人工

### Git Commits

| Hash | Message |
|------|---------|
| `a274607` | feat(gui): raise board point history to 128 samples for trend page |
| `9af83a1` | feat(gui): trend page point dropdown, sample-count windows, auto Y range |
| `056ec87` | docs(task): record trend-page-live verification results |
| `de6bb55` | chore(task): archive trend-page-live |

### Testing

- [OK] bash scripts/build.sh; /tmp/trend_check 全PASS; stage1_lvgl_hmi_accept pass=15 fail=3(环境项); make -C app/velaguard/host_tests 未跑(无点表协议变更)

### Status

[OK] **Completed**

### Next Steps

- LCD人工目视趋势页与下拉选择；落盘持久化另开任务评估


## Session 8: 现场测试修复与告警页多分栏归档
<!-- trellis-session: v=2 fp=94fb684c6b5f4017 -->

**Date**: 2026-09-13
**Task**: 现场测试修复与告警页多分栏归档
**Branch**: `integrate-learn-vela-0912`

### Summary

Session summary was not supplied.

### Main Changes

- field-test-fixes-0913：在线抖动（字节超时 50ms + 单次重试 + 周期去重）、首页告警列表原地刷新、报告页刷新按钮经 HEARTBEAT.poke 触发 MiMo 再生成日报、状态栏标题后附日期
- alarm-page-multi-rows：模型层告警期挂到每个点并派生主告警，告警页多点分栏显示，逐点标记处理与静音，首页快捷静音作用于全部活动告警；pending_alarm.txt 保持单条主告警格式，全部活动告警恢复才 unlink
- 新增无头渲染验收 gui/headless/alarm_check_main.c（离屏 480x272 + 脚本化点击 + PPM 出图），LVGL 9 可点击卡片内普通容器吞点击的坑与无头工具用法沉淀进 frontend spec
- 边界规则正文并入根 AGENTS.md，docs/agents/BOUNDARY.md 缩为来源索引；README 修正未落地 claim、新增 NSH 命令速查与三张移动端 SVG

### Git Commits

| Hash | Message |
|------|---------|
| `8a75c1e` | fix(velaguard): stop live online/offline flapping (retry + per-cycle apply) |
| `c3b0429` | fix(gui): rebuild home filter index on live changes |
| `604b389` | feat(gui): report page refresh button pokes agent heartbeat |
| `77ab678` | feat(gui): append date to the status bar title |
| `6694b1c` | docs(agents): consolidate boundary rules into root AGENTS.md |
| `938c98b` | feat(gui): multi-row alarm page with per-point ack and mute |
| `728ec4c` | docs(readme): honest claims, NSH command quick reference, mobile SVGs |
| `701cfe4` | docs(task): add field-test-fixes-0913 task files |
| `5f2fbd7` | chore(logs): collect AI coding sessions 09-12/09-13 |

### Testing

- [OK] make -C app/velaguard/host_tests test 本次复跑 7 项全绿
- [OK] bash scripts/build.sh（velaguard-lvgl）本次复跑编译通过并产出 nuttx.hex
- [OK] PC 模拟器冒烟与板端 COM3 验收以任务 implement.md 记录为准，本次未复跑

### Status

[OK] **Completed**

### Next Steps

- 板端真机过一遍告警页多分栏 AC1-AC6；09-11-host-point-live-query 状态 done 但未归档，需确认后归档
