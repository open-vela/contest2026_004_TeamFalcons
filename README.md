# VelaGuard

> **团队**：contest2026_004 Team Falcons（FoLeaf）  
> **赛道**：AI 硬件产品创新  
> **硬件**：STM32H750B-DK + VelaGuard 扩展板（RS485 / RJ45 / ESP-01 备链 / LTDC 触控）  
> **手册**：完整产品与架构说明见 [`VelaGuard_项目手册.md`](VelaGuard_项目手册.md)（v3 · 2026-08-29）

**一句话**：运行在 openvela 上的 RS485/Modbus 现场网关。采集、告警与统计全部由本地确定性逻辑完成；板载 `ai_agent` 做运营助手——告警自动解释、日报/周报，以及自然语言查实时数据。

---

## 它解决什么问题

把一台设备接到陌生 485 总线上，工程师通常要手工试地址与波特率、人肉猜字序与倍率、对着告警数值自己脑补含义。VelaGuard 把前半段交给扫描与约束求解，把「对人意味着什么」交给板载 Agent：

| 能力 | 常见做法 | VelaGuard |
|------|----------|-----------|
| 发现从站 | 手工试地址 × 波特率 | 自动扫描矩阵 |
| 识别数据格式 | 试四种字序看哪个像 | 物理合理性 + 时间连续性约束求解 |
| 定位通信故障 | 老师傅经验 | 确定性规则库（阶段 2，不依赖 Agent） |
| 理解告警 | 看原始数值自己猜 | Agent 自动解释（标注「AI 推测」） |
| 运行概况 | 翻日志 / 手工统计 | Agent 定时生成日报/周报 |
| 查当前读数 | 组态或串口助手 | 自然语言提问（CLI / LVGL） |

目标用户是和作者同类的嵌入式工程师、自动化调试与系统集成人员——**不面向**产线操作工；Agent **不做**总线排障实验设计。

---

## 系统形态

独立网关，不依赖长期连接电脑。USB CDC / UART 只作开发调试与救援通道。

```mermaid
graph TD
    subgraph Field["现场侧"]
        S1["Modbus RTU 从站<br/>温湿度 / 电表 / 流量计 …"]
    end

    subgraph Gateway["STM32H750B-DK + openvela"]
        AI["板载 ai_agent<br/>告警解释 · 报告 · NL 查数"]
        HMI["LVGL 现场 HMI<br/>只读 + 确认"]
        NET["RJ45 Ethernet（主）<br/>ESP-01 Wi-Fi（备）"]
    end

    subgraph Cloud["云端"]
        LLM["云端 LLM（MiMo）<br/>OpenAI 兼容 HTTPS"]
        MQTT["MQTT Broker<br/>遥测 / 告警 / OTA"]
    end

    S1 -->|"RS485 半双工"| Gateway
    AI -->|"HTTPS"| LLM
    NET -->|"MQTT"| MQTT
```

**永远本地（断网也继续）**：Modbus 周期采集、帧级质量统计、阈值/突变/离线告警、屏幕告警、配置与事件日志、数字量输出安全默认态。  
**需要网络**：Agent 解释与报告（LLM 在云端）、遥测上云、OTA。断网时展示规则引擎原始信息，不假装还能「AI 诊断」。

```mermaid
graph TD
    subgraph Always["永远本地"]
        L1["Modbus 周期采集"]
        L2["帧级质量统计"]
        L3["阈值 / 突变 / 离线告警"]
        L4["屏幕告警与状态"]
        L5["配置与事件日志"]
    end

    subgraph Online["需要网络"]
        N1["Agent 告警解释 / 报告"]
        N2["遥测与告警上云"]
        N3["OTA"]
    end

    subgraph Degraded["断网降级"]
        D1["无 AI 解释与报告"]
        D2["展示规则引擎原始告警"]
        D3["阶段2: 485 规则库确定性归因"]
    end

    N1 -->|"断网时"| Degraded
```

---

## 软件分层

应用层只管「给人看 / 给人确认」；真正的采集与安全边界在 VelaGuard Service；Agent 只能通过只读工具碰已采集数据。

