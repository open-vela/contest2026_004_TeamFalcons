# 板端与上位机 vgpoint 通信

所属父任务：`09-09-pre-920-score-play`。

命令、稳定应答、错误码、点字段以 [`docs/velaguard-host-nsh-protocol.md`](../../../docs/velaguard-host-nsh-protocol.md) 为准。本文不另开命令表。

原先待办 `09-09-nsh-vgpoint-host-editor` 不单独开工；告警比较、告警页、`pending_alarm.txt` 仍由 `09-09-demo-threshold-alarm` 负责。本任务只把点表从上位机写到板端，并让采集/首页改读这张已确认表。

## 目标

操作者在 PC 上通过 ST-LINK 虚拟串口，按文件把从站、寄存器、倍率、比较方式和阈值写到开发板。板端先改候选表、试读，等人确认后再写入 eMMC 上的已确认表。确认之前，正在采集用的规则不变。Agent 不能改点表。

## 已核对的事实

- 固件还没有 `vgpoint`。已有 `vgdiscover` 候选路径 `/data/velaguard/discover/point_table_candidate.json`，以及已确认路径 `/data/velaguard/config/points.json`。
- 现有 `points.json` 写出不含 `cmp` / `warn` / `crit` / `fail_n`。读侧目前只解析从站地址。
- 周期采集和首页舰队仍读编译进镜像的 `vg_mthings_points[]`（`vg_ui_backend_board.c`、`gui/main/ui/model/vg_model.c`）。
- 作品主线 `CONFIG_NSH_LINELEN=64`。规范要求改为 128，命令体上限 120 字节。
- 总线忙标志 `board_bus_busy()` 只在 HMI 进程内有效。`vgpoint` 是独立 NSH 程序，试读必须和扫描/采集跨进程互斥。
- 现有 COM3 写法：`scripts/stage1_modbus_discovery_accept.ps1`（写一行、等到 `nsh>`）。
- Agent Skill 正文允许 `vgmodbus` / `vgstats` / `vgcfg dump`。规范禁止 Agent 调用 `vgpoint`、`vgdiscover apply`、`vgcfg commit`。

## 要做到什么

1. NSH 实现规范全部 verb：`list`、`add`、`set`、`del`、`test`、`apply --confirm`、`abort`。稳定行与错误码与规范第 6、7 节一致。
2. `add` / `set` / `del` 只改候选。第一次改候选时：候选不存在则复制已确认表；已确认也没有则从空表开始。
3. 点记录扩展现有 `points.json`（`schema_version` 仍为 1）。新增字段可缺省。主键 `tag` 大小写敏感，上限 32 点。
4. `test` 对候选做保持寄存器读；占用总线忙标志，采集让路。总线已被扫描或另一路占用则 `bus_busy`。不得在 `test` 成功后自动 `apply`。
5. 没有 `--confirm` 的 `apply` 必须 `ERR code=need_confirm`，退出码 1。成功后调用现有双槽 `vg_config_commit`，把候选覆盖到已确认路径，并刷新内存中采集/首页使用的点表。
6. 冷启动等 `/data` 挂上后只加载已确认表。文件缺失或损坏则首页为空，不回退 `vg_mthings_points[]`。
7. 作品主线 defconfig：`CONFIG_NSH_LINELEN=128`。超长命令 `too_long`，不得截断执行。
8. 上位机 PowerShell：按 JSON 点文件逐条 `add`/`set`，每条等到 OK；再 `test`；停住等人回车；单独发 `apply --confirm`；最后 `list` 核对已确认表。默认串口 COM3、115200。
9. 改 Agent Skill 正文，并在工具层挡住 `vgpoint`、`vgdiscover apply`、`vgcfg commit`。

## 本次不做

- 告警比较、告警页、`pending_alarm.txt`（`09-09-demo-threshold-alarm`）
- Windows 图形配置软件、在屏幕上编辑点表、屏上长按确认
- 二进制帧、第二路 UART、MQTT / 云端下发点表
- 经 RS485 配置从站、上位机订阅实时曲线或告警
- 开机自动启动 Agent

## 怎样算完成

- [x] AC1 `nsh>` 下 `vgpoint` 出现在帮助中；规范列出的 verb 均可调用，稳定行可被脚本匹配
- [x] AC2 `add`/`set`/`del` 只改候选文件；`vgpoint list`（默认）与采集仍对应当前已确认表
- [x] AC3 未带 `--confirm` 的 `apply` 含 `need_confirm`（板测已过）；`--confirm` 落盘与内存表切换已实现，无人值守未自动执行以免改已确认表
- [x] AC4 上位机脚本在 `test` 的 `READ` 行之后停住等人，再单独发送 `apply --confirm`；不得把写入与确认合成一行
- [ ] AC5 擦掉或没有已确认表时首页为空，采集不再使用 `vg_mthings_points[]`（代码已切源；未做擦表后的屏上看空）
- [x] AC6 Host 单测覆盖点表读写（含缺省阈值字段）、稳定行格式、`need_confirm`；`make -C app/velaguard/host_tests test` 通过
- [x] AC7 `bash scripts/build.sh` 编译作品主线通过
- [x] AC8 COM3 脚本走完 add → test → apply --confirm → list（本次 `test` 为 `bus_busy`，命令格式符合规范）

## 关键决定

| 项 | 决定 |
| --- | --- |
| 协议正文 | 只引用 `docs/velaguard-host-nsh-protocol.md`，任务文档不抄命令表 |
| 上位机输入 | JSON 点列表，字段与规范第 4 节一致；脚本翻译成 `vgpoint add`/`set` |
| 采集切换 | 本任务内完成：确认表成为采集和首页的唯一来源 |
| 告警 | 本任务只落盘 `cmp`/`warn`/`crit`/`fail_n`，不比较、不上告警页 |
| 总线互斥 | 跨进程锁文件（NSH `vgpoint`/`vgdiscover` 与 HMI 采集不是同一进程） |
| 旧待办 | `09-09-nsh-vgpoint-host-editor` 不再开工 |
