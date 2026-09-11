# 上位机按点位 ID 查询当前值

所属父任务：`09-09-pre-920-score-play`。命令与稳定应答以 [`docs/velaguard-host-nsh-protocol.md`](../../../docs/velaguard-host-nsh-protocol.md) 为唯一口径；实现时改该文，本文不另开命令表。

## 目标

上位机经 ST-LINK 虚拟串口，按点位 `id`（或一次查全部）读取已确认表每个点的当前数值，与首页同一拍采集结果对齐。不推送曲线，不改点表，不抢 RS485。

## 背景

`vgpoint list` 只有配置。`vgpoint test` 对候选表做一次 Modbus 试读。`vgmodbus` 不认点位 `id`。首页数值在 HMI 进程的 `g_live_v[]` 里，NSH 的 `vgpoint` 是另一进程。采集循环大约每 200 ms 加一轮总线读。点主键是 `id`。

## 需求

R1. 新 verb：`vgpoint get` 与 `vgpoint get <id>`。只读已确认表对应的采集快照。不占用 RS485，不改候选/已确认 JSON。

R2. 稳定行前缀 `vgpoint: VALUE`（与 `test` 的 `READ` 区分）。每点一行：`id=` `value=` `ok=` `unit=` `age_ms=`。`ok=0` 时 `value=-`。按 `id` 查找；全表省略 id。找不到 `no_id`。

R3. 空已确认表：`OK cmd=get table=committed n=0`，无 VALUE 行。快照文件不存在（HMI 尚未写出）：`ERR code=no_sample`。

R4. HMI 每轮采集结束后把快照原子写入 eMMC（先写临时文件再改名）。`get` 只读该文件。Agent 仍禁止全部 `vgpoint`（含 `get`）；现场读从站仍用 `vgmodbus` 或 `vgpoint test`。

R5. Host 单测覆盖快照解析、`VALUE` 行格式、缺文件 `no_sample`、按 id 过滤。固件编译作品主线通过。

## 验收标准

- [x] AC1 规范增加 `get` / `VALUE` / `no_sample`；`test` 的 `READ` 含义不变
- [x] AC2 `vgpoint get` 不触发 RS485；与同时进行的 HMI 采集不互相 `bus_busy`
- [x] AC3 `get <id>` 只输出该点；未知 id 为 `no_id`
- [x] AC4 无快照 → `no_sample`；空表 → `OK n=0`
- [x] AC5 `make -C app/velaguard/host_tests test` 通过
- [x] AC6 `bash scripts/build.sh` 通过

## 范围外

- 主动推送、MQTT 订阅、二进制帧
- 查询时现场读从站、改采集周期
- 仓外上位机 GUI（板端协议 + 可选验收脚本）
- 把 `get` 放进 Agent 白名单

## 关键决定

| 项 | 决定 |
| --- | --- |
| 数据来源 | HMI 最近一次采集快照，不抢总线 |
| 命令 | `vgpoint get` [`<id>`] |
| 稳定行 | `vgpoint: VALUE`，不用 `READ` |
| 索引 | 仅 `id` |
| Agent | 仍禁止全部 `vgpoint` |
