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
