# 现有栈（2026-09-10）

对照协议 `docs/velaguard-host-nsh-protocol.md` 扫仓库后的实现锚点。

## 点表

- 写出：`app/velaguard/vg_point_table.c` `vg_point_table_write_candidate`，字段到 `unit` 为止。
- 读入：`vg_point_table_read_slaves` 只抽 `hits[]` 或 `"addr":`。没有完整点解析。
- 结构：`vg_discover.h` `vg_point_entry` 无阈值字段。上限 `VG_DISCOVER_MAX_POINTS` = 32。
- 路径：候选 `vgdiscover.c` 中 `point_table_candidate.json`；已确认 `points.json`。
- `vg_point_table_apply` 无 `--confirm` 时打印 dry-run 并 **返回 0**。`vgpoint apply` 不得学这一步。

## 采集与首页

- `app/velaguard/vg_ui_backend_board.c` `vg_hmi_acq_thread`：`vg_mthings_points[]` + `vg_discover_poll_holding`。
- `board_bus_busy()`：只看本进程扫描/落盘线程，对 NSH 不可见。
- `gui/main/ui/model/vg_model.c` 约 1445 行：首页舰队同样读组态数组；`vg_mthings_point_count` 现为 52。

## NSH

- `app/velaguard/Makefile`：`vgdiscover` 在 `CONFIG_VG_BUS_DISCOVER` 下注册，栈 8192。
- `scripts/configs/velaguard-lvgl.defconfig`：`CONFIG_NSH_LINELEN=64`。

## 上位机

- `scripts/stage1_modbus_discovery_accept.ps1`：`Send-Serial` 写一行、等到 `nsh>`，默认 COM3 115200 LF。

## Agent

- `app/velaguard/vg_agent_seed.c`：Skill 允许 `vgmodbus`、`vgstats`、`vgcfg dump`。尚无 `vgpoint` 禁令字样。工具层白名单若在 `packages/ai_agent`，实现时再对一下，不要只改 Skill 正文。

## Host 单测

- `app/velaguard/host_tests/test_discover.c` 覆盖候选写出与从站地址解析。新解析/稳定行应加测试，不要塞进无关的 net/config 用例。
