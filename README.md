<p align="center">
  <img src="./assets/readme/hero.svg" width="100%" alt="VelaGuard：运行在 openvela 上的 RS485/Modbus 现场网关，本地确定性采集告警，板载 AI 运营助手">
</p>

<p align="center">
  <b>contest2026_004 · Team Falcons（FoLeaf）</b><br>
  AI 硬件产品创新 · STM32H750B-DK · openvela / NuttX
</p>

<p align="center">
  <a href="VelaGuard_项目手册.md">项目手册</a> ·
  <a href="docs/agents/BOUNDARY.md">边界说明</a> ·
  <a href="CONTEXT.md">术语表</a> ·
  <a href="#构建与烧录">构建</a>
</p>

---

## 这是什么

**VelaGuard** 是一台运行在 **STM32H750B-DK + openvela** 上的 **RS485/Modbus 现场网关**。

采集、告警与统计全部由本地确定性逻辑完成，断网也继续。板载 `ai_agent` 做运营助手：告警自动解释、定时日报/周报、自然语言查实时数据。它**不**写总线、不改配置、不替人做处置。

目标用户是嵌入式工程师、自动化调试与系统集成人员，不是产线操作工。

---

## 它解决什么问题

把一台设备接到陌生 485 总线上，工程师通常要手工试地址与波特率、人肉猜字序与倍率、对着告警数值自己脑补含义。

| 能力 | 常见做法 | VelaGuard |
|------|----------|-----------|
| 发现从站 | 手工试地址 × 波特率 | 自动扫描矩阵（阶段 1：`vgdiscover` @9600） |
| 识别数据格式 | 试四种字序看哪个像 | 物理合理性 + 时间连续性约束求解 |
| 定位通信故障 | 老师傅经验 | 确定性规则库（阶段 2，不依赖 Agent） |
| 理解告警 | 看原始数值自己猜 | Agent 自动解释（标注「AI 推测」） |
| 运行概况 | 翻日志 / 手工统计 | Agent 定时生成日报/周报 |
| 查当前读数 | 组态或串口助手 | 自然语言提问（CLI / LVGL） |

---

## 系统形态

独立网关，不依赖长期连接电脑。USB CDC / UART 只作开发调试与救援通道。

<p align="center">
  <img src="./assets/readme/system-map.svg" width="100%" alt="系统形态：现场 Modbus 从站经 RS485 进入 H750B-DK 网关，网关内含永远本地的采集告警环、LVGL HMI 与 ai_agent；网络侧连接 MQTT 与 MiMo LLM">
</p>

**永远本地**（断网也继续）：Modbus 周期采集、帧级质量统计、阈值/突变/离线告警、屏幕告警、配置与事件日志、数字量输出安全默认态。

**需要网络**：Agent 解释与报告（LLM 在云端）、遥测上云、OTA。断网时展示规则引擎原始信息，不假装还能 AI 诊断。

---

## 软件怎么分层

应用层只管给人看、给人确认；真正的采集与安全边界在 VelaGuard Service；Agent 只能通过只读工具碰已采集数据。

| 层 | 职责 | 代表模块 |
|----|------|----------|
| Application | 现场 HMI / 会话编排 | `hmi_app` · `agent_app` |
| ai_agent | ReAct、Skill、主动任务、HTTPS | `packages/ai_agent` |
| VelaGuard Service | 采集、探查、统计、规则、只读工具、网络 | `modbus_collector` · `rule_engine` · `agent_tools` · `network_manager` |
| openvela / NuttX | 任务、文件系统、串口、以太网、LTDC | UART(RS485) · Ethernet · eMMC |

### 确定性算法 vs Agent

这是架构核心：

- **走确定性路径**：地址×波特率扫描、字序约束求解、帧级统计、485 故障归因（阶段 2）
- **走 Agent**：告警解释、日报/周报、自然语言查数

