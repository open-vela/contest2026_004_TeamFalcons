# 最简 LVGL HMI（Phase B · 上板集成）

> 手册 §6、`VelaGuard_推进方案.md` §6.1 / §9.2  
> **Phase A（PC 模拟器 + 裁剪）**：子任务 [`08-30-stage1-gui-port-crop`](../08-30-stage1-gui-port-crop/prd.md) — `gui/` 目录  
> **本任务（Phase B）**：openvela 上 LTDC 触控 + 真实数据后端 + 板端验收

## Goal

在 STM32H750B-DK + 扩展板 LTDC 触控屏上运行 **裁剪后的 VelaGuard HMI**，读取本地已采集/已落盘数据（非 mock），总线探查经 UI 开关（默认关）对接 `vgdiscover` 能力，满足 9/20 子集「最简 LVGL」与手册 §14.1 相关 HMI 项。

## 任务关系

```text
08-30-stage1-lvgl-hmi          ← 本任务（Phase B 出口 / 板端 Gate）
  └─ 08-30-stage1-gui-port-crop   Phase A：gui/ PC 模拟器 + 手册裁剪 UI
```

| 阶段 | 目录 / 产物 | Gate |
|------|-------------|------|
| **A** | `gui/` + `main.exe` | 模拟器可跑；导航/扫描开关/OTA 占位符合手册 |
| **B** | `app/velaguard/hmi/` + defconfig | `build.sh` 编过；板端 HMI 可交互；AC 板测 |

**原则**：UI 源码以 `gui/main/ui/` 为 **单一来源**；Phase B 将其编进 NuttX 应用，不 fork 两套页面逻辑。

## Requirements

### 来自 Phase A（继承，不在本任务重复实现）

- R-A1 页面集：首页、从站详情、告警（AI 推测）、运行报告、总线探查
- R-A2 Defer toast：趋势/诊断/日志/系统/添加 →「阶段 2 提供」；OTA →「阶段 3 提供」
- R-A3 扫描开关 **默认关**（手册 §5.1 / §6.6）

### Phase B 新增

- R1 **构建集成**：新增或扩展 defconfig（如 `velaguard-lvgl` 或 `net+lvgl` 合并策略见 design）；`scripts/build.sh` 可构建 HMI 固件
- R2 **显示与输入**：LTDC + 板载/扩展板触控；480×272（或与 `vg_display.h` 一致）；不依赖 SDL
- R3 **vg_ui_backend 实装**：替换 PC mock，读：
  - 从站/采样：`vg_stats` / 采集缓存 / `points.json`
  - 告警：规则引擎或 agent 告警上下文（只读）
  - 报告：`/data/agent/reports/` 最新日报
  - 网络状态：`vgnet` / `network_manager` 摘要
- R4 **总线探查对接**：discover 页在开关 ON 时调用 **`vg_bus_scan` / `vg_reg_probe` 等 C API**（或受控 NSH 封装），非 mock 列表；apply 须屏幕 confirm
- R5 **与 net 预设共存**：明确 HMI 与 `velaguard-net`（MQTT/Agent/采集）的 defconfig 合并路径；避免 SDRAM/FB 与网络栈资源冲突（见 bringup 文档）
- R6 **只读 + 确认**：与 Phase A 相同；HMI 不写寄存器、不 bypass 点表 confirm

## Out of Scope（本任务）

- PC 模拟器首次迁入与裁剪（Phase A / 子任务）
- OTA 真实流程（阶段 3）
- 趋势/诊断/事件日志完整页（阶段 2 或更后）
- Agent 自然语言输入 UI（CLI 已有；HMI 查数为可选增强）

## Acceptance Criteria

- [x] **AC1** Phase A 子任务 Gate 通过（或本任务启动前 `gui/` 已具备裁剪后 UI）
- [x] **AC2** `bash scripts/build.sh <lvgl-target>` 成功；固件含 HMI，体积在板子资源预算内
- [x] **AC3** 板端冷启动进入 HMI：**无自动 RS485 scan**（开关默认关）
- [x] **AC4** 首页显示 **真实** 从站/告警摘要（非纯 mock）
- [x] **AC5** 告警页展示 AI 解释字段（联网时有内容；断网降级符合 §2.2）
- [x] **AC6** 报告页可读最新日报文件（或明确 empty 态）
- [x] **AC7** 探查：开关 ON → scan @9600 → ≥1 从站（MThings mock）；confirm 后 `vgcfg dump` 可读
- [x] **AC8** 验收脚本或 `stage1_*_accept` 笔记落盘

## Dependencies

| 任务 | 关系 |
|------|------|
| `08-30-stage1-gui-port-crop` | Phase A UI 源码与裁剪 (**blocking**) |
| `08-30-stage1-modbus-discovery` | discover C API / NSH |
| `08-30-stage1-agent-ops` | 日报路径、告警解释 |
| `08-30-stage1-data-layout` | `/data` 布局 |
| `08-30-stage1-ai-agent` | Agent 告警解释内容 |

## Notes

- 参考 `docs/velaguard-bringup-known-issues.md`：`net` 与 `lvgl` 预设分离历史；合并需 design 决策
- 上游 UI 仓仍可在 `FoLeaf/velaguard_gui` 迭代；同步路径：`main/ui/` → contest `gui/main/ui/` → 本任务编译单元
- **Phase A 子任务** `08-30-stage1-gui-port-crop` 完成 PC Gate 后再 `task.py start` 本任务
