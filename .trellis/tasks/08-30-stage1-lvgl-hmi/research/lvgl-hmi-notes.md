# stage1-lvgl-hmi 笔记

> 2026-08-30 · Phase B 启动

## 任务状态

- `task.py start 08-30-stage1-lvgl-hmi` ✓
- 子任务 Phase A：`08-30-stage1-gui-port-crop`（`gui/` 已迁入 + 裁剪）

## B1 spike（本轮）

| 项 | 路径 |
|----|------|
| NuttX HMI 入口 | `app/velaguard/vg_hmi.c` — LTDC 占位屏 |
| Kconfig | `CONFIG_VG_HMI` + stack/priority/input |
| defconfig | `nuttx/.../configs/velaguard-lvgl/defconfig`（含 `CONFIG_STM32H750B_DK_QSPI_BOOT=y`） |
| 构建 | `bash scripts/build.sh velaguard-lvgl [--clean]` |
| NSH | `vghmi` / `vghmi &` |
| 验收 | `scripts/stage1_lvgl_hmi_accept.ps1` |

**2026-08-30 B3 板测**：
- 全量 `gui/main/ui` 编入 `vghmi`；无 stack overflow
- 屏上应见裁剪后**首页**（状态栏 + 从站网格 + 底栏 详情/报告/探查/静音）
- 数据仍为 `vg_ui_backend.c` mock

## 预设关系

| TARGET | 用途 |
|--------|------|
| `lvgl` | 上游 widgets demo |
| `velaguard-lvgl` | 演示固件：`vghmi` + net/Agent/vgdiscover（一次烧录） |
| `net` | 日常无 LVGL |

## 2026-09-09 C1/C4

- 冷启动从 `points.json` 导入首页舰队（非 mock 24）
- `vghmi: home fleet n=32` + `/data/velaguard/hmi_fleet.txt`
- `stage1_lvgl_hmi_accept.ps1` **14/14 PASS**（复位抓 boot）

- [x] B1 板端：`vghmi` 点亮 480×272（串口 4/4；LCD 目视）
- [x] B2 同源编译 `gui/main/ui`（flash ~407KB；SRAM ~74%；mock backend）
- [x] B3 `vg_ui_backend_board.c` + discover/report 对接
- [x] B4.1 net 合并进 `velaguard-lvgl`（INIT=`velaguard_app_main`，`VG_HMI_AUTOSTART`）
- [x] B4.3 NSH 9/9 PASS（LCD 首页待目视）

## B4 合并（2026-09-01）

| 项 | 结果 |
|----|------|
| 构建 | `bash scripts/build.sh velaguard-lvgl --clean` PASS |
| 体积 | flash ~849KB QSPI；SRAM 485204 / 512KB（92.55%） |
| ELF | `vghmi_main` + `vgdiscover_main` + `velaguard_app_main` + `vg_bus_scan` |
| RAM | 板端 `VG_SENSOR_MAX=32`、`VG_HISTORY_LEN=16`；`BOARD_SDRAM2_HEAP_OFFSET=1MB`（避开 LTDC FB） |
| 预设 | `net` 无 LVGL；演示用 `velaguard-lvgl` |
| 板测 | `flash.ps1` PASS（3.27V）；`stage1_lvgl_hmi_accept.ps1` **9/9 PASS** |

HMI 与 `ai_agent` 同时 autostart 会 `Assertion failed` 拖死整机；HMI 固件跳过 agent autostart（NSH 仍有 `ai_agent`）。

Makefile：net+HMI 同时开时不重复链 `vg_discover_modbus`/`nanomodbus`；`STACKSIZE` 按 PROGNAME 对齐（仅 `vghmi` 49K）。
