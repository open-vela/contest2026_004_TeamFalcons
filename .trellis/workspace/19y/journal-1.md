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
