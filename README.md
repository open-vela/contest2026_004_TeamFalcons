# VelaGuard 项目手册 — Mermaid 图表总览

> 基于 `VelaGuard_项目手册.md` v2（2026-08-20 设计评审后重写）整理

---

## 1. 系统整体形态

```mermaid
graph TD
    subgraph Field["现场侧"]
        S1["Modbus RTU 从站\n温湿度 / 电表 / 流量计 …"]
    end

    subgraph Gateway["STM32H750B-DK + openvela"]
        AI["板载 ai_agent\nReAct 循环 + Modbus 只读工具集"]
        HMI["LVGL 现场 HMI\n只读 + 确认"]
        NET["RJ45 Ethernet（主）\nESP-01 Wi-Fi（备）"]
    end

    subgraph Cloud["云端"]
        LLM["云端 LLM（MiMo）\nOpenAI 兼容 HTTPS"]
        MQTT["MQTT Broker\n遥测 / 告警 / OTA"]
    end

    S1 -->|"RS485 半双工"| Gateway
    AI -->|"HTTPS"| LLM
    NET -->|"MQTT"| MQTT
```

---

## 2. 软件分层架构

```mermaid
graph TD
    subgraph App["Application Layer"]
        HMI_APP["hmi_app\nLVGL 现场 HMI（只读+确认）"]
        DIAG_APP["diagnostic_app\n诊断模式编排 / 探查会话管理"]
    end

    subgraph AgentLayer["ai_agent（openvela packages/ai_agent）"]
        REACT["ReAct loop\n多轮工具调用（上限10轮）"]
        TOOLS_REG["tool_registry\n自定义 C 工具注册"]
        SKILLS["skills\n/data/agent/skills/*.md"]
        PROACTIVE["proactive task\n阈值触发的主动诊断"]
        LLM_PROXY["llm_proxy\nOpenAI 兼容 HTTPS"]
    end

    subgraph VGService["VelaGuard Service"]
        MC["modbus_collector\n周期采集"]
        BP["bus_prober\n扫描/块探测/字序求解"]
        FS["frame_stats\n帧级质量统计"]
        RE["rule_engine\n阈值/突变/离线告警"]
        DR["diag_rules\n故障归因规则库（阶段2）"]
        AT["agent_tools\nModbus 只读工具+沙箱"]
        PT["point_table\n点表存储与热加载"]
        CS["config_store\n双槽掉电安全配置"]
        ES["event_store\n结构化事件日志"]
        NM["network_manager\nRJ45 / ESP-01 单活动链路"]
        MQ["mqtt_client\n遥测/告警/OTA"]
        OTA["ota_service\n镜像下载与暂存"]
    end

    subgraph OS["openvela / NuttX"]
        RTOS["task / pthread / VFS / FAT / SDMMC\nsockets / mbedTLS"]
        HW["UART(RS485) / Ethernet / LTDC / Touch / GPIO"]
    end

    App --> AgentLayer
    App --> VGService
    AgentLayer --> VGService
    VGService --> OS
```

---

## 3. Agent 与确定性逻辑的分工

```mermaid
graph LR
    subgraph Det["确定性算法"]
        D1["地址 × 波特率穷举扫描"]
        D2["字序约束求解\nABCD/BADC/CDAB/DCBA"]
        D3["帧级质量统计\nCRC率/超时率/延迟分布"]
    end

    subgraph Agent["板载 AI Agent"]
        A1["下一步做什么实验？\n实验设计"]
        A2["现场人员该怎么办？\n表达"]
    end

    subgraph Rule["规则库（阶段2）"]
        R1["症状 → 归因\n决策表分类"]
    end

    D1 -->|"从站列表"| A1
    D2 -->|"字序/类型证据"| A1
    D3 -->|"帧统计证据"| A1
    D3 -->|"触发条件"| R1
    A1 --> A2
    R1 --> A2
```

---

## 4. 字序与数据类型约束求解流程

