# BVO-09 异常与告警恢复只读接口

日期：2026-10-22。状态：数据层与主机测试完成；工具暴露在 BVO-10 接线；板端未验证。

## 1. 交付

| 文件 | 变更 |
|---|---|
| `app/velaguard/vg_runtime.h` | 新增 `vg_runtime_anomalies()`、`vg_runtime_recovery()`、`vg_runtime_boot_id()`、`vg_runtime_events_dropped()` 与两个结构体 |
| `app/velaguard/vg_runtime.c` | 事件增加 `serial` 与 `episode`；点状态记录阈值起始时间和两个 episode；环形覆盖计数 |
| `app/velaguard/host_tests/test_runtime_anomaly.c` | 新增主机测试 |
| `app/velaguard/host_tests/Makefile` | 加入 `test_runtime_anomaly` |

## 2. 行为

- 异常快照覆盖运行层见过的全部点位。运行层每个采集周期对已确认点表逐点调用 `vg_runtime_note_sample()`，所以集合就是当前已确认点，不经过报告的 8 点截断。
- 每个点位一条记录，类别用标志位表示：规则阈值、离线、数据过期可以同时出现，`total` 统计点位数，不重复计数。
- 排序：严重阈值、普通阈值、离线、仅过期。同级别按点位 ID 排序，保证同一快照产生同样的播报摘要。
- 恢复结论只来自事件环。最近事件是 RAISE 为进行中；是 CLEAR 且同 episode 的 RAISE 仍在为已恢复；RAISE 已被覆盖为已恢复但起点未知；没有任何事件且当前正常为无法确认，不把当前正常当作历史恢复。
- 事件环 32 条，覆盖时 `vg_runtime_events_dropped()` 增加。恢复时长只在 RAISE 与 CLEAR 都保留时给出。
- 事件环在 RAM，全部记录属于本次上电。`vg_runtime_boot_id()` 供回答注明启动标识。

## 3. 主机测试

```
cd app/velaguard/host_tests
make test_runtime test_runtime_anomaly
./test_runtime
./test_runtime_anomaly
```

结果：`test_runtime: OK`、`test_runtime_anomaly: ok`。

覆盖用例：

1. 12 个阈值点全部返回，受限读取仍报告完整 total。
2. 离线加点位过期只产生一条记录，total 不重复计数。
3. 在线但样本过期单独成项，排序最后；新样本不算异常。
4. 已恢复、进行中、无法确认、离线进行中四种恢复结论。
5. RAISE 被覆盖后的部分恢复：状态正确，时长未知。

## 4. 未完成

- 未烧录，未在真实采集周期下观察。主机测试直接驱动 `vg_runtime_note_sample()`，不代替板端证据。
- 语音工具与回答文本在 BVO-10 接入，本任务不注册新工具。
- 配置删除点位后运行层记录不会主动清除；当前点表驱动的异常集合不受影响，但 `vg_runtime_recovery()` 仍可按 ID 查询旧事件。若后续需要严格隔离，需在配置提交时清理。