字序求解不用 LLM：枚举 ABCD/BADC/CDAB/DCBA → 物理合理性筛选 → 时间连续性筛选 → 联合判定类型与倍率。唯一解直接落点表，否则列出候选交屏幕确认。

---

## Agent 安全边界

<p align="center">
  <img src="./assets/readme/agent-boundary.svg" width="100%" alt="Agent 安全边界：只读 C 工具、参数上界与限流、schema 校验、AI 推测标注、人工确认后本地执行">
</p>

边界写在 **C 工具注册层**，不依赖 prompt：

- 工具集只读：无 Modbus 写、无总线探测实验类操作
- 地址、时间窗口、点位数量有硬上界；单位时间限流
- 按会话类型门控；每次调用写审计日志
- Agent 只产出解释与建议；处置须人工确认后由本地代码执行

输出侧：schema 校验 → 风险分级 → LVGL 展示（「AI 推测」）→ 现场确认 → 本地代码执行 → `events.jsonl` 记 `agent_suggestion`。

---

## 典型场景

### 1. 接入陌生总线

接上 485 → 开启扫描开关（默认关闭）→ 扫描地址×波特率 → 探测寄存器块 → 约束求解推断类型/字序/倍率 → 点表预览 → 测试读取 → 人工确认 → 进入采集循环。探查与求解全部本地。

### 2. 告警自动解释（事件主动）

规则引擎先在本地弹出告警；联网时自动启动解释会话，Agent 用只读工具拉上下文，输出摘要 + evidence，UI 标注「AI 推测」。**不自动清告警、不改配置。**

### 3. 日报 / 周报（定时主动）

定时器到点后汇总窗口内告警与指标，报告写入 `/data/velaguard/reports/`，LVGL 报告页可预览。9/20 里程碑以日报为必做，周报可后补。

自定义 Skill：`alarm_interpretation.md`、`operations_report.md`（`/data/agent/skills/`）。CLI / LVGL 另支持自然语言查数（如「5 号从站流量怎么样？」）。

---

## 网络、启动与 OTA

`network_manager` 管理**单活动链路**：RJ45 优先，故障切 ESP-01，RJ45 恢复并经稳定窗口后再切回。指数退避重连。

启动顺序保证先本地安全环、后网络与 Agent：

```text
硬件/看门狗 → eMMC → 配置双槽 → 采集与帧统计
→ 告警规则 → DO 安全默认 → LVGL → 网络/MQTT → ai_agent
```

固件从 QSPI **XIP** 执行。OTA：应用把镜像落到 eMMC → 重启 → 片内 boot stub 校验并烧写 QSPI → 自检确认或回滚。MQTT 主题形如 `vg/{device_id}/telemetry|status|alarm|diagnosis|ota/...`。

现场 HMI 原则：**只读 + 确认，屏上不做编辑。** 总线扫描 UI 开关默认关闭。持久化落在板载 8 GB eMMC（FAT），配置写入用双槽 + CRC + 单调序号。

PC 模拟器源码在 [`gui/`](gui/)。WSL 构建见 [`gui/README.md`](gui/README.md)。

---

## 大赛赛道对照

| 官方要求 | 本项目落点 |
|----------|------------|
| Agent 在设备上跑起来 | `packages/ai_agent` 移植到 STM32H750B-DK（Cortex-M7） |
| ≥1 交互渠道 | CLI（`vela>`）+ LVGL |
| ≥1 自定义 Skill | `alarm_interpretation.md`、`operations_report.md` |
| ≥1 主动 + 执行 | **事件主动**告警解释；**定时主动**日报/周报 |
| openvela 能力 | LVGL HMI、ai_agent + 云端 MiMo、触控 UI |
| 加分：端云协作 | Bridge 化 LLM（后续增强） |
| 加分：自定义 UI | 工业风格只读+确认 HMI |

可证伪目标（手册 §14.2）：告警解释合理率 ≥ 70%；日报指标一致率 ≥ 90%；全过程写操作触发次数 = 0。

