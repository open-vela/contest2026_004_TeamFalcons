# gui-port-crop 实施笔记

> 2026-08-30 · Phase A 代码迁入 + 手册裁剪

## 迁入

- 源：`FoLeaf/velaguard_gui` @ `release/v9.1`
- 目标：`contest2026_004_TeamFalcons/gui/`
- LVGL：`lvgl/` v9.1.0（`gui/scripts/setup_gui.sh`）

## 裁剪落地

| 项 | 文件 |
|----|------|
| Defer toast | `vg_shell.c` `nav_allowed()` |
| OTA toast「阶段 3 提供」 | 同上 |
| 首页底栏 详情/报告/探查/静音 | `vg_page_home.c` |
| 运行报告 mock | `vg_page_report.c` |
| 总线探查 + 扫描开关默认关 | `vg_page_discover.c` |
| 告警 AI 推测区块 | `vg_page_alarm.c` |
| backend 占位 | `model/vg_ui_backend.c` |

## 构建

- WSL：需 `sudo apt install libsdl2-dev` 后 `cmake -S gui -B gui/build && cmake --build gui/build`
- Windows：`gui/README_CN.md` llvm-mingw 路径（本环境未跑）

## 待办

- [ ] Windows / WSL 本地 build + 跑 `./gui/bin/main` 验 AC1–AC4（Phase A 出口）
- [ ] Phase B → 父任务 `08-30-stage1-lvgl-hmi`（上板 + 真实 backend）
