# vgpoint 动手顺序

依赖：协议 [`docs/velaguard-host-nsh-protocol.md`](../../../docs/velaguard-host-nsh-protocol.md)。本任务不实现告警比较。

## 清单

- [x] 扩展 `vg_point_entry` 与 `vg_point_table` 读写：`cmp`/`warn`/`crit`/`fail_n`；空键省略；缺字段当缺省
- [x] 候选第一次编辑：复制已确认，否则空表；tag 去重；满 32 点 `full`
- [x] 跨进程总线锁；HMI 采集忙则 `usleep` 跳过本轮；`vgpoint test` 抢不到则 `bus_busy`
- [x] 新 NSH `vgpoint.c` + `Makefile`/`Kconfig`/`CMakeLists.txt`：list/add/set/del/test/apply/abort 与稳定行
- [x] `CONFIG_NSH_LINELEN=128`；`CONFIG_NSH_MAXARGUMENTS=32`；命令体 >120 字节 `too_long`
- [x] `apply --confirm`：写已确认路径 + `vg_config_commit` + 切换内存表；无 `--confirm` → `need_confirm` 退出 1
- [x] 采集线程和首页改为只读内存已确认表；无表则首页空；去掉运行时对 `vg_mthings_points[]` 的依赖
- [x] Agent Skill 正文 + `run_shell` 白名单挡住 `vgpoint` / `vgdiscover apply` / `vgcfg commit`
- [x] Host 单测：JSON 往返、稳定行、`need_confirm`、空阈值省略；接到 `app/velaguard/host_tests`
- [x] 新增 PowerShell：按 JSON 逐条写入，`test` 后 `Read-Host`，再 `apply --confirm`，最后 `list`
- [x] 示例点文件（规范第 4 节 temp/flood 一类，不写进固件）
- [x] `bash scripts/build.sh`；COM3 脚本验收（从站没有时仍校验命令格式）
- [x] 把 `09-09-nsh-vgpoint-host-editor` 标成不再开工；告警任务改为消费本任务写出的已确认表

## 校验

```bash
make -C app/velaguard/host_tests test
bash scripts/build.sh
```

板端（Windows COM3 可用时）：

```powershell
powershell.exe -ExecutionPolicy Bypass -File scripts/flash.ps1
powershell.exe -ExecutionPolicy Bypass -File scripts/vgpoint_host_apply.ps1
```

（脚本最终文件名以实现为准，须能 `-PointsFile` 且 `test` 后停住。）

## 风险文件

- `app/velaguard/vg_point_table.c`、`vg_discover.h`：扫描写出必须仍能被新解析器读入
- `app/velaguard/vg_ui_backend_board.c`、`gui/main/ui/model/vg_model.c`：首页与采集切源
- `scripts/configs/velaguard-lvgl.defconfig`：行长 128、参数个数 32
- Agent 白名单：改错会导致 `vgcfg dump` 也不能跑

## 剩余（不扩功能）

编码清单已勾完。本任务不要再开新需求。剩下两件事放到 `09-09-demo-threshold-alarm` 板测时顺带做：

1. 完整 `vgpoint apply --confirm`（脚本在 `test` 后 `Read-Host`，需人回车）
2. 擦掉已确认表后目视首页为空（PRD AC5）

下一步产品工作是告警上屏，不是继续改 `vgpoint`。

## 回滚

1. 去掉 `vgpoint` 的 `PROGNAME`/`MAINSRC`
2. 采集改回切源前的已确认加载（或临时恢复组态数组，仅回滚用）
3. 恢复 `CONFIG_NSH_LINELEN=64`（仅当尚未依赖 128 行长的其他命令）
