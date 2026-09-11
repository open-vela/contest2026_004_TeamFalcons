---
name: velaguard-candidate-confirm
description: "VelaGuard 点表闸门：候选表试读后人确认 apply，采集表才变。Use when: vgpoint、点表、候选 candidate、apply --confirm、试读 test、vgpoint get、上位机 NSH、host_tests、points.json、灌表、point table commit。"
---

# VelaGuard 点表闸门

上位机经 ST-LINK NSH 写点表。编辑只动候选；人确认 `vgpoint apply --confirm` 之后，周期采集和告警才读已确认表。

命令口径只认 [docs/velaguard-host-nsh-protocol.md](../../../docs/velaguard-host-nsh-protocol.md)。不要在任务文档或本 Skill 再抄命令表。NSH 行长与 `too_long` 见 [references/gotchas.md](references/gotchas.md)。

生成 / 改 MThings `.mthings` 用已有 `mthings-automation-config-skill`，本 Skill 不产出从站工程。

## 两张表

| 表 | 路径 | 谁读 |
|----|------|------|
| 已确认 | `/data/velaguard/config/points.json` | 周期采集、首页、告警比较 |
| 候选 | `/data/velaguard/discover/point_table_candidate.json` | 仅 `vgpoint` / `vgdiscover` 编辑与试读 |
| 采集快照 | `/data/velaguard/live/values.txt` | 仅 `vgpoint get`；不占 RS485 |

`add` / `set` / `del` 只改候选。未 `apply --confirm` 之前采集表不变。冷启动只加载已确认表；缺失或损坏则首页为空，不回退镜像内置表。

## 闸门

1. 改候选（`add` / `set` / `del`）。`id` 主键，大小写敏感；`name` 可重复。查找、去重、告警关联一律按 `id`，不得按 `name` 索引。
2. `vgpoint test`（可带 id）：对**候选**做一次 Modbus 试读，占 RS485。总线已被扫描/落盘占用则 `bus_busy`，不要硬抢。
3. **停下来等人**。禁止把 `add` / `set` / `test` 和 `apply --confirm` 写在同一行。固件不得在 test 成功后自动 apply。
4. 单独发 `vgpoint apply --confirm`。没有 `--confirm` 必须 `ERR code=need_confirm`、退出码 1。
5. `vgpoint list` 核对已确认表。`vgpoint get` 读 HMI 快照，不占总线。

`vgpoint get` 不是轮询口，也不是试读。现场读从站用 `vgmodbus` 或 `vgpoint test`。

## 硬约束

- RS485 只给板做 Modbus 主站。不经 RS485、MQTT 或第二路 UART 下发配置。
- 同一总线只能有一个主站。MThings 做从站 mock 时，关掉 MThings 采集/主站会话，上位机不得自己轮询 485。
- 板端 Agent 禁止 `vgpoint`、`vgdiscover apply`、`vgcfg commit`（见 `app/velaguard/vg_agent_seed.c`）。
- 点数量上限 32（`VG_DISCOVER_MAX_POINTS`）。

## 验收

```bash
make -C app/velaguard/host_tests test
```

灌表演示（会在 `test` 之后等人回车，再单独 `apply --confirm`）：

```text
powershell.exe -ExecutionPolicy Bypass -File scripts/vgpoint_host_apply.ps1
powershell.exe -ExecutionPolicy Bypass -File scripts/vgpoint_host_apply.ps1 -PointsFile scripts/vgpoint_scene_room.json
```

默认点表：`scripts/vgpoint_demo_points.json`。场景点表：`scripts/vgpoint_scene_room.json`。