```mermaid
graph TD
    subgraph App["Application"]
        HMI_APP["hmi_app<br/>LVGL 现场 HMI"]
        AGENT_APP["agent_app<br/>告警解释 / 报告 / 查询编排"]
    end

    subgraph AgentLayer["ai_agent（packages/ai_agent）"]
        REACT["ReAct loop（≤10 轮）"]
        TOOLS_REG["tool_registry · 自定义 C 工具"]
        SKILLS["skills · /data/agent/skills/*.md"]
        PROACTIVE["proactive：事件主动 + 定时主动"]
        LLM_PROXY["llm_proxy · OpenAI 兼容 HTTPS"]
    end

    subgraph VGService["VelaGuard Service"]
        MC["modbus_collector"]
        BP["bus_prober"]
        FS["frame_stats"]
        RE["rule_engine"]
        DR["diag_rules（阶段2）"]
        AT["agent_tools · 只读 + 沙箱"]
        PT["point_table / config_store"]
        NM["network_manager · RJ45/ESP-01"]
        MQ["mqtt_client / ota_service"]
    end

    subgraph OS["openvela / NuttX"]
        RTOS["task · VFS · FAT · SDMMC · sockets"]
        HW["UART(RS485) · Ethernet · LTDC · Touch"]
    end

    App --> AgentLayer
    App --> VGService
    AgentLayer --> VGService
    VGService --> OS
```

### Agent 与确定性逻辑怎么分工

这是架构核心：**穷举、约束求解、统计、485 归因走确定性路径；解释与汇总才走 Agent。**

```mermaid
graph LR
    subgraph Det["确定性算法"]
        D1["地址 × 波特率扫描"]
        D2["字序约束求解"]
        D3["帧级质量统计"]
    end

    subgraph Agent["板载 AI（运营助手）"]
        A1["告警解释 · 事件主动"]
        A2["日报/周报 · 定时主动"]
        A3["自然语言查实时数据"]
    end

    subgraph Rule["规则库（阶段2）"]
        R1["485 症状 → 归因决策表"]
    end

    D1 -->|"点表/采集"| A1
    D3 -->|"触发告警"| A1
    D3 -->|"统计输入"| R1
```

字序求解不用 LLM：枚举 ABCD/BADC/CDAB/DCBA → 物理合理性筛选（NaN/inf 等）→ 时间连续性筛选 → 联合判定类型与倍率；唯一解直接落点表，否则列出候选交屏幕确认。

```mermaid
flowchart TD
    A["寄存器原始字节"] --> B["枚举 4 种字序"]
    B --> C{"物理合理性?"}
    C -->|"排除"| X1["排除该字序"]
    C -->|"通过"| D["连续多次采样"]
    D --> E{"时间连续性?"}
    E -->|"剧烈跳变"| X2["排除"]
    E -->|"平滑"| F["联合类型 + 倍率推断"]
    F --> G{唯一解?}
    G -->|"是"| I["确定字序+类型+倍率"]
    G -->|"否"| J["候选项 → 屏幕确认"]
```

---

## 典型场景（图文）

### 接入陌生总线

接上 485 →「Scan Bus」→ 扫描地址 × 波特率 → 探测寄存器块 → 约束求解推断类型/字序/倍率 → 点表预览 → 测试读取 → 人工确认 → 进入采集循环。探查与求解全部本地、不依赖网络。

### 告警自动解释（事件主动）

规则引擎先在本地弹出告警；联网时自动启动解释会话，Agent 用只读工具拉上下文，输出摘要 + evidence，UI 标注「AI 推测」。**不自动清告警、不改配置。**

### 日报 / 周报（定时主动）

定时器到点后汇总窗口内告警与指标，报告写入 `/data/velaguard/reports/`，LVGL 报告页可预览。9/20 里程碑以日报为必做，周报可后补。

```mermaid
flowchart TD
    subgraph Event["事件主动：告警解释"]
        A1["规则引擎产生告警"] --> A2["本地告警弹出"]
        A2 --> A3{网络可用?}
        A3 -->|"否"| A4["展示规则引擎原始信息"]
        A3 -->|"是"| A5["自动启动解释会话"]
        A5 --> A6["拉取告警上下文 / 实时值 / 事件"]
        A6 --> A7["解释 + evidence<br/>标注「AI 推测」"]
    end

    subgraph Timer["定时主动：日报/周报"]
        B1["定时器到点"] --> B2["汇总窗口内告警与指标"]
        B2 --> B3["报告落盘 reports/"]
        B3 --> B4["LVGL 报告页预览"]
    end
```

自定义 Skill：`alarm_interpretation.md`、`operations_report.md`（`/data/agent/skills/`）。交互渠道另支持 CLI / LVGL 自然语言查数（如「5 号从站流量怎么样？」），满足赛道交互要求；主场景仍是上面两类主动能力。

---

## Agent 安全边界

能对活着的工业总线下指令的 LLM 是危险品。边界写在 **C 工具注册层**，不依赖 prompt：

