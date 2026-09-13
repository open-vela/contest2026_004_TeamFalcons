# 执行计划：点表数据趋势页恢复与增强

按块执行，每块完成即跑质量检查并可独立提交/回退。

## 块 1：历史深度

- [x] `gui/main/ui/model/vg_model.h`：板上 `VG_HISTORY_LEN` 16 → 128，重写注释（BSS 增量 ≈28 KB、基线来源、回退路径 64）。
- [x] 验证：板端 `bash scripts/build.sh` 成功；`arm-none-eabi-size nuttx` 前后对比 BSS 增量 ≈ +28 KB 量级、无溢出告警。
- 回滚点：恢复 16。

## 块 2：趋势页增强

- [x] 下拉选择器：`vg_model_get_sensors()` 构建 id 映射，选中回调 `vg_model_set_selected_sensor(id)` + `refresh_trend(NULL)`；创建时按 `vg_model_get_selected_sensor_id()` 初始化；name 重复时以 id 尾缀消歧。
- [x] 窗口按钮：文案改最近60点 / 全部；meta 行改当前点数。
- [x] Y 轴量程：无阈值点位按窗口内 min/max 自适应（10% 边距，零跨度保护）。
- [x] 保持 4 series 顺序与 chart_draw_task_cb 不变；离线显示 -- 不变。
- [x] 验证：PC 模拟器构建运行，按键切场景覆盖 正常/预警/严重/离线；检查 AC2/AC3/AC4/AC5/AC7。
- 回滚点：整块 revert。

## 块 3：解锁导航

- [x] `gui/main/ui/shell/vg_shell.c` `nav_allowed()`：仅移除 `case VG_PAGE_TREND:`；其余门禁不动。
- [x] 验证：模拟器中首页趋势快捷方式与设备页按钮均可进入（AC1）。
- 回滚点：恢复 case。

## 总验证

- [x] PC 模拟器：`cmake --build gui/build` 后运行，逐项核对 AC1-AC5、AC7。
- [x] 板端：`bash scripts/build.sh`；烧录按 velaguard-board-inner-loop 技能自验收（AC6，含真实点表下拉与双窗口）。
- [x] trellis-check：spec 合规、Scope Discipline（改动文件与本任务 AC 对应）、跨层数据流核对。
- [x] host_tests 非必需（无点表协议变更），可选回归 `make -C app/velaguard/host_tests test`。

## 提交与收尾（针对混合工作区的特殊约束）

- 工作区含 09-13-field-test-fixes-0913 与 09-13-alarm-page-multi-rows 在途改动，本任务提交只暂存：`gui/main/ui/model/vg_model.h`（仅本任务 hunk）、`gui/main/ui/pages/vg_page_trend.c`、`gui/main/ui/shell/vg_shell.c`（仅 nav_allowed hunk）、`.trellis/tasks/09-13-trend-page-live/`。
- 共享文件用 `git add -p` 精确暂存本任务 hunk，提交前 `git diff --cached` 复核。
- 归档用 `python3 ./.trellis/scripts/task.py archive 09-13-trend-page-live --no-commit`，随后手工仅提交任务目录的归档移动，避免扫入他人 WIP。

## 结果备注（2026-09-13）

- 板端构建 BSS 实测 +28,672 字节（s_sensors 0x4e00 到 0xbe00），sram 总占用 216,900 / 512 KB（41.4%）。
- QSPI 烧录 + stage1_lvgl_hmi_accept：pass=15 fail=3，失败项均为环境项（52 点预期模式与当前 14 点已提交表不符、无 USB-RS485 模拟从站），与本任务无关；NSH free 显示堆健康。
- LCD 上趋势页的最终目视确认留待人工（验收脚本 NOTE 同样要求目视）。
- 提交调整：nav 解锁 hunk 随提交 a274607 入库（补丁分块顺序所致），未单独成块。
- 无头验证 gui/headless/trend_check_main.c 全部 PASS（含告警页回归 ALL PASS）。