```mermaid
flowchart TD
    A["寄存器原始字节"] --> B["枚举4种字序\nABCD / BADC / CDAB / DCBA"]
    B --> C{"物理合理性筛选\nNaN / inf / 1e-38?"}
    C -->|"排除"| X1["❌ 排除该字序"]
    C -->|"通过"| D["连续多次采样"]
    D --> E{"时间连续性筛选\n解码序列是否平滑?"}
    E -->|"剧烈跳变"| X2["❌ 排除该字序"]
    E -->|"平滑"| F["联合数据类型判定\nint16/uint16/int32/float32"]
    F --> G["倍率推断\n值域量纲数量级"]
    G --> H{唯一解?}
    H -->|"是"| I["✅ 确定字序+类型+倍率"]
    H -->|"否"| J["列出候选项\n→ 屏幕确认"]
```

---

## 5. 主动诊断会话流程

```mermaid
flowchart TD
    A["帧统计：CRC错误率 / 超时率越阈"] --> B["生成 degraded 告警\n（本地，不依赖网络）"]
    B --> C{网络可用?}
    C -->|"否"| Z["展示原始帧统计\n阶段2后走规则库"]
    C -->|"是"| D["自动启动诊断会话"]
    D --> E["Agent 在只读沙箱内\n设计并执行探测实验"]
    E --> F["降速重试 → 隔离测试\n延长超时 → 换寄存器块 …"]
    F --> G{达到10轮上限?}
    G -->|"是"| H["强制收尾\nunresolved: true"]
    G -->|"否"| I{收敛?}
    I -->|"否"| E
    I -->|"是"| J["输出归因结论\n+ 置信描述"]
    J --> K["生成排查工单\n推送屏幕 + MQTT"]
    K --> L["LVGL 展示\n明确标注「AI 推测」"]
    L --> M["现场人员确认"]
    M --> N["本地代码执行处置动作"]
    H --> K
```

---

## 6. Agent 工具沙箱与权限边界

```mermaid
flowchart LR
    LLM["AI Agent\nReAct Loop"] --> GUARD["tool_guard.c\n沙箱检查"]
    GUARD --> CHECK1{"功能码\n只读?"}
    CHECK1 -->|"写功能码\n05/06/0F/10"| BLOCK["❌ 根本不存在\n无法调用"]
    CHECK1 -->|"只读\n01/02/03/04"| CHECK2{"参数越界?"}
    CHECK2 -->|"是"| REJECT["❌ 直接拒绝"]
    CHECK2 -->|"否"| CHECK3{"限流?"}
    CHECK3 -->|"超速"| REJECT2["❌ 拒绝"]
    CHECK3 -->|"正常"| CHECK4{"诊断模式\n激活?"}
    CHECK4 -->|"否"| REJECT3["❌ 工具集失效"]
    CHECK4 -->|"是"| EXEC["✅ 执行只读请求"]
    EXEC --> AUDIT["写入结构化\n审计日志"]
```

---

## 7. Agent 输出处置流程

```mermaid
flowchart TD
    A["Agent 归因结论\nJSON输出"] --> B["板端 schema 校验"]
    B --> C{校验通过?}
    C -->|"否"| ERR["展示错误原因\n允许重试\n写调试日志"]
    C -->|"是"| D["风险分级\nlow / medium / high"]
    D --> E["LVGL 展示\n明确标注「AI 推测」"]
    E --> F["现场人员确认"]
    F --> G["本地代码执行"]
    G --> H["写 events.jsonl\n记录来源: agent_suggestion"]
```

---

## 8. OTA 升级架构（XIP 约束下）

```mermaid
sequenceDiagram
    participant Broker as MQTT Broker
    participant App as 应用（QSPI XIP运行）
    participant eMMC as eMMC
    participant Stub as 片内Flash Boot Stub
    participant QSPI as QSPI NOR

    Broker->>App: OTA offer（分片）
    App->>eMMC: 写入新镜像 staging.img
    App->>eMMC: 校验 sha256 + 数字签名
    App->>App: 置升级标志，重启
    Note over App: 应用退出，无法再访问QSPI
    Stub->>eMMC: 读取 staging.img
    Stub->>Stub: 再次校验镜像
    Stub->>QSPI: 擦写新固件（片内执行，可自由操作QSPI）
    Stub->>Stub: 跳转至新固件
    App->>App: 新固件自检
    alt 自检通过
        App->>QSPI: mark confirmed
    else 自检失败
        Stub->>eMMC: 加载 rollback.img
        Stub->>QSPI: 回滚旧固件
    end
```