- 工具集只读：无 Modbus 写、无总线探测实验类操作
- 地址、时间窗口、点位数量有硬上界；单位时间限流
- 按会话类型门控；每次调用写审计日志
- Agent 只产出解释与建议；处置须人工确认后由本地代码执行

```mermaid
flowchart LR
    LLM["AI Agent<br/>ReAct"] --> GUARD["tool_guard / 沙箱"]
    GUARD --> CHECK1{"只读查询?"}
    CHECK1 -->|"写 / 总线实验"| BLOCK["不存在 · 无法调用"]
    CHECK1 -->|"读状态 / 历史"| CHECK2{"参数越界 / 限流 / 会话?"}
    CHECK2 -->|"否"| REJECT["拒绝"]
    CHECK2 -->|"是"| EXEC["执行只读查询"]
    EXEC --> AUDIT["结构化审计日志"]
```

输出侧：schema 校验 → 风险分级 → LVGL 展示（「AI 推测」）→ 现场确认 → 本地代码执行 → `events.jsonl` 记 `agent_suggestion`。

---

## 网络、启动与 OTA

`network_manager` 管理 **单活动链路**：RJ45 优先，故障切 ESP-01，RJ45 恢复并经稳定窗口后再切回。指数退避重连。该模块已基本建成并进入冻结维护。

```mermaid
stateDiagram-v2
    [*] --> NET_DOWN
    NET_DOWN --> NET_CONNECTING : 触发连接
    NET_CONNECTING --> NET_ONLINE_RJ45 : RJ45 成功
    NET_CONNECTING --> NET_ONLINE_WIFI : Wi-Fi 成功
    NET_CONNECTING --> NET_DOWN : 失败（指数退避）
    NET_ONLINE_RJ45 --> NET_DEGRADED : 质量下降
    NET_ONLINE_WIFI --> NET_DEGRADED : 质量下降
    NET_DEGRADED --> NET_ONLINE_RJ45 : RJ45 恢复
    NET_DEGRADED --> NET_ONLINE_WIFI : Wi-Fi 恢复
    NET_ONLINE_RJ45 --> NET_DOWN : 断线
    NET_ONLINE_WIFI --> NET_DOWN : 断线
    NET_ONLINE_WIFI --> NET_ONLINE_RJ45 : RJ45 稳定后切回
```

启动顺序保证「先本地安全环、后网络与 Agent」：硬件/看门狗 → eMMC → 配置双槽 → 采集与帧统计 → 告警规则 → DO 安全默认 → LVGL → 网络/MQTT → ai_agent。

固件从 QSPI **XIP** 执行，运行期不能擦写 QSPI。OTA 因此改为：应用把镜像落到 eMMC → 重启 → 片内 boot stub 校验并烧写 QSPI → 自检确认或回滚。

```mermaid
sequenceDiagram
    participant Broker as MQTT Broker
    participant App as 应用（QSPI XIP）
    participant eMMC as eMMC
    participant Stub as 片内 Boot Stub
    participant QSPI as QSPI NOR

    Broker->>App: OTA offer（分片）
    App->>eMMC: staging.img + 校验签名
    App->>App: 置升级标志，重启
    Stub->>eMMC: 读 staging
    Stub->>QSPI: 擦写新固件
    Stub->>Stub: 跳转新固件
    alt 自检通过
        App->>QSPI: mark confirmed
    else 自检失败
        Stub->>eMMC: rollback.img
        Stub->>QSPI: 回滚
    end
```

MQTT 主题形如 `vg/{device_id}/telemetry|status|alarm|diagnosis|ota/...`；不再承载已移除的语音/TTS 路径。详情与 QoS 见项目手册 §8 / §16。

---

## 现场 HMI

