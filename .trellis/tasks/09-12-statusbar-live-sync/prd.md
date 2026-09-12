# 顶部状态栏与真实状态同步

## Goal

顶部状态栏 chips（NET/WIFI/MiMo/ACQ/AUD/告警）目前由 mock 场景驱动，板上 NET/MiMo 永远显示异常、ACQ 永远正常、WiFi 没有入口。本任务让每个 chip 反映真实运行状态。

## Requirements

1. `vg_net_mgr` 新增结构化状态查询 `vg_net_mgr_status()`：RJ45 link/IP/ping、WiFi assoc/IP、MQTT 会话在线、活动出口。复用 `vg_net_mgr_print_status` 已有字段，加锁读取。
2. `vg_ui_backend` 新增 `poll_net`：板上实现调 `vg_net_mgr_status()` 并检查 `/dev/pwm0`（蜂鸣器）得出 AUD；PC 模拟实现返回 false，保持场景 mock 行为。
3. `vg_model` 板端 tick 同步 `s_net`：
   - NET：RJ45 有 IP 或 WiFi 有 IP 才算 ok
   - WIFI：新 chip，assoc 且有 IP 显示 WIFI（info），否则灰色
   - MiMo：MQTT CONNACK 在线才 ok（OTA 中仍显示 OTA）
   - ACQ：由点表实时在线状态推导，全部离线才算采集故障
   - AUD：`/dev/pwm0` 可访问才 ok
   - IP：系统页显示活动出口 IP
4. 状态变化才 notify，不引入每秒全屏重绘。
5. 告警 chip 已由点表阈值/离线告警驱动（`vg_alarm_eval`），本任务只验证不动逻辑。

## Constraints / Non-goals

- 不改告警评估逻辑、不做状态栏布局重构（480px 内加一个 chip）。
- PC 模拟器行为不变（场景键 1-6 演示流保持）。

## Acceptance Criteria

- [x] `bash scripts/build.sh` 编译通过
- [ ] 板上：插网线后 NET/WIFI/MiMo 按真实状态点亮；拔网线后 NET 变红、MiMo 断开变灰
- [ ] 拔 RS485 后 ACQ 变红；蜂鸣器设备缺失时 AUD 显示异常
- [ ] PC 模拟器场景键演示行为不变
