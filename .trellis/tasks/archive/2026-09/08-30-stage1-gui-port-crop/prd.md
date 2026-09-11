# 移植 velaguard_gui 并按手册裁剪 UI

> 父任务：`08-30-stage1-lvgl-hmi` · 手册 §6、`VelaGuard_推进方案.md` §6.1 / §9.2  
> **上游 UI 仓**：https://github.com/FoLeaf/velaguard_gui（分支 `release/v9.1`，LVGL 9.1 + PC 模拟器 480×272）

## Goal

将 `velaguard_gui` 中已实现的 VelaGuard HMI（`main/ui/`：app / shell / theme / model / pages / widgets）**迁入本 contest 仓**，并按项目手册 **裁剪为阶段 1 最简 HMI**：只保留 9/20 子集页面与交互，defer 或隐藏非目标功能；总线扫描 **UI 开关默认关闭**（手册 §5.1 / §6.6）。

本任务 **Phase A** 以 PC 模拟器可运行为 Gate；**Phase B（上板集成）** 见父任务 [`08-30-stage1-lvgl-hmi`](../08-30-stage1-lvgl-hmi/prd.md)。

## 源仓盘点（2026-08-30）

| 路径 | 内容 |
|------|------|
| `main/ui/app/` | `vg_app_init`、页面路由、演示快捷键 1–6 |
| `main/ui/shell/` | 状态栏、content host、返回栈、toast |
| `main/ui/theme/`、`fonts/` | 工业暗色主题 + CJK 子集字体 |
| `main/ui/model/` | mock 传感器 / 告警 / 网络 / 场景（**需改为读真实数据或薄 adapter**） |
| `main/ui/pages/` | 见下表 |
| `main/ui/widgets/` | chip、device_card、metric_row 等 |

### 源仓 pages 与手册 §6.2 对照

| 源文件 | 当前用途（README / 文件名） | 阶段 1 决策 |
|--------|------------------------------|-------------|
| `vg_page_home.c` | 首页总览 | **保留** — 手册 §6.3 |
| `vg_page_device.c` | 从站详情 | **保留** — §6.2 从站详情 |
| `vg_page_alarm.c` | 告警 | **保留并强化** — §6.4 AI 推测标注 |
| `vg_page_trend.c` | 实时趋势 | **Defer** — 9/20 子集外（推进方案 §9.2） |
| `vg_page_diagnosis.c` | 诊断 | **Defer / 合并** — 非 stage1 主路径 |
| `vg_page_add_sensor.c` | 添加传感器 | **改口径** → 总线探查页子流程或隐藏 |
| `vg_page_logs.c` | 日志 | **Defer** — 事件日志页非 9/20 必须 |
| `vg_page_ota.c` | OTA | **占位** — 保留入口，点击 toast「阶段 3 提供」（方案 A） |
| `vg_page_system.c` | 系统状态 | **Defer** — 9/20 子集外 |

### 手册要求但源仓缺失 / 需新建

| 页面 | 手册 | 动作 |
|------|------|------|
| 运行报告（日报/周报预览） | §6.5 | **新建** `vg_page_report.c` 或复用 shell + 读 `/data/agent/reports/` |
| 总线探查（扫描→点表→确认） | §6.6 | **新建/改造** — 对接 NSH `vgdiscover` 或 C API 封装；**扫描开关默认关** |
| 点表确认流 | §5.1 | 探查页内「测试读取 → 确认 / 放弃」 |

## Requirements

- R1 **迁入方式**：在 contest 仓建立可追踪的 UI 树（submodule / subtree / `app/velaguard/hmi/` 拷贝三选一，在 `design.md` 定案）；保留与源仓同步策略说明
- R2 **手册裁剪**：上表「保留 / Defer / 占位」落地 — 导航不出现趋势、完整诊断等 Defer 页；**OTA 保留入口**，点击仅 toast「阶段 3 提供」（与源仓 C2/C3 占位策略一致）
- R3 **扫描开关默认关**：总线探查页（或设置项）toggle 默认 **OFF**；仅 ON 时显示「开始扫描」并调用 discover 后端（`vgdiscover` 或等价 C API）
- R4 **只读 + 确认**：点表 apply 必须屏幕确认；无写寄存器、无 Agent 自动改配置入口（手册 §2.3）
- R5 **视觉规范**：状态色绿/黄/红/灰/蓝；「AI 推测」与确定性结论可区分（§6.1）
- R6 **Model 层**：PC 模拟器阶段可保留 mock，但须定义 **vg_ui_backend** 接口，便于 Phase B 接 `vg_stats` / `vgcfg` / agent 报告路径
- R7 **构建**：Windows llvm-mingw 模拟器构建文档入库（源自 `README_CN.md`）；WSL/Linux 路径可选

## Out of Scope（本任务）

- openvela LTDC 上板显示驱动（Phase B / 另任务）
- 波特率矩阵扫描 UI（阶段 1 固定 9600）
- 规则库归因 UI（阶段 2）
- 修改 `velaguard_gui` 上游仓结构（仅同步/移植到 contest）

## Acceptance Criteria

- [ ] **AC1** contest 仓内可构建并运行 PC 模拟器（480×272），默认进入裁剪后的首页
- [ ] **AC2** 导航暴露：首页、从站详情、告警详情、运行报告、总线探查；**OTA 可保留入口**，点击 toast「阶段 3 提供」；趋势/诊断等 Defer 页不注册或同等占位
- [ ] **AC3** 冷启动 / 进入探查页时 **扫描开关为关**，RS485 无自动 scan
- [ ] **AC4** 打开扫描开关 → 触发 discover 流程 UI（可先 mock 列表，再接真实 `vgdiscover`）
- [ ] **AC5** 告警页可见「AI 推测」样式占位或真实字段；报告页可展示最新日报路径/摘要（mock 或读盘）
- [ ] **AC6** `design.md` 记录：目录布局、裁剪清单、backend 接口、与 `08-30-stage1-modbus-discovery` 对接点

## Dependencies

| 任务 | 关系 |
|------|------|
| `08-30-stage1-modbus-discovery` | 探查后端（NSH / 后续 C API） |
| `08-30-stage1-agent-ops` | 日报路径、告警解释字段 |
| `08-30-stage1-data-layout` | `/data` 路径约定 |

## Notes

- 源仓 README 称 C2/C3 页曾为 toast「后续版本」，但 `pages/` 已有多文件 — 以 **源仓 HEAD + 手册** 为准做裁剪，不原样全收
- 父任务 `08-30-stage1-lvgl-hmi` PRD 的 R1–R5 仍有效；本任务是其 **移植与裁剪** 实施子任务
- 参考文档：`VelaGuard_项目手册.md` §6、`config/mthings/README.md`（mock 总线）
