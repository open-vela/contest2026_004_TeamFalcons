---
name: velaguard-alarm-to-screen
description: "VelaGuard 本地告警上屏：按已确认点表阈值或离线弹出告警页并落盘。Use when: 告警上屏、阈值 warn/crit、cmp ge/le/eq、pending_alarm.txt、离线 offline、本地安全环 Local Safety Loop、alarm page、拔 485。"
---

# VelaGuard 告警上屏

本地安全环：采集已确认点表 → `vg_alarm_eval` 比较 → 告警页弹出 → 写入 `/data/velaguard/pending_alarm.txt`。断网也要成立，不经 MQTT 下发，不依赖 Agent。

实现：[`app/velaguard/vg_alarm_eval.c`](../../../app/velaguard/vg_alarm_eval.c)。字段口径：[`docs/velaguard-host-nsh-protocol.md`](../../../docs/velaguard-host-nsh-protocol.md) 第 4 节。术语：[`CONTEXT.md`](../../../CONTEXT.md) 的 Local Safety Loop / Offline / Recovering。

点表怎么写到板上，走 `velaguard-candidate-confirm`。本 Skill 假定已确认表已经带 `cmp` / `warn` / `crit`。

## 比较规则

只读已确认表。**不得**按点名称认水浸 / 烟雾。

- `cmp` 为空，或 `ge`/`le` 且 `warn` 与 `crit` 都空：只采集，不做模拟量告警。
- `eq`：用 `crit` 做等于判定（例如水浸等于 1）。`warn` 可空。
- `ge` / `le`：有 `warn` 则预警，有 `crit` 则严重。两者都有时，`ge` 要求 `warn <= crit`，`le` 要求 `warn >= crit`。
- 连续读失败达到 `fail_n` 次（默认 3，范围 1..20）：该点记 Offline，告警种类为离线。
- 严重优先于离线，离线优先于预警（`vg_alarm_kind_rank`）。

HMI 在 `vg_model_set_live` 路径上调用 `vg_alarm_eval`，命中则 `vg_pending_alarm_write`。

## 落盘与上屏

- 告警正文：`/data/velaguard/pending_alarm.txt`
- 屏幕自己读这份文件并弹出告警页；不要走 MQTT、不要等 Agent
- 日报 / 报告页读 `/data/velaguard/reports`，与待处理告警不是同一条路径
- 告警所属点恢复正常（在线读数回 `NONE`）时，HMI 清 `s_alarm` 并 `unlink` 待处理文件；Agent HEARTBEAT 也会删同一文件，两边都删不算错

## 明确不做

OTA、周报、规则库、开机同时拉起 HMI 和 Agent。Agent 解读告警是后一步（seed Skill `alarm_interpretation.md`），不能代替本闭环。

## 板测最小闭环

固件：作品主线 `velaguard-lvgl`。先用 `velaguard-candidate-confirm` 灌一张带阈值的已确认表。

1. 首页能列出已确认点（`nsh> vgpoint list` 点数与屏上一致）。
2. 注入：把模拟量打到 `warn`/`crit` 之外，或拔掉 485 / 关掉从站。
3. 告警页弹出（目视）。
4. `ls /data/velaguard/pending_alarm.txt` 文件存在。

Host 逻辑：`make -C app/velaguard/host_tests test`（含 `test_vgpoint`）。不要用按名称分支的旧告警路径。
