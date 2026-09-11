# 点表主键改为 id+name

所属父任务：`09-09-pre-920-score-play`。衔接 `09-10-host-nsh-vgpoint`（已按单一 `tag` 实现）。命令与稳定应答以 [`docs/velaguard-host-nsh-protocol.md`](../../../docs/velaguard-host-nsh-protocol.md) 为唯一口径；本文件不另开命令表。

## 目标

点表记录同时有点位 ID 和点位名。协议、固件、脚本只按 ID 增删改查；屏幕和告警文案显示名称。名称可重复、可含中文。不再用 ASCII `tag` 同时充当主键和显示名。

## 背景

现行规范与实现把身份收成一个 `tag`：`[A-Za-z0-9_]{1,23}`、表内唯一，因此不能中文。查找函数是 `vg_point_table_find_tag()`，命令是 `add -t` / `set <tag>`，错误码 `dup_tag` / `no_tag`。

HMI 已经拆开：`vg_sensor_t.id[24]` 与 `name[48]`。`vg_model_import_runtime_points()` 目前把 `p->tag` 拷进两栏。告警比较已按 `cmp`/`warn`/`crit`，禁止按中文点名匹配。

稳定应答 `key=value` 且 value 不含空格；命令体上限 120 字节；`fail_n` 已占用 `-n`。板上点表已清空，本任务不读、不转写旧 `tag`。

## 需求

R1. JSON 点记录字段为 `id`（主键）和 `name`（显示名）。`schema_version` 仍为 1。写出不再含 `tag`。缺少 `id`、或 `id` 非法的点丢弃；整表因此可能变成 0 点（首页空）。不把 `tag` 当作 `id`。

R2. `id`：`[A-Za-z0-9_]{1,23}`，大小写敏感，同一张表内唯一。`name`：可重复；UTF-8；1..47 字节（与 `VG_SENSOR_NAME_MAX-1` 对齐）；禁止 ASCII 空白、`=`、`"`、`\` 和控制字符。缺 `name` 或 `add` 未给 `-N` 时，存储为与 `id` 相同。

R3. `add` 用 `-i <id>`；`set` / `del` / `test <id>` 的位置参数是 ID。改名用 `set <id> -N <name>`。ID 冲突 `dup_id`，找不到 `no_id`。删除 `dup_tag` / `no_tag`。显示名不参与去重或查找。

R4. `POINT` 行含 `id=` 与 `name=`；`READ` 行只含 `id=`（试读脚本不依赖显示名）。

R5. 加载已确认表后，HMI `vg_sensor_t.id` 来自点 `id`，`name` 来自点 `name`。告警关联继续用 `id`。

R6. `vgdiscover` 推断点：ASCII `id` 沿用 `s{addr}_r{reg}_{i}`，`name` 初值等于 `id`。上位机再 `set -N` 改中文名。

R7. 上位机 demo JSON 与 `vgpoint_host_apply.ps1` 改用 `id`/`name`。串口按 UTF-8 发送。若单条 `add` 加上 `name` 会超过 120 字节，先 `add` 再单独 `set -N`。

R8. Host 单测覆盖：同名不同 ID 可共存；按 `name` 不能当查找键；仅有 `tag` 无 `id` 的 JSON 点数为 0；合法中文 `name` 可 JSON 往返；稳定行是 `id=` 不是 `tag=`。

## 验收标准

- [x] AC1 规范第 4/5/6/7 节改为 `id`+`name`；命令与 `POINT`/`READ` 按 ID；错误码 `dup_id`/`no_id`；文中不再把 `tag` 当主键
- [x] AC2 两个点 `id=t1`/`id=t2` 且 `name` 同为中文时可写入候选；`set`/`del`/`test` 只认对应 `id`
- [ ] AC3 已确认表加载后首页传感器 `id`/`name` 与点表一致；缺 `name` 时屏幕显示等于 `id`（代码已分栏；未做板端灌表目视）
- [x] AC4 读入只有 `tag`、没有 `id` 的 JSON 得到 0 个点，不把 `tag` 升成 `id`
- [x] AC5 `make -C app/velaguard/host_tests test` 通过
- [x] AC6 `bash scripts/build.sh` 作品主线通过
- [x] AC7 demo JSON 与 `vgpoint_host_apply.ps1` 使用 `id`/`name`；`dup_id` 时改走 `set <id>`

## 范围外

- 告警比较、`pending_alarm.txt`、报告页（`09-09-demo-threshold-alarm`）
- 屏上编辑点表、Windows 图形组态、把 `tag` 迁成 `id` 的兼容层
- 二进制帧、MQTT 下发、提高 `CONFIG_NSH_LINELEN` 超过 128
- 用点位名做告警匹配或主键

## 关键决定

| 项 | 决定 |
| --- | --- |
| 身份字段 | 只有 `id` + `name`。`tag` 是被替换的旧字段，不是第三套 |
| ID | 短 ASCII `[A-Za-z0-9_]{1,23}` |
| 名称 | 可重复、可中文；NSH value 不含空格，故名称也不含空格 |
| 索引 | 一律按 `id` |
| 旧表 | 不兼容。缺少 `id` 的点丢弃。板端表已清空 |
| 命令开关 | `-i` 为 ID；`-N` 为名称（`-n` 仍是 `fail_n`，`-t` 不再使用） |