权威规则以官方赛道文档为准；本地边界见 [`docs/agents/BOUNDARY.md`](docs/agents/BOUNDARY.md)，术语见 [`CONTEXT.md`](CONTEXT.md)。

---

## 构建与烧录

在 **openvela 工作区根目录**（含 `.repo/` 的父目录）同步 manifest 后，于本仓执行：

```bash
cd contest2026_004_TeamFalcons
bash scripts/build.sh          # 作品主线 velaguard-lvgl（网络 + 带屏 + Agent）
bash scripts/build.sh min      # 仅 bring-up（无网络，不是提交镜像）
```

产物：`.debug/nuttx.hex` 与 `qspi_bootstub.hex`（QSPI XIP 启动需两份 HEX 依次烧录）。

公共仓改动在 nuttx / nuttx-apps / MQTT-C 的 feature 分支上，**不使用 patch**。构建前 `build.sh` 会校验本地树；若未切换分支：

```bash
bash scripts/build.sh --sync-upstream
# 或手动：
#   git -C ../nuttx checkout velaguard/integration
#   git -C ../apps checkout velaguard/netinit-esp8266
#   git -C ../apps/netutils/mqttc/MQTT-C checkout velaguard/mqtt-pal-hook
```

### 公共仓 Pull Request

| 仓库 | 分支 | PR |
|------|------|-----|
| open-vela/nuttx | `velaguard/qspi-boot-stm32h750b-dk` | [#350](https://github.com/open-vela/nuttx/pull/350) |
| open-vela/nuttx | `velaguard/board-and-defconfigs` | [#351](https://github.com/open-vela/nuttx/pull/351) |
| open-vela/nuttx | `velaguard/eth-mii-stm32h750b-dk` | [#352](https://github.com/open-vela/nuttx/pull/352) |
| open-vela/nuttx | `velaguard/display-acceleration-stm32h750b-dk` | [#353](https://github.com/open-vela/nuttx/pull/353) |
| open-vela/nuttx | `velaguard/ui-performance-stm32h750b-dk` | [#354](https://github.com/open-vela/nuttx/pull/354) |
| open-vela/nuttx-apps | `velaguard/netinit-esp8266` | [#119](https://github.com/open-vela/nuttx-apps/pull/119) |
| open-vela/apps_netutils_mqttc_MQTT-C | `velaguard/mqtt-pal-hook` | [#1](https://github.com/open-vela/apps_netutils_mqttc_MQTT-C/pull/1) |

Fork：`FoLeaf/nuttx`、`FoLeaf/nuttx-apps`、`FoLeaf/apps_netutils_mqttc_MQTT-C`。本地集成分支（仅开发用）：`nuttx/velaguard/integration`。

---

## AI Coding 日志

路径：`logs/Foleaf/`（已索引会话经校验；本地 schema 收录 `cursor` / `grok-build`，以诚实标签入索引）。

```bash
python3 ../.claude/skills/contest-log-collector/tools/validate-log.py logs/
```

---

## 文档导航

| 文档 | 内容 |
|------|------|
| [`VelaGuard_项目手册.md`](VelaGuard_项目手册.md) | 产品边界、模块细则、HMI、存储、验收与架构决策 |
| [`CONTEXT.md`](CONTEXT.md) | 领域术语 |
| [`docs/agents/BOUNDARY.md`](docs/agents/BOUNDARY.md) | 赛题与本地边界 |
| [`app/velaguard/README.md`](app/velaguard/README.md) | 应用目录说明 |
| [`board/contest_board/README.md`](board/contest_board/README.md) | 扩展板相关说明 |

Modbus 通信状态机、故障归因决策表、启动顺序细节、MQTT QoS 等以手册正文为准；本 README 只保留导航级说明。

---

## 许可证

Apache License 2.0。作品为原创实现；第三方组件保留其自身许可证（如 `app/velaguard/nanomodbus/` 的 MIT）。
