# vgpoint 易错点

命令字段与稳定应答以 [docs/velaguard-host-nsh-protocol.md](../../../../docs/velaguard-host-nsh-protocol.md) 为准。

## NSH 行限制

作品主线：`CONFIG_NSH_LINELEN=128`，`CONFIG_NSH_MAXARGUMENTS=32`。命令字节数（不含行结束符）上限 **120**。超长返回 `too_long`，不得截断执行。

带齐字段的 `add` 约 26 个词。名称较长时先 `add` 再 `set <id> -N <name>`。

上位机发 LF。等再次出现 `nsh>` 再发下一条。脚本只认协议第 6 节稳定状态行。

## test 与 get

| 命令 | 读哪张 | 占 RS485 |
|------|--------|----------|
| `vgpoint test` | 候选 | 是 |
| `vgpoint get` | 已确认点的采集快照 | 否 |
| `vgmodbus` | 现场寄存器 | 是，且不认点位 id |

候选为空：`test` → `no_candidate`。已确认空：`get` → `n=0`，无 VALUE 行。快照尚未写出：`no_sample`。

## apply

- `vgpoint apply --confirm`：候选覆盖已确认，并刷新屏幕点表。
- 无 `--confirm`：必须失败。不要做成 dry-run 却返回成功。
- 空候选文件允许落盘成空已确认表（删光全部点）。`test` 对空候选仍失败。
- `vgpoint abort`：丢掉候选，采集不受影响。

## 旧 discover 表

`vgdiscover apply --confirm` 写出的旧表可能没有 `cmp` / `warn` / `crit`。读入时这些字段视为空：采集可用，不做模拟量告警，直到上位机 `set` 补上。

JSON 里空阈值要**省略该键**，不要写 `null`。旧字段 `tag` 不是身份：只有 `tag`、没有合法 `id` 的点丢弃。

## 参数字母

- `-i` id，`-N` 显示名，`-n` 仍是 `fail_n`
- `-C` 严重值（大写），避免和 `list -c`（打印候选）混淆
- `-k -` 清掉比较方式；`-w -` / `-C -` 清空对应阈值