PC 模拟器源码在 [`gui/`](gui/)（自 [FoLeaf/velaguard_gui](https://github.com/FoLeaf/velaguard_gui) 迁入，按手册 §6 裁剪）。WSL 构建见 [`gui/README.md`](gui/README.md)；Windows 见 `gui/README_CN.md`。

原则：**只读 + 确认，屏上不做编辑。** 告警与报告入口比配置更突出；「AI 推测」与「确定性结论」视觉上必须区分。总线扫描 **UI 开关默认关闭**。

```mermaid
graph TD
    HOME["首页 / 总览<br/>NET · AI · 各从站状态"]
    HOME --> SLAVE["从站详情"]
    HOME --> TREND["实时趋势"]
    HOME --> ALARM["告警详情"]
    ALARM --> EXPLAIN["AI 告警解释"]
    HOME --> REPORTS["运行报告"]
    HOME --> SCAN["总线探查"]
    SCAN --> PREVIEW["点表预览 → 测试读取 → 确认"]
    HOME --> EVENTS["事件日志"]
    HOME --> SYSINFO["系统状态"]
```

持久化落在板载 8 GB eMMC（FAT）：`/data/agent/`（Skill、会话）、`/data/velaguard/`（点表双槽、日志、`reports/`、OTA staging）。配置写入用双槽 + CRC + 单调序号对抗 FAT 非掉电安全。

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

```mermaid
mindmap
  root((VelaGuard 评审))
    基础要求
      Agent 上板
      CLI / LVGL 交互
      自定义 Skill
      事件主动 + 定时主动
    加分项
      Bridge 端云协作
      LVGL 工业 HMI
```

可证伪目标（手册 §14.2）：告警解释合理率 ≥ 70%；日报指标一致率 ≥ 90%；全过程写操作触发次数 = 0。

权威规则以官方赛道文档为准；本地边界见 [`docs/agents/BOUNDARY.md`](docs/agents/BOUNDARY.md)，术语见 [`CONTEXT.md`](CONTEXT.md)。

---

## 构建与烧录

在 **openvela 工作区根目录**（含 `.repo/` 的父目录）同步 manifest 后，于本仓执行：

```bash
cd contest2026_004_TeamFalcons
bash scripts/build.sh min    # 最小 bring-up（无网络）
bash scripts/build.sh net    # 完整网络 + MQTT + ESP8266 备链
```

产物：`contest2026_004_TeamFalcons/.debug/nuttx.hex` 与 `qspi_bootstub.hex`（QSPI XIP 启动需两份 HEX 依次烧录）。

**公共仓依赖**：改动在 nuttx / nuttx-apps / MQTT-C 的 feature 分支上，**不再使用 patch**。构建前 `build.sh` 会校验本地树是否包含这些改动；若未切换分支：

```bash
bash scripts/build.sh --sync-upstream net
# 或手动：
#   git -C ../nuttx checkout velaguard/integration
#   git -C ../apps checkout velaguard/netinit-esp8266
#   git -C ../apps/netutils/mqttc/MQTT-C checkout velaguard/mqtt-pal-hook
```

### 公共仓 PR

| 仓库 | 分支 | PR | CI |
|------|------|-----|-----|
| open-vela/nuttx | `velaguard/qspi-boot-stm32h750b-dk` | https://github.com/open-vela/nuttx/pull/350 | ✅ |
| open-vela/nuttx | `velaguard/board-and-defconfigs` | https://github.com/open-vela/nuttx/pull/351 | ✅ |
| open-vela/nuttx | `velaguard/eth-mii-stm32h750b-dk` | https://github.com/open-vela/nuttx/pull/352 | ✅ |
| open-vela/nuttx | `velaguard/display-acceleration-stm32h750b-dk` | https://github.com/open-vela/nuttx/pull/353 | ✅ |
| open-vela/nuttx | `velaguard/ui-performance-stm32h750b-dk` | https://github.com/open-vela/nuttx/pull/354 | ✅ |
| open-vela/nuttx-apps | `velaguard/netinit-esp8266` | https://github.com/open-vela/nuttx-apps/pull/119 | ✅ |
| open-vela/apps_netutils_mqttc_MQTT-C | `velaguard/mqtt-pal-hook` | https://github.com/open-vela/apps_netutils_mqttc_MQTT-C/pull/1 | ✅ |

Fork：`FoLeaf/nuttx`、`FoLeaf/nuttx-apps`、`FoLeaf/apps_netutils_mqttc_MQTT-C`。本地集成分支（仅开发用）：`nuttx/velaguard/integration`。

---

## AI Coding 日志

路径：`logs/Foleaf/`（已索引会话经 `validate-log.py` 校验；本地 schema 已收录 `cursor` / `grok-build`，以诚实标签入索引）。

```bash
python3 ../.claude/skills/contest-log-collector/tools/validate-log.py logs/
```

---

## 更多文档

| 文档 | 内容 |
|------|------|
| [`VelaGuard_项目手册.md`](VelaGuard_项目手册.md) | 产品边界、模块细则、HMI、存储、验收与架构决策 |
| [`CONTEXT.md`](CONTEXT.md) | 领域术语 |
| [`docs/agents/BOUNDARY.md`](docs/agents/BOUNDARY.md) | 赛题与本地边界 |
| [`app/velaguard/README.md`](app/velaguard/README.md) | 应用目录说明 |
| [`board/contest_board/README.md`](board/contest_board/README.md) | 扩展板相关说明 |

手册中另有 Modbus 通信状态机、故障归因决策表、启动顺序细节、MQTT QoS 等，评审与实现以手册正文为准；本 README 只保留导航级图文。
