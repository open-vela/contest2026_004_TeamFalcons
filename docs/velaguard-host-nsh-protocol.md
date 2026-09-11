# 板端与上位机 NSH 协议（vgpoint）

| 项 | 内容 |
|----|------|
| 状态 | 规范；`vgpoint` 已实现。主键为 `id` + 显示名 `name`；`get` 读采集快照（`09-11-host-point-live-query`） |
| 实现任务 | `09-11-host-point-live-query`（主键拆分在 `09-11-point-id-name-split`；灌表骨架在 `09-10-host-nsh-vgpoint`） |
| 固件 | 作品主线 `velaguard-lvgl` |
| 依据 | BOUNDARY V5；现有 `vgdiscover` / `points.json` / COM3 验收脚本 |

本文是实现和验收的唯一命令口径。命令表不要在任务文档里再抄一份，任务里只引用本文。

---

## 1. 范围

上位机经 ST-LINK 虚拟串口，向板载 NSH 按行发送文本命令，批量写入或修改运行时点表（从站地址、寄存器、倍率、比较方式、预警值、严重值），并按 `id` 查询已确认点的当前值。

写点表走候选/`apply`。查当前值走 `vgpoint get`，读 HMI 最近一次采集快照，不占用 RS485。不订阅实时曲线，不推送告警。告警仍由板端写入 `/data/velaguard/pending_alarm.txt`，屏幕自己读。

RS485 只给板做 Modbus 主站。不经 RS485、MQTT 或第二路 UART 下发配置。

---

## 2. 会话

| 项 | 规则 |
|----|------|
| 物理口 | ST-LINK VCP。现有验收脚本：Windows `COM3`、115200 8N1 |
| 对端 | 板载 NSH，提示符 `nsh>` |
| 行结束 | 上位机发 LF（`\n`）。CRLF 也可，板端按 NSH 惯例处理 |
| 一条命令 | 一行。禁止把 `add` / `set` / `test` 和 `apply --confirm` 写在同一行 |
| 命令结束 | 再次出现 `nsh>` |
| 退出码 | 成功 0，失败 1 |

作品主线 defconfig：`CONFIG_NSH_LINELEN=128`，`CONFIG_NSH_MAXARGUMENTS=32`。带齐字段的 `add` 约 26 个词；NSH 默认最多 7 个参数（库内还会抬到 11），不够会在进 `vgpoint` 之前报 `too many arguments`。命令字节数（不含行结束符）上限 **120**。超长返回 `too_long`，不得截断执行。

人可读的说明可以打印，脚本**只认**第 6 节的稳定状态行。等 `nsh>` 再发下一条。

---

## 3. 两张表

| 表 | 路径 | 谁读 |
|----|------|------|
| 已确认（committed） | `/data/velaguard/config/points.json` | 周期采集、首页、告警比较 |
| 候选（candidate） | `/data/velaguard/discover/point_table_candidate.json` | 仅 `vgpoint` / `vgdiscover` 编辑与试读 |
| 采集快照 | `/data/velaguard/live/values.txt` | 仅 `vgpoint get`；HMI 每轮采集结束后原子覆盖 |

冷启动：等 `/data` 挂上后只加载已确认表。文件缺失或损坏则首页为空，**不**回退编译进镜像的 `vg_mthings_points[]`。

`add` / `set` / `del` 只改候选。采集在 `apply --confirm` 成功之前仍用已确认表。

第一次改候选时：若候选文件不存在，则把已确认表复制过去；已确认也没有，则从空表开始。

`vgdiscover apply --confirm` 写出的旧表没有阈值字段。读入时 `cmp` / `warn` / `crit` 视为空：采集可用，不做模拟量告警，直到上位机 `set` 补上。

点数量上限与 `VG_DISCOVER_MAX_POINTS` 相同，当前为 **32**。

---

## 4. 点记录

扩展现有 `points.json`，不另做第三份格式。`schema_version` 保持 `1`。阈值等字段均可缺省；**`id` 必填**。

每个点两个身份字段：`id`（主键）和 `name`（显示名）。`id` 大小写敏感，同一张表里不得重复。`name` 可重复，可含中文。`addr`+`reg` 允许重复，不推荐。

