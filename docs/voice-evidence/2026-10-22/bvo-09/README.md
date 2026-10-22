# BVO-09 异常与告警恢复只读接口

日期：2026-10-22。状态：数据层、只读工具与主机测试完成；板端工具已实测，真实从站在线下的告警与恢复事件仍待取证。

## 1. 交付

| 文件 | 变更 |
|---|---|
| `app/velaguard/vg_runtime.h` / `.c` | 新增 `vg_runtime_anomalies()`、`vg_runtime_recovery()`、`vg_runtime_boot_id()`、`vg_runtime_events_dropped()`、`vg_runtime_points_seen()` |
| `app/velaguard/vg_agent_tools.c` | `vg_anomaly_read`、`vg_alarm_recovery` 两个只读工具与输出格式 |
| `app/velaguard/vgagent.c` | `vgagent tool vg_alarm_recovery <点>` 直接按 point_id 包装 |
| `app/velaguard/host_tests/test_runtime_anomaly.c` | 主机测试，6 个用例 |
| `app/velaguard/host_tests/test_agent_tools.c` | 工具输出格式用例 |

## 2. 行为

- 异常快照覆盖运行层见过的全部点位。运行层每个采集周期对已确认点表逐点调用 `vg_runtime_note_sample()`，集合就是当前已确认点，不经过报告的 8 点截断。
- 每个点位一条记录，类别用标志位表示：规则阈值、离线、数据过期可以同时出现，`total` 统计点位数，不重复计数。
- 排序：严重阈值、普通阈值、离线、仅过期。同级别按点位 ID 排序，保证同一快照产生同样的播报摘要。
- 恢复结论只来自事件环。最近事件是 RAISE 为进行中；是 CLEAR 且同 episode 的 RAISE 仍在为已恢复；RAISE 已被覆盖为已恢复但起点未知；没有任何事件且当前正常为无法确认，不把当前正常当作历史恢复。
- 事件环 32 条，覆盖时 `vg_runtime_events_dropped()` 增加。恢复时长只在 RAISE 与 CLEAR 都保留时给出。
- 事件环在 RAM，全部记录属于本次上电。`vg_runtime_boot_id()` 供回答注明启动标识。

## 3. 板端实测

[board-live-slave.log](board-live-slave.log)，固件 SHA256 `a5a0031c...1f4d44f8`，2026-10-22 18:39-18:42。主机侧 `maintenance_rtu_slave.ps1` 在 COM7 作 RTU 从站，板端是总线上唯一主站。

| 工况 | 工具输出 |
|---|---|
| 14 点全部在线、数值正常 | `vganomaly: total=0 seen=14`；`vg_point_read temp` 返回 `value=24 ok=1 age_ms=1780` |
| 温度切到 36.0（阈值 35） | `total=1 shown=1`；`id=temp rank=crit flags=threshold value=36.0 threshold=35.0 age_ms=1500 since_ms=16460` |
| 恢复 24.0 后查询 | `vgrecovery: id=temp kind=threshold state=recovered history_complete=true duration_ms=31000 episode=1` |
| 仅 1 号从站不应答约 18 秒 | 点位查询已返回 `value=- reason=read_failed`，异常集合仍为 0 |
| 同一工况约 44 秒 | `total=2`：`temp` 与 `humid` 均 `flags=offline,stale`，保留最后读数，`age_ms=45760` |
| 恢复应答 | `total=0`；事件环出现 `offline_recovered` 的 CLEAR |

事件环按时间记录同一测点的完整过程：

```
18:40:09 id=temp action=raise kind=threshold value=36 thr=35 reason=threshold_ge
18:40:40 id=temp action=clear kind=threshold value=24 reason=threshold_cleared
18:41:19 id=temp action=raise kind=offline   value=24 reason=modbus_fail_offline
18:42:13 id=temp action=clear kind=offline   value=24 reason=offline_recovered
```

- 阈值恢复给出了 31,000 ms 的时长，与 18:40:09 到 18:40:40 一致。
- 离线判定不是立即发生：约 18 秒时点位查询已无读数，异常集合仍为 0；约 44 秒才进入 `offline`。这与采集失败计数窗口一致，回答不能把刚掉线的点说成在线。
- 从站日志显示 1,253 次请求、1,184 次应答、69 次按模式抑制、`crc_scan=0`，没有第二个主站。

无在线从站时的结果保留在 [board-anomaly-recovery.log](board-anomaly-recovery.log)：`total=14 shown=12 seen=14`、`omitted=2`、未采样点 `value=-`，恢复返回 `unknown`。

## 4. 本轮在板端发现并修复的问题

| 现象 | 原因 | 处理 |
|---|---|---|
| `flags=,offline` | 逗号写在每个标志前，第一个标志也带逗号 | 只在非首项前加逗号，新增两标志与单标志用例 |
| 未采样点显示 `value=0.0` | 格式化没有检查 `has_value` | 无样本显示 `value=-` |
| 首轮采集前输出 `total=0`，读起来像无异常 | 没有区分未采集与无异常 | 新增 `vg_runtime_points_seen()` 与 `note=first_cycle_pending` |
| `vgagent tool vg_alarm_recovery ups_load` 返回 `缺少 point_id` | NSH 去掉引号，裸词被包装成 query | 恢复工具按 point_id 包装 |

## 5. 主机测试

```
cd app/velaguard/host_tests
make test_runtime test_runtime_anomaly test_agent_tools
./test_runtime
./test_runtime_anomaly
./test_agent_tools
```

结果：`test_runtime: OK`、`test_runtime_anomaly: ok`、`test_agent_tools: OK`。

覆盖用例：

1. 12 个阈值点全部返回，受限读取仍报告完整 total。
2. 离线加点位过期只产生一条记录，total 不重复计数。
3. 在线但样本过期单独成项，排序最后；新样本不算异常。
4. 已恢复、进行中、无法确认、离线进行中四种恢复结论。
5. RAISE 被覆盖后的部分恢复：状态正确，时长未知。
6. `seen` 按点位计数：同一测点重复采样不加一，离线点仍计入。
7. 工具输出：单标志无前导逗号、双标志各一个逗号、无样本不出现数值、首轮未完成带提示。

## 6. 未完成

- 板端已取到 `active`、`recovered` 与 `unknown` 三种结论；`RECOVERED_PARTIAL` 需要 32 条事件把 RAISE 挤掉，本轮在线从站只产生 5 条，未在板端构造。
- 同一测点同时处于阈值与离线只在主机用例中构造；板端观察到的是阈值与离线先后发生，以及离线与过期同时出现。
- 配置删除点位后运行层记录不会主动清除；当前点表驱动的异常集合不受影响，但 `vg_runtime_recovery()` 仍可按 ID 查询旧事件。若后续需要严格隔离，需在配置提交时清理。
- 语音回答如何引用异常总数与前三项属于 BVO-10，本任务只提供工具与快照。
