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

[board-anomaly-recovery.log](board-anomaly-recovery.log)，固件 SHA256 `0d517611...1504dbd0`，2026-10-22 18:10，RS485 无在线从站：

```
vgagent tool vg_anomaly_read
vganomaly: total=14 shown=12 seen=14 boot=00004f42
- id=acu_ret rank=offline flags=offline value=- threshold=0.0 age_ms=0 since_ms=193730
...
- omitted=2

vgagent tool vg_alarm_recovery ups_load
vgrecovery: id=ups_load kind=threshold state=unknown history_complete=false duration_ms=0 episode=0 boot=00004f42
```

- 14 个已确认点全部进入异常集合，工具一次只列 12 条并给出 `omitted=2`，总数不因截断而变小。
- 从未采样成功的点显示 `value=-`，不把 0.0 当成读数。
- 没有事件时恢复结论为 `unknown`，没有把当前离线或当前正常当成历史结论。
- 上电后首轮采集完成前，两个工具都能回答；异常集合为空时输出 `note=first_cycle_pending`，避免把未采集读成现场健康。

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

- 没有真实在线从站，因此没有板端 RAISE/CLEAR 事件。恢复的四种结论只在主机用例中验证，板端只取到 `unknown`。
- 配置删除点位后运行层记录不会主动清除；当前点表驱动的异常集合不受影响，但 `vg_runtime_recovery()` 仍可按 ID 查询旧事件。若后续需要严格隔离，需在配置提交时清理。
- 语音回答如何引用异常总数与前三项属于 BVO-10，本任务只提供工具与快照。