查找、去重、`set` / `del` / `test` / `get`、告警关联一律按 `id`。不得按 `name` 索引。旧字段 `tag` 不是身份字段：对象里只有 `tag`、没有合法 `id` 的点丢弃，不升成 `id`。

| 字段 | 类型 | 约束 | 缺省 |
|------|------|------|------|
| `id` | 字符串 | `[A-Za-z0-9_]{1,23}` | 必填 |
| `name` | 字符串 | UTF-8，1..47 字节；禁止 ASCII 空白、`=`、`"`、`\` 和控制字符 | 缺省等于 `id` |
| `addr` | 整数 | 1..247 | 必填 |
| `fc` | 整数 | 3 或 4 | 3 |
| `reg` | 整数 | 0..65535 | 必填 |
| `qty` | 整数 | 1..4 | 1 |
| `dtype` | 字符串 | `int16` 或 `uint16` | `int16` |
| `scale` | 浮点 | 有限值，建议大于 0 | `1` |
| `unit` | 字符串 | 最多 7 个 ASCII 字符 | 空 |
| `cmp` | 字符串 | `ge` / `le` / `eq`，或空 | 空：不做模拟量告警 |
| `warn` | 浮点或空 | 有限值 | 空 |
| `crit` | 浮点或空 | 有限值 | 空 |
| `fail_n` | 整数 | 1..20 | `3`（连续读失败则离线） |

告警规则（实现阶段执行，规范先定口径）：

- `cmp` 为空，或 `ge`/`le` 时 `warn` 与 `crit` 都空：该点不做模拟量告警，仍采集。
- `eq`：用 `crit` 作为等于判定（例如水浸等于 1）。`warn` 可空。
- `ge` / `le`：有 `warn` 则预警，有 `crit` 则严重。两者都有时，`ge` 要求 `warn <= crit`，`le` 要求 `warn >= crit`。
- 离线：该点连续读失败达到 `fail_n` 次。

JSON 里空阈值**省略该键**，不要写 `null`（板端解析按数字 `sscanf`）。例：

```json
{"schema_version":1,"bus":{"device":"/dev/rs485","baud":9600},"hits":[1],"points":[{"id":"temp","name":"温度","addr":1,"fc":3,"reg":0,"qty":1,"dtype":"int16","scale":0.1,"unit":"C","cmp":"ge","warn":40,"crit":55,"fail_n":3},{"id":"flood","name":"水浸","addr":2,"fc":3,"reg":2,"qty":1,"dtype":"uint16","scale":1,"unit":"","cmp":"eq","crit":1,"fail_n":3}]}
```

---

## 5. 命令

通用形态：

```text
vgpoint <verb> [args...]
```

未知 verb、缺参数、类型不对：`ERR code=bad_arg`。

### 5.1 `list`

```text
vgpoint list
vgpoint list -c
```

默认打印已确认表。`-c` 打印候选。只读。

### 5.2 `add`

只写候选。`id` 已存在则 `dup_id`，表满则 `full`。

```text
vgpoint add -i <id> -a <addr> -r <reg> [-N <name>] [-f <fc>] [-q <qty>] [-d <dtype>] [-s <scale>] [-u <unit>] [-k <cmp>] [-w <warn>] [-C <crit>] [-n <fail_n>]
```

`-i` / `-a` / `-r` 必填。`-N` 为显示名（大写），缺省等于 `id`。`-n` 仍是 `fail_n`。`-C` 是严重值（大写），避免和 `list -c` 混淆。`-t` 不再使用。

整行仍受 120 字节限制。名称较长时先 `add` 再 `set <id> -N <name>`。

### 5.3 `set`

只写候选。只改给出的字段，其余保持。`id` 不在候选中则 `no_id`。

```text
vgpoint set <id> [-N <name>] [-a <addr>] [-r <reg>] [-f <fc>] [-q <qty>] [-d <dtype>] [-s <scale>] [-u <unit>] [-k <cmp>] [-w <warn>] [-C <crit>] [-n <fail_n>]
```

把 `cmp` / `warn` / `crit` 清成「不做模拟量告警」：`-k -`，并省略 `-w` / `-C`。`-w -` 或 `-C -` 表示清空该阈值。

### 5.4 `del`

```text
vgpoint del <id>
```

只从候选删除。不存在则 `no_id`。已确认表要等 `apply --confirm` 才会少这个点。

### 5.5 `test`

```text
vgpoint test
vgpoint test <id>
```

对候选做一次保持寄存器读（`fc=3` 用 FC03，`fc=4` 用 FC04）。省略 id 则试读候选里全部点。不落盘，不改已确认表。

试读时必须占用总线忙标志，让周期采集让路（实现时复用 `board_bus_busy()` 一类状态）。总线已被扫描/落盘占用则 `bus_busy`，不要硬抢 `/dev/rs485`。

某个点读失败：该点 `READ ... ok=0`，命令仍可 `OK`（部分失败）。候选为空则 `no_candidate`。全部点都读失败才 `test_fail`。

### 5.6 `get`

```text
vgpoint get
vgpoint get <id>
```

读已确认表对应的采集快照，不占用 RS485，不改候选或已确认 JSON。省略 id 则输出快照里全部点；给出 id 则只输出该点。

已确认表为空（文件缺失或点数为 0）：`OK cmd=get table=committed n=0`，无 VALUE 行。带 id 且已确认表为空：`no_id`。

已确认表非空但快照文件尚不存在（HMI 还未写出）：`no_sample`。给出的 id 不在快照中：`no_id`。全表 `get` 以快照为准，只列快照里的点。

`get` 不得调用总线锁。现场读从站仍用 `vgmodbus` 或 `vgpoint test`。

### 5.7 `apply`

```text
vgpoint apply --confirm
```

把候选覆盖到已确认路径，并按现有双槽规则调用 `vg_config_commit`（设备名可沿用或写成 `vgpoint`）。刷新屏幕使用的点表。

没有 `--confirm`：**必须** `ERR code=need_confirm`，退出码 1。不要做成 dry-run 却返回成功，以免脚本误判。

候选文件不存在：`no_candidate`（`msg=missing`）。
候选文件存在但点数为 0：允许落盘，把已确认表写成空表，返回 `OK cmd=apply table=committed n=0`。这是「删光全部点再确认落盘」的合法结果。`test` 对空候选仍 `no_candidate`。

### 5.8 `abort`

```text
vgpoint abort
```

丢掉候选文件（或覆盖成已确认表的副本）。采集不受影响。没有候选也可 `OK`（幂等）。

### 5.9 确认步骤（9/20 口径）

确认在板端执行这条 `apply --confirm`。上位机脚本在 `test` 打出 `READ` 行之后**必须停下来等人**（PC 上回车，或人在 NSH 手打 `vgpoint apply --confirm`），再单独发 apply。

固件不得在 `test` 成功后自动 apply。本期不做屏幕长按确认。

---

## 6. 稳定应答

前缀一律 `vgpoint:`。键值用空格分隔，`key=value`，value 不含空格。空字段用 `-`。

脚本匹配：

- 成功收尾一行：`^vgpoint: OK `
- 失败收尾一行：`^vgpoint: ERR `
- 列表：`^vgpoint: POINT `
- 试读：`^vgpoint: READ `
- 当前值：`^vgpoint: VALUE `

每条命令**恰好一行** OK 或 ERR，放在其他 `POINT` / `READ` / `VALUE` 之后、`nsh>` 之前。

成功：

```text
vgpoint: OK cmd=<verb> table=candidate|committed n=<int>
```

`n` 对 `list` / `add` / `set` / `del` / `abort` / `apply` 是该命令作用后那张表的点数；对 `test` 是 `ok=1` 的点数；对 `get` 是输出的 VALUE 行数。`table`：改候选或只读候选时为 `candidate`；`list` 默认、`get` 和 `apply` 成功后为 `committed`。

失败：

```text
vgpoint: ERR cmd=<verb> code=<token> msg=<ascii>
```

`msg` 只用 `[A-Za-z0-9_.-]`，便于脚本切分。

列表行（每个点一行）：

```text
vgpoint: POINT id=temp name=温度 addr=1 fc=3 reg=0 qty=1 dtype=int16 scale=0.1 unit=C cmp=ge warn=40 crit=55 fail_n=3
```

试读行：

```text
vgpoint: READ id=temp raw=401 value=40.1 ok=1
```

`ok=0` 时 `raw=-`，`value=-`。

当前值行（与 `READ` 分开；`get` 不读从站）：

```text
vgpoint: VALUE id=temp value=40.1 ok=1 unit=C age_ms=210
vgpoint: VALUE id=flood value=- ok=0 unit=- age_ms=210
```

`ok=0` 时 `value=-`。`unit` 空则 `-`。`age_ms` 是整份快照的年龄（当前单调毫秒减写盘时的 `tick_ms`），同一条命令里各行相同。

允许在稳定行之前打印人读日志（例如 `vgpoint: test bus=/dev/rs485`）。脚本不要依赖这些行。

---

## 7. 错误码

| code | 何时 |
|------|------|
| `bad_arg` | 未知 verb、缺必填项、非法数字、非法 id/name/cmp/dtype |
| `too_long` | 命令超过 120 字节 |
| `full` | 候选已有 32 点还 `add` |
| `dup_id` | `add` 的 id 已在候选中 |
| `no_id` | `set` / `del` / 带 id 的 `test` / 带 id 的 `get` 找不到该点 |
| `no_candidate` | `test` 时候选为空或不存在；`apply` 时候选文件不存在 |
| `no_sample` | `get` 时已确认表非空，但采集快照文件尚不存在 |
| `need_confirm` | `apply` 未带 `--confirm` |
| `bus_busy` | 试读时扫描或另一路总线操作占用 RS485 |
| `test_fail` | `test` 时候选每个点都读失败 |
| `io` | 写 eMMC / 读文件 / `vg_config_commit` 失败 |
| `denied` | 预留：调用方被拒绝（见第 8 节；NSH 人工调用不走这条） |

---

## 8. Agent 禁令

Agent 的 `run_shell` 白名单保持只读：`vgmodbus`、`vgstats`、`vgcfg dump`。

禁止通过 Agent 调用：

- `vgpoint` 全部 verb
- `vgdiscover apply`
- `vgcfg commit`

实现阶段改 `app/velaguard/vg_agent_seed.c` 的 Skill 正文，并在 Agent 工具层挡住这些命令。规范先把口径定死：点表变更只能由调试串口上的人或上位机脚本发起。

---

## 9. 上位机脚本约定

1. 等到 `nsh>`。
2. 按文件逐条 `add` / `set`，每条等到 OK 再发下一条。
3. `vgpoint test`，把全部 `READ` 行打印给人看。
4. **停住**，等人确认。
5. 单独发送 `vgpoint apply --confirm`。
6. `vgpoint list`，核对已确认表。

不得把第 2 步和第 5 步合成一条命令。脚本可以在人按回车之后代发 apply，但中间必须有停顿。

参考现有 COM3 写法：`scripts/stage1_modbus_discovery_accept.ps1`（写一行、等到 `nsh>`）。批量点表脚本在实现阶段新增，不在本规范文件里附带。

演示触发（规范不规定从站仿真细节）：上位机把某点配成 `cmp=eq crit=1`，再让从站输出 1；或拔掉 RS485 触发离线。

---

## 10. 并发与总线

周期采集、总线扫描、`vgpoint test`、`vgdiscover test-read` 共用 `/dev/rs485`。任意时刻只允许一路占用。

`test` 期间采集线程必须跳过本轮（忙则 `usleep` 再试）。`add` / `set` / `list` / `get` / `abort` 不占用总线。

`apply` 写文件期间采集仍读内存里的旧已确认表，apply 成功后再切换指针或重新加载，避免读到半截 JSON。

---

## 11. 本次不做

- 二进制帧、CRC 封装、第二路 UART
- MQTT / 云端下发点表
- 上位机订阅实时值或告警推送
- 经 RS485 配置从站
- Windows 图形配置软件
- 在屏幕上编辑点表或长按确认
- 开机自动启动 Agent

---

## 12. 实现阶段才改、本规范不改的文件

- `scripts/configs/velaguard-lvgl.defconfig`：`CONFIG_NSH_LINELEN=128`，`CONFIG_NSH_MAXARGUMENTS=32`
- `app/velaguard/`：`vgpoint` 命令、点表字段、采集改读已确认表
- `app/velaguard/vg_agent_seed.c`：Skill 禁令
- 新增 PowerShell 批量脚本

实现前以本文为准；若命令或字段有变，先改本文再改代码。
