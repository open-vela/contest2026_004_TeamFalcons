# vgpoint 怎么接到现有点表和串口上

命令与应答仍以 [`docs/velaguard-host-nsh-protocol.md`](../../../docs/velaguard-host-nsh-protocol.md) 为准。

## 数据怎么走

```
PC JSON 点文件
  → PowerShell 逐条 vgpoint add/set（等 nsh>）
  → 候选 JSON
  → vgpoint test（占总线，READ 行）
  → 人回车
  → vgpoint apply --confirm
  → 已确认 points.json + vg_config_commit
  → 内存已确认表切换（采集/首页）
```

两张表路径与现有 `vgdiscover` 相同。不另做第三份 JSON 格式。`schema_version` 保持 1。空阈值省略该键，不写 `null`。

冷启动：`/data` 就绪后只加载已确认表到内存。缺失或损坏则点数为 0。`apply` 写文件期间采集继续读旧内存表，成功后再换指针或整表替换，避免读到半截 JSON。

## 模块边界

| 模块 | 职责 |
| --- | --- |
| `vg_discover.h` 的 `vg_point_entry` | 增加 `cmp`、`warn`/`crit`（带是否存在标志）、`fail_n`。旧扫描推断路径把这些字段留空 |
| `vg_point_table.c` | 候选/已确认读写、第一次编辑时复制已确认、按 tag 增改删、写出时省略空阈值 |
| 新 `vgpoint.c` | NSH 入口与 verb 分发、稳定行打印、120 字节超长检查 |
| 总线占用 | 跨进程锁（建议 `/data/velaguard/discover/bus.lock` + `flock`）。HMI 采集、扫描/落盘、`vgpoint test`、`vgdiscover test-read` 共用。`list`/`add`/`set`/`del`/`abort` 不占锁 |
| `vg_ui_backend_board.c` / `vg_model.c` | 采集和首页改读内存已确认表；`board_bus_busy()` 改为看这把锁 |
| `vg_agent_seed.c` + Agent `run_shell` 白名单 | Skill 写明禁令；工具层拒绝 `vgpoint`、`vgdiscover apply`、`vgcfg commit` |
| `scripts/configs/velaguard-lvgl.defconfig` | `CONFIG_NSH_LINELEN=128` |
| 新 `scripts/vgpoint_host_apply.ps1`（名称实现时可定为 `vgpoint_*.ps1`） | COM3 批量写入；`-PointsFile`；`test` 后 `Read-Host` 才发 apply |

`vgpoint apply` 的 `vg_config_commit` 设备名可用 `vgpoint`。无 `--confirm` 不得走 `vgdiscover apply` 那种退出码 0 的 dry-run。

## 与扫描的关系

`vgdiscover dump` / `apply` 仍写同一套候选和已确认文件。旧表没有阈值字段时，读入视为 `cmp`/`warn`/`crit` 为空：采集可用，本任务不做模拟量告警。上位机随后 `vgpoint set` 补阈值。

## 上位机脚本

输入文件：JSON，顶层与规范示例相同，或仅含 `points` 数组。脚本把每个点变成一条 `vgpoint add`（tag 已存在则改为 `set`）。

流程固定为规范第 9 节。禁止 `-AutoConfirm` 一类默认跳过人确认的开关。COM 口默认 `COM3`，可参数覆盖。

## 兼容与回滚

- 不改协议正文除非命令或字段真的变了（先改文档再改代码）。
- 不引入 cJSON；继续用现有字符串扫描写出，扩展解析到完整点记录。
- 编译期 `vg_mthings_points[]` 可留在镜像里供对照，运行时不得再作为采集源。
- 若本任务回滚：删掉 `vgpoint` 程序与脚本后，扫描落盘路径仍可用；首页会重新空或需临时恢复组态数组（回滚清单见 `implement.md`）。

## 风险

- NSH 行长与 120 字节检查必须在分词执行前完成，避免截断后误 `add`。
- HMI 与 NSH 不是同一进程，只改 `vg_ui_backend_board.c` 里的静态标志不够。
- `apply` 与采集并发：必须先写完文件再切换内存表。