---

## 9. 启动顺序

```mermaid
flowchart TD
    A["上电"] --> B["最小硬件 / 日志 / 看门狗"]
    B --> C["eMMC + 文件系统"]
    C --> D{配置加载}
    D -->|"成功"| E["加载用户配置"]
    D -->|"失败/双槽损坏"| F["回退出厂默认\n写 error 事件"]
    E --> G["Modbus 采集 + 帧统计"]
    F --> G
    G --> H["本地告警规则"]
    H --> I["数字量输出\n进入安全默认态"]
    I --> J["LVGL UI"]
    J --> K["network_manager\n→ MQTT"]
    K --> L["ai_agent"]
    L --> M["系统就绪"]
```

---

## 10. 网络状态机

```mermaid
stateDiagram-v2
    [*] --> NET_DOWN
    NET_DOWN --> NET_CONNECTING : 触发连接尝试
    NET_CONNECTING --> NET_ONLINE_RJ45 : RJ45 连接成功
    NET_CONNECTING --> NET_ONLINE_WIFI : Wi-Fi 连接成功
    NET_CONNECTING --> NET_DOWN : 失败（指数退避）
    NET_ONLINE_RJ45 --> NET_DEGRADED : 链路质量下降
    NET_ONLINE_WIFI --> NET_DEGRADED : 链路质量下降
    NET_DEGRADED --> NET_ONLINE_RJ45 : RJ45 恢复
    NET_DEGRADED --> NET_ONLINE_WIFI : Wi-Fi 恢复
    NET_ONLINE_RJ45 --> NET_DOWN : 断线
    NET_ONLINE_WIFI --> NET_DOWN : 断线
    NET_ONLINE_WIFI --> NET_ONLINE_RJ45 : RJ45 恢复（稳定窗口后切回）
```

---

## 11. Modbus 通信状态机

```mermaid
stateDiagram-v2
    [*] --> online
    online --> degraded : 出现错误但未达离线阈值
    degraded --> offline : 连续失败超过离线判定
    offline --> recovering : 低频探测，出现成功响应
    recovering --> online : 连续成功确认
    recovering --> offline : 再次失败
    degraded --> online : 错误消除
```

---

## 12. HMI 页面结构

```mermaid
graph TD
    HOME["🏠 首页 / 总览\nNET状态 AI状态 时间\n各从站: 当前值 / 通信质量 / 告警等级"]
    HOME --> SLAVE["📊 从站详情\n当前值 / 通信质量\n点表 / 帧统计"]
    HOME --> TREND["📈 实时趋势"]
    HOME --> ALARM["🔔 告警详情"]
    ALARM --> TICKET["📋 诊断工单\n现象摘要 + 归因结论\nAI推测标注 + 探查过程回放"]
    HOME --> SCAN["🔍 总线探查"]
    SCAN --> PROGRESS["扫描进度与结果"]
    SCAN --> PREVIEW["点表预览\n寄存器地址/数据类型/字序/倍率"]
    SCAN --> CONFIRM["测试读取 → 确认"]
    HOME --> EVENTS["📜 事件日志"]
    HOME --> SYSINFO["⚙️ 系统状态"]
```

---

## 13. 存储布局

```mermaid
graph TD
    eMMC["eMMC 8GB\nFAT 文件系统"]

    eMMC --> DA["/data/agent/"]
    DA --> SK["skills/\nrs485_fault_triage.md"]
    DA --> CFG_AI["config/\nai_agent配置（含加密LLM key）"]
    DA --> SESS["sessions/"]

    eMMC --> DVG["/data/velaguard/"]
    DVG --> CFG_VG["config/\npoint_table_a.json (双槽+CRC+seq)\npoint_table_b.json\nnetwork.json / rules.json"]
    DVG --> LOGS["logs/\nlatest.log / debug.log\narchive/ / events.jsonl"]
    DVG --> DIAG["diagnosis/tickets/\n诊断工单归档"]
    DVG --> OTA_DIR["ota/\nstaging.img\nmanifest.json / rollback.img"]
```

