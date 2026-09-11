# 点表 id+name 怎么改协议和结构

命令与应答仍以 [`docs/velaguard-host-nsh-protocol.md`](../../../docs/velaguard-host-nsh-protocol.md) 为准；先改该文再改代码。

## 数据怎么走

身份不再经过 `tag`：

```
PC JSON（id + name）
  → vgpoint add -i … / set <id> -N …
  → 候选 JSON 的 id/name
  → apply --confirm
  → 已确认 points.json
  → vg_point_entry.id / .name
  → vg_runtime_point_t
  → vg_sensor_t.id / .name
```

查找、去重、告警 `sensor_id`、NSH 位置参数全部用 `id`。`name` 只出现在 JSON、`POINT` 行、HMI 文案。

## 结构与 API

| 位置 | 改动 |
| --- | --- |
| `vg_point_entry` | `tag[24]` 改为 `id[24]` + `name[48]` |
| `vg_runtime_point_t` | 同样拆成 `id` + `name` |
| 校验 | `vg_point_validate_id`（原 tag 规则）；`vg_point_validate_name`（长度与禁字符） |
| 查找 | `vg_point_table_find_id`；删除 `find_tag` / `validate_tag` |
| JSON 读写 | 读 `id` 必填；`name` 缺省复制 `id`；写出 `id`/`name`；不写 `tag`；对象里只有 `tag` 则该点丢弃 |
| `vgpoint.c` | `-i`、位置参数 ID、`-N`、错误码 `dup_id`/`no_id` |
| 格式化 | `POINT id=… name=…`；`READ id=…` |
| 扫描推断 | `add_point` 写入 `id`，`name` 等于 `id` |
| 上位机 | demo 字段与脚本 `-i`/`-N`；串口 `Encoding = UTF8`；超 120 字节则拆成 `add` + `set -N` |

`schema_version` 保持 1。空阈值规则不变。

## 命令形态

```text
vgpoint add -i <id> -a <addr> -r <reg> [-N <name>] …
vgpoint set <id> [-N <name>] …
vgpoint del <id>
vgpoint test [<id>]
```

`list`/`test`/`apply`/`abort` 流程与两张表规则不改。

## 名称与 120 字节

`name` 在 RAM/JSON/HMI 最长 47 字节。一条带齐阈值的 `add` 再加长中文名会超过 120。不提高 `NSH_LINELEN`。主机脚本：`add` 只保证 `id`+地址+寄存器+阈值进得去；`name` 若使该行超长，下一行 `vgpoint set <id> -N <name>`。固件仍允许短 `add -N`。

`POINT` 的 `name=` 不得含空格，与现有脚本切分一致。串口必须 UTF-8，否则中文名在 Windows 默认代码页下会坏。

## 兼容

不做 `tag` → `id` 升级。旧 Host 单测里「只有 tag 的 JSON 仍能读出 1 个点」改为期望 0 个点。板上已确认表已清空，冷启动缺 `id` 则首页空，符合现有空表行为。

## 风险

- 仓外上位机若仍发 `-t` / `tag=`，会 `bad_arg`。本仓库脚本与 demo 同步改；仓外工程不在本任务改代码，只在规范里写清字段。
- JSON 写出 `name` 时禁止 `"` `\`，避免现有无转义 `fprintf` 打断对象。
- `vg_model_import_runtime_points` 必须分别拷 `id`/`name`，不能再把同一字符串填两栏。
