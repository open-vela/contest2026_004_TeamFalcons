# 点位当前值怎么给上位机

命令与应答仍以 [`docs/velaguard-host-nsh-protocol.md`](../../../docs/velaguard-host-nsh-protocol.md) 为准；先改该文再改代码。

## 数据怎么走

```
HMI 采集线程（已占 RS485）
  → g_live_v[] / g_live_on[]（屏上用）
  → 原子写出 /data/velaguard/live/values.txt
NSH vgpoint get
  → 只读该文件
  → VALUE 行 + OK/ERR
上位机
```

`vg_live_points_*` 仍是已确认**点表**副本，不是数值。数值快照是新文件，不要塞进 `points.json`。

## 快照格式

文本，便于 Host 单测和人工看。整文件覆盖写：`values.txt.tmp` → `rename`。

```text
tick_ms=<monotonic>
n=<int>
id=temp value=40.1 ok=1 unit=C
id=flood value=- ok=0 unit=-
```

`tick_ms` 为该轮采集写盘时的单调毫秒。`get` 用当前单调时间减它得到 `age_ms`（整文件同一年龄即可）。`unit` 空则 `-`。`ok=0` 时 `value=-`，与 `READ` 一致。

路径宏：`CONFIG_VG_LIVE_VALUES_PATH`，缺省 `/data/velaguard/live/values.txt`。

## 模块边界

| 模块 | 职责 |
| --- | --- |
| `vg_ui_backend_board.c` 采集线程 | 每轮 poll 结束后写快照；失败也写（各点 `ok=0`），好让 `get` 能区分「从未采集」和「本轮全失败」 |
| `vg_point_table.c` | 快照读写/解析、`VALUE` 行格式化（Host 可测） |
| `vgpoint.c` | 新 verb `get`；不调用 `vg_bus_try_lock` |
| 规范 | 第 1 节改为允许查询当前值；第 5/6/7 节加 `get` / `VALUE` / `no_sample` |

首次成功写出之前文件不存在 → `no_sample`。空已确认表：采集线程可写 `n=0` 的快照，或 `get` 见 `n_points==0` 直接 `OK n=0`（以「已确认表为空」为准，不必等快照）。

## 兼容

- 不改 `test` / `READ` / 写点表 verb
- Agent 白名单不增加 `vgpoint`
- 仓外上位机按新 `VALUE` 行解析即可

## 风险

- FAT 上 rename 要同目录；写盘失败不得让采集线程卡死（忽略错误，下一轮再写）
- 快照与点表 id 集合可能短暂不一致（刚 `apply`）：`get` 以快照为准，未知 id 仍 `no_id`；全表 `get` 只列快照里的点
- 命令体仍 ≤120 字节；`get` 很短
