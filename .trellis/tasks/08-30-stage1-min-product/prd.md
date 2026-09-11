# 阶段 1：最小完整作品（父任务）

> 依据 `VelaGuard_推进方案.md` §6、§9.2；手册 §14。  
> **前置**：阶段 0 已验收（2026-08-30）；ai_agent 探针 AC1–4 已通过。  
> **规划刷新**：2026-09-01（Q1=B：HMI+Agent 同机为尽力项）。  
> **2026-09-10**：作品只留一套固件 `velaguard-lvgl`。告警与 Agent 试验见 `09-09-pre-920-score-play`，不再用 `velaguard-net` 作演示或拍摄退路。

## Goal

交付「可以就此停手也说得过去的完整作品」：运营助手 Agent、总线自动探查、最简 LVGL HMI，满足大赛 C9 主动+执行与手册 §14.1 / §14.2。

## Confirmed facts（仓库 + 板测，2026-09-01）

| 能力 | 证据 | 状态 |
|------|------|------|
| RJ45 + MQTT + vgmqtt | 归档 `08-14-stage1-net-mqtt-min-loop` | 完成 |
| eMMC / RS485 / 帧统计 / 掉电配置 | 归档 `08-29-stage0-*` | 完成 |
| 运营助手文档口径 v3 | 手册 / 推进方案 / README / CONTEXT | 文档完成，待归档 `08-25-agent-capability-rescope` |
| `/data` + net 预设 | 归档 `08-30-stage1-data-layout` | 完成 |
| ai_agent + eMMC `/data/agent` | 归档 `08-30-stage1-ai-agent` | 完成 |
| Skill + **定时日报** 板测 | 归档 `08-30-stage1-agent-ops` AC5：`daily-20260228.md` 1796B | 完成 |
| 告警解释代码 | agent-ops AC4 代码就绪 | 板测可选，不阻塞出口 |
| Modbus 扫描/探测/点表/`apply` | `vgdiscover` 板测 **32/32** @9600 | NSH 完成，任务未归档 |
| HMI 冷启动 480×272 | `velaguard-lvgl`；`stage1_lvgl_hmi_accept.ps1` **9/9** | 点亮完成；AC4–AC7 未板测 |
| 演示预设 | `bash scripts/build.sh`（`velaguard-lvgl`） | 2026-09-10 起唯一作品镜像 |
| HMI + `ai_agent` 同 autostart | 2026-09-01 assert 拖死整机；HMI 固件 skip agent autostart | 同机不作为出口硬门槛（Q1=B） |
| CLI `vela> ask` | 日报链路已用过 | 「查从站实时读数」专项板测未做 |

## Task Map

不新开子任务。剩余工作落在已有 `lvgl-hmi`，以及 `net` 固件上的 CLI（和可选告警）板测。

| 子任务 | 状态 | 还做什么 |
|--------|------|----------|
| `08-25-agent-capability-rescope` | 文档已落地 | 归档 |
| `08-30-stage1-data-layout` / `ai-agent` / `agent-ops` | 已归档 | agent-ops 告警板测可选 |
| `08-30-stage1-modbus-discovery` | NSH AC 全绿 | 归档；屏上 confirm 交给 lvgl-hmi |
| `08-30-stage1-lvgl-hmi` | in_progress | **主战场**：AC4–AC7 |
| `08-30-stage1-gui-port-crop` | UI 已进 `gui/` | PC Gate 不阻塞板端 |

## Decisions

- **Q1=B（2026-09-01）**：阶段 1 出口不要求开机同时自动启动界面和 Agent。  
- **2026-09-10**：只烧 `velaguard-lvgl`。同机 Agent 改由 `09-09-samefw-agent-spike` 限时试验，失败也不改烧无屏镜像。

## Remaining requirements（出口必须）

- R-rem1 首页从站/告警非纯 mock（discover 结果或 `points.json` / 采集缓存）
- R-rem2 报告页可读 `/data/agent/reports/daily-*.md` 或明确 empty 态
- R-rem3 扫描开关默认关；ON → scan @9600 → ≥1 从站 → 屏上 confirm → `vgcfg dump` 可读
- R-rem4 告警页有「AI 推测」字段或断网降级文案（手册 §2.2）
- R-rem5（尽力）HMI 存活时 `ai_agent &` 不 panic；失败不阻塞出口
- R-rem6 `vela> ask` 问从站读数，日志含工具调用与数值（在 `net` 上验）
- R-rem7 对照手册 §14.1；§14.2 日报证据沿用已有板测；告警比例若未板测则笔记标明跳过；写操作 = 0
- R-rem8 **演示 MVP**：点表阈值驱动本地告警；水浸=1 或拔从站 → HMI 告警页 + `pending_alarm.txt`。2026-09-10 起改由上位机写入点表；执行权在 `09-09-demo-threshold-alarm`，本父任务不再平行开工。

## Out of Scope

- 阶段 2 规则库、阶段 3 OTA、周报、Bridge
- 双栈 ESP 故障切换再打磨
- PC 模拟器 Windows 路径（不阻塞板端）
- 趋势/诊断/日志/系统完整页
- 单固件 HMI+Agent autostart 作为出口条件
- Windows 配置 GUI、在屏幕上编辑点表 → 9/20 后
- NSH `vgpoint` / COM3 上位机加点 → 已由 `09-10-host-nsh-vgpoint` 编码落地；告警比较仍在 `09-09-demo-threshold-alarm`

## Acceptance Criteria

- [x] 9/20：ai_agent + `/data/agent`
- [x] 9/20：两个运营 Skill
- [x] 9/20：定时日报板测
- [x] 9/20：Modbus NSH 扫描 + 点表（32/32）
- [x] 9/20：最简 LVGL（AC4–AC7）
- [x] 9/20：CLI 自然语言查实时读数（`net` 专项板测）
- [ ] **演示 MVP**：点表阈值 → 注入超阈或离线 → HMI 告警 + `pending_alarm.txt`（推进方案 §10）
- [ ] 对照手册 §14.1（演示子集；未做项标明阶段）
- [ ] §14.2：日报 ≥90% 已有一次证据；写操作 = 0；告警解释 ≥70% 若未板测则笔记跳过
- [ ] （尽力，`velaguard-net`）事件主动告警解释板测
- [ ] （尽力）HMI 运行时手动 `ai_agent &` 不拖死整机

## References

- `VelaGuard_推进方案.md` §6、§9.2、§10
- `VelaGuard_项目手册.md` §14
- `CONTEXT.md`
- `08-30-stage1-lvgl-hmi/research/lvgl-hmi-notes.md`
- `08-30-stage1-modbus-discovery/research/modbus-discovery-notes.md`