---

## 14. 故障归因规则库（阶段2）

```mermaid
graph LR
    subgraph Symptoms["观测现象"]
        S1["全部从站不响应"]
        S2["单个从站不响应"]
        S3["CRC错随总线长度增加"]
        S4["错误集中在长帧"]
        S5["响应延迟抖动大"]
        S6["收到回声帧"]
        S7["帧间隔违规"]
        S8["仅某从站响应后出错"]
        S9["两地址交替失联"]
    end

    subgraph Causes["归因结论"]
        C1["接线/供电/全局波特率错"]
        C2["该从站地址错/掉线/损坏"]
        C3["终端电阻缺失或反射"]
        C4["波特率轻微失配或时钟漂移"]
        C5["从站忙或总线竞争"]
        C6["DIR时序错，半双工回环"]
        C7["主站发送过快，3.5T不足"]
        C8["该从站释放总线慢"]
        C9["地址冲突"]
    end

    S1 --> C1
    S2 --> C2
    S3 --> C3
    S4 --> C4
    S5 --> C5
    S6 --> C6
    S7 --> C7
    S8 --> C8
    S9 --> C9
```

---

## 15. 联网/离线能力边界

```mermaid
graph TD
    subgraph Always["永远本地（不依赖网络）"]
        L1["Modbus 周期采集"]
        L2["帧级质量统计\nCRC错误率/超时率/延迟分布/帧间隔违规"]
        L3["阈值/突变/离线告警"]
        L4["屏幕告警与状态显示"]
        L5["配置读写与事件日志"]
        L6["数字量输出安全默认态"]
    end

    subgraph Online["需要网络"]
        N1["Agent 诊断推理\n（LLM 在云端）"]
        N2["遥测与告警上云（MQTT）"]
        N3["OTA 固件升级"]
    end

    subgraph Degraded["断网降级"]
        D1["v1: 显示原始帧级统计"]
        D2["阶段2: 确定性规则库归因"]
    end

    N1 -->|"断网时"| Degraded
```

---

## 16. MQTT Topic 结构与 QoS

```mermaid
graph TD
    ROOT["vg/{device_id}/"]
    ROOT --> TEL["telemetry\nQoS 0 / retained: 否"]
    ROOT --> STATUS["status\nQoS 0 / retained: 是"]
    ROOT --> ALARM["alarm\nQoS 1 / retained: 否"]
    ROOT --> DIAG["diagnosis\nQoS 1 / retained: 否"]
    ROOT --> OTA["ota/"]
    OTA --> OTA1["offer\nQoS 1"]
    OTA --> OTA2["accept\nQoS 1"]
    OTA --> OTA3["chunk/request\nQoS 1"]
    OTA --> OTA4["chunk/data\nQoS 1"]
    OTA --> OTA5["progress\nQoS 1"]
    OTA --> OTA6["result\nQoS 1"]
    OTA --> OTA7["confirm\nQoS 1"]
```

---

## 17. 大赛验收要求对照

```mermaid
mindmap
  root((VelaGuard\n大赛评审))
    基础要求
      Agent在硬件上跑起来
        ai_agent移植到STM32H750B-DK
        Cortex-M7首例
      至少一个交互渠道
        CLI（官方明确CLI即可）
        LVGL现场HMI
      ≥1个自定义Skill
        rs485_fault_triage.md
        存放于/data/agent/skills/
      ≥1个主动+执行场景
        帧错误率越阈
        自动启动只读诊断
      完整场景说明
        用户故事/功能清单/技术实现
    加分项
      端云协作
        Bridge化LLM链路
        API key不落设备
        云侧热改prompt
      LVGL自定义UI
        工业风格HMI
        只读+确认设计
```