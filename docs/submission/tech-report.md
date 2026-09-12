# VelaGuard 技术报告（草稿骨架）

> 按《2026 首届 openvela AI 硬件开发者大赛 · 作品提交模板》第二节结构。每节下的引用块是数据来源提示，成稿时删除。
> 成稿：`pandoc docs/submission/tech-report.md -o VelaGuard-技术报告.docx`，再导出 pdf。
> 计划与每节素材映射见 [`../plan-917-submission.md`](../plan-917-submission.md) §5。

## 1、信息表

| 项目 | 内容 |
|---|---|
| 作品名称 | VelaGuard |
| 队伍名称 | Team Falcons（contest2026_004_TeamFalcons） |
| 团队分工 | （1 人：固件 / 硬件扩展板 / HMI / Agent / 文档） |
| 选题方向 | AI 硬件产品创新（主）；新硬件平台适配（驱动开发部分：QSPI XIP、ETH、显示、MQTT PAL） |

## 2、摘要

> ≤300 字。最后写。三个量化数字：公共仓 PR 数、本地告警时延、Agent 工具调用次数 / 写操作 0。

## 3、正文

### 3.1 绪论

**项目背景与问题定义**

> README「它解决什么问题」表。

**技术难点**

> Cortex-M7 跑 ai_agent（片内 SRAM 静态占用 92.5%）；QSPI XIP 下的启动与 OTA 形态；RJ45/ESP-01 单活动链路；LVGL 与 Agent 共存的内存预算。

**创新点**

> 1 本地安全环：采集 / 告警 / 落盘永远本地。2 Agent 只读边界写在 C 工具注册层（`tool_shell.c`），不是 prompt。3 Agent 只解释、报告、查数，不动产线；输出标「AI 推测」。

### 3.2 系统方案设计

**系统总体架构**

> `assets/readme/system-map.svg`；README「软件怎么分层」表。

**方案论证与选型**

> 开发板：H750B-DK（RS485 UART7 + 4.3" 屏 + SDRAM + QSPI + eMMC 扩展 + ETH）对比 ESP32-S3 / Gemini-S1。
> 端 / 云职责：手册 §2.2 分界线原文；断网降级口径。
> LLM 链路：直连 MiMo（`llm_proxy`）vs Bridge（ADR-0002），选直连的理由。

**关键模块设计**

> 通信：`vg_net_mgr` 单活动链路、指数退避；Modbus：nanoMODBUS + `/dev/rs485`；交互：LVGL HMI 只读 + 确认、CLI `vela>`。

### 3.3 核心算法与技术原理

**AI 算法实现**

> 端侧无模型；云端 MiMo（OpenAI 兼容 `/v1/chat/completions`，Bearer key），key 由 `vgprovision` 加密存于 eMMC，不进固件。ReAct 循环参数：HMI 构建静态栈 16 KB、上下文 4 KB、流缓冲 4 KB（`agent_config.h:411-431`）。

**关键机制设计**

> `vg_alarm_eval`：cmp ∈ {eq, ge, le}、warn / crit、fail_n 离线、严重 > 离线 > 预警；候选表 → 试读 → 人确认 → 已确认表；工具白名单与 `vgpoint` / `vgdiscover` / `vgcfg commit` 硬拒绝。

**openvela 系统能力的深度运用**

> 图形：LVGL HMI。AI：`packages/ai_agent`（ReAct、Skill、run_shell / read_file / write_file 工具）。对 openvela 的拓展：nuttx #350–#354、nuttx-apps #119、MQTT-C #1；`packages/ai_agent` 的 HMI 内存路径补丁。改进建议：ai_agent 在 Cortex-M 上的内存配置项、mallinfo 遍历在高占用堆上的断言。

### 3.4 系统实现

**软件 / 固件架构**

> README 分层表 + 启动顺序。

**数据流与关键流程设计**

> 图：Modbus 采集 → `vg_alarm_eval` → 告警页 / `pending_alarm.txt` → Agent（`alarm_interpretation.md`）→ `reports/last_alarm.md` → 告警页；定时 / 提问 → `operations_report.md` → `reports/daily-*.md` → 报告页。

**硬件设计与适配**

> H750B-DK；扩展板 `docs/velaguard-expansion-board.md`（RS485 UART7、ESP-01 USART2、DO 低边、LED、BOM §7）；eMMC。
> 是否完成驱动开发：**是**。表：PR / 驱动类型 / 难点 / 解决。

**应用 / 交互端设计**

> LVGL 页面截图：首页、探查、告警、报告；CLI；上位机（协议 `docs/velaguard-host-nsh-protocol.md`，任何串口终端可用）。

**自定义 Skill**

> `/data/agent/skills/alarm_interpretation.md`（触发：告警发生 / 用户提问）、`operations_report.md`（触发：定时 / 用户要日报）、`modbus_query.md`（触发：查实时值）。各自的步骤、允许工具、禁令。来源 `app/velaguard/vg_agent_seed.c`。

### 3.5 系统测试与结果分析

**测试环境**

> 板卡、固件版本（commit）、从站（型号或 MThings 模拟）、broker（若用）、MiMo 模型。

**功能测试**

> 表：项 / 方法 / 结果。来源 `plan-917-submission.md` §4。

**性能测试**

> 表：开机时间、采集周期、告警时延、离线判定时延、`ask` 端到端时延 / 迭代 / 工具数、SRAM / SDRAM / 固件大小。

**可靠性与稳定性测试**

> 24 h 连续运行；注入 N 次命中 / 误报 / 漏报；拔插 485 / 网线恢复时间；写操作 = 0；断网降级行为。

### 3.6 AI-Native 开发说明

| 指标 | 数据 |
|---|---|
| AI Coding 代码占比 | （估算 + 口径） |
| 使用的 AI 工具 | Claude Code、Codex、OpenCode、Cursor、Grok Build（与 `logs/Foleaf/` 标签一致） |
| MCP 工具使用情况 | （如实） |
| Skills 使用与新增情况 | 使用：官方 `openvela-build`、`contest-log-collector`、`nuttx-driver-development`、`driver-code-reviewer`、`codesize`、`kconfig-tweak`、`submit-pr`。新增：`.agents/skills/velaguard-alarm-to-screen`、`velaguard-candidate-confirm`、`velaguard-board-inner-loop`、`velaguard-board-hw`、`mthings-automation-config-skill`；板端运行时 Skill 3 份 |
| Token 使用总量 | （MiMo 控制台） |

**补充**

> 效率提升实例；问题：mallinfo 断言、Cursor 日志标签、UNC 路径下 ripgrep 超时。

### 3.7 总结与展望

**成果总结**

**应用前景与商业价值**

> 受众：嵌入式工程师 / 自动化集成商 / 系统调试人员；模式：网关硬件 + 组态工具 + 云看板订阅；规模化：多设备集中监控（`plan-cloud-backend.md` C1 / C5）。

**不足与未来工作**

> 带屏固件 Agent 心跳关闭（需操作员触发）；MQTT 仅 status 且明文；OTA 未实现；周报、规则库为后续阶段。链接云端计划。
