# VelaGuard 项目手册

> 版本：v3.1（2026-08-30 总线探查 stage1 落地口径）
> v2 将 Agent 定位为 485 排障实验设计者；v3 改为**运营助手**（告警解释 + 日报周报 + 自然语言查数）。
> v3.1：**阶段 1** 总线探查先以 NSH `vgdiscover` @9600 交付；LVGL「Scan Bus」**开关默认关闭**，须用户显式开启后才扫描。
> 采集、告警、统计仍由本地确定性逻辑完成；Agent 负责把数据讲给人听。
> 被推翻的决策及理由记录在 `VelaGuard_推进方案.md` 第 2 章。

---

## 1. 项目定位

### 1.1 一句话定位

VelaGuard 是一台运行在 STM32H750B-DK + openvela 上的 **RS485/Modbus 现场网关**。它的核心不是采集转发，而是**把已采集的数据讲给人听**：采集、告警与统计全部由本地确定性逻辑完成；板载 AI Agent 在告警产生时自动解释含义，定时生成运行日报/周报，并支持自然语言查询实时读数。

### 1.2 要解决的真问题

把一台设备接到一条陌生的 485 总线上，工程师面对的是一串没有说明书的问题：

- 总线上有几个从站？各自什么地址、什么波特率？
- 某个寄存器读出来是 `0x41C8 0x0000`，这是 int32 还是 float32？字序是 ABCD 还是 CDAB？倍率是 0.1 还是 1？
- 通信时好时坏，是终端电阻没装、A/B 反接、波特率轻微失配、还是某个从站释放总线太慢？
- 告警弹出后，这条告警对人意味着什么？该先看什么？
- 今天/本周设备运行概况如何？哪些点位异常最多？

前两类问题靠确定性扫描与约束求解；第三类靠规则库（阶段 2）；后两类由 Agent 在联网时用自然语言解释与汇总——**Agent 不做总线排障实验**。

### 1.3 目标用户

**和作者同类的人**：需要在现场或实验室接入陌生 Modbus 设备的嵌入式工程师、自动化调试人员、系统集成人员。

明确**不面向**产线操作工和不懂总线的运维——那需要产线现场经验来验证需求，作者不具备，硬做会做成想象中的产品。

### 1.4 核心价值

| 能力 | 现状做法 | VelaGuard |
|---|---|---|
| 发现从站 | 手工试地址和波特率组合 | 自动扫描地址 × 波特率矩阵 |
| 识别数据格式 | 人肉试四种字序，看哪个像 | 物理合理性 + 时间连续性约束求解 |
| 定位通信故障 | 老师傅经验 + 反复试 | 确定性规则库（阶段 2，不依赖 Agent） |
| 理解告警含义 | 看原始数值自己猜 | Agent 自动解释（标注 AI 推测） |
| 运行概况 | 翻日志 / 手工统计 | Agent 定时生成日报/周报 |
| 查当前读数 | 打开组态或串口助手 | 自然语言提问（CLI/LVGL） |
| 生成点表 | 手写 JSON 或组态软件 | 探测结果直接生成，屏幕预览后确认 |

### 1.5 openvela 能力落点

| openvela 能力 | 使用方式 |
|---|---|
| ai_agent 框架 | 板载 ReAct 循环、自定义 C 工具、Markdown Skill、主动任务 |
| LVGL 图形栈 | 现场 HMI（只读 + 确认） |
| NuttX 串口子系统 | RS485 半双工 DIR 时序（含驱动级修复） |
| NuttX 网络栈 | RJ45 + ESP-01 双栈、MQTT、mbedTLS |
| NuttX 块设备与文件系统 | eMMC bring-up、掉电安全配置存储 |
| OTA / bootctl | 片内 bootstub 从 eMMC 烧写 QSPI |

---

## 2. 产品边界

### 2.1 系统形态

独立网关，不依赖长期连接电脑运行。

```text
Modbus RTU 从站（温湿度 / 电表 / 流量计 …）
  ↓ RS485 半双工
STM32H750B-DK + openvela
  ├─ 板载 ai_agent（ReAct 循环 + Modbus 只读工具集）
  ├─ LVGL 现场 HMI（只读 + 确认）
  └─ RJ45 Ethernet（主） / ESP-01 Wi-Fi（备）
       ↓ HTTPS                    ↓ MQTT
     云端 LLM（MiMo）           MQTT Broker（遥测 / 告警 / OTA）
```

USB CDC / UART 只作为开发调试和救援通道，不是正式运行链路。

### 2.2 联网与离线的分界线

**永远本地、永远不依赖网络**：

- Modbus 周期采集
- 帧级质量统计（CRC 错误率、超时率、响应延迟分布、帧间隔违规）
- 阈值 / 突变 / 离线告警
- 屏幕告警与状态显示
- 配置读写与事件日志
- 数字量输出的安全默认态

**需要网络**：

- Agent 的告警解释、日报/周报与自然语言润色（LLM 在云端）
- 遥测与告警上云
- OTA

**断网时的诚实口径**：采集、告警、统计、日志全部照常。Agent 能力降级：告警详情展示规则引擎原始信息（触发条件、当前值、时间）；不生成 AI 解释与报告；自然语言查数可降级为结构化数值展示。网络恢复后可补跑 pending 的报告任务。

### 2.3 Agent 的权限边界

这是本项目最重要的安全设计。一个能对活着的工业总线下指令的 LLM 是危险品。

**工具层硬约束**（在 C 代码的工具注册层实现，不依赖 prompt 约束）：

- 工具集只读：查询实时值、告警上下文、事件与统计摘要，**不包含** Modbus 写功能码或总线探测实验类操作
- 从站地址、时间窗口、查询点位数量均有硬上界，越界直接拒绝
- 单位时间请求数限流，防止 agent 把总线或存储打满
- 按会话类型门控（告警解释 / 报告生成 / 交互查询），非活跃会话工具集失效
- 每次工具调用写入结构化审计日志

**Agent 输出的处置边界**：

Agent 只产出**解释与建议**，不直接改配置、不写寄存器、不清除告警。任何处置需人工确认后由本地代码执行。

```text
Agent 解释 / 报告
→ 板端 schema 校验
→ LVGL 展示（告警解释标注「AI 推测」）
→ 现场人员确认或归档
```

**Agent 不可以**：写任何寄存器、修改采集配置、控制数字量输出、清除告警、覆盖本地安全规则。

### 2.4 明确不做的事

| 不做 | 理由 |
|---|---|
| 语音输入输出（ASR/TTS） | 工控现场噪声环境下伪需求；官方明确 CLI 即可满足交互渠道要求；WM8994 codec 树内无驱动 |
| 本地音频告警 | 同上，WM8994 需自写驱动；改用 LED + 屏幕告警 |
| 传感器手册上传与解析 | 需要云端 OCR/文档解析服务，与嵌入式主线无关 |
| 屏上编辑配置 | 现场人员需要「看」和「确认」，配置创作是办公室的事；屏上编辑意味着软键盘、输入校验、误操作回退，是无底洞 |
| 自然语言创作点表 | 被自动扫描探测取代——后者更准、更快、不依赖网络 |
| 板端跑本地大模型 | H750 上跑不动够格的模型，不自欺 |
| AI 直接控制执行器 | 工业安全红线 |

---

## 3. 硬件资源（实测基线）

> 以下数字来自 `nuttx/boards/arm/stm32h7/stm32h750b-dk/` 与 2026-08-18 构建产物，
> 不是数据手册标称值。板级 README 写「128MB SDRAM」是颗粒容量，实际布线只有一半可访问。

### 3.1 存储与内存

| 资源 | 实际容量 | 现状 |
|---|---|---|
| 片内 Flash | 128 KB @ `0x08000000` | 仅存 QSPI boot stub（720 B），余量 ~127 KB |
| QSPI NOR (MT25QL512ABB) | 256 Mbit ≈ 32 MB | **主固件 XIP 执行**于 `0x90000000`，当前占 ~220 KB |
| 外部 SDRAM | **8 MB**（16-bit 走线，非 128 MB） | FMC Bank6 `0xD0000000`，首 1 MB 预留，7 MB 已入堆 |
| 片内 SRAM | ~1 MB | AXI 512 KB / SRAM1-3 288 KB / SRAM4 64 KB / DTCM 128 KB / ITCM 64 KB |
| eMMC | 8 GB | SDMMC 接口，**默认未启用，板级驱动未实现** |
| microSD | 卡槽 | SDMMC 接口，默认未启用；**本项目无卡，不使用** |

**堆预算**：AXI 主堆约 450 KB（已吃满 AXI 剩余空间），扩展区 SRAM1-3 + SRAM4 + DTCM 约 464 KB，SDRAM 约 7 MB，合计约 **7.9 MB**。DTCM 不适合 DMA 缓冲。

**关键约束**：固件从 QSPI XIP 执行，因此**运行期不能擦写 QSPI**——擦写必须退出 memory-mapped 模式，那一刻任何从 `0x90000000` 取指的代码（含中断服务程序）会当场失效。这直接决定了 OTA 的架构（见 §8.4）。

### 3.2 外设

| 外设 | 状态 | 用途 |
|---|---|---|
| 4.3" 480×272 RGB LCD (LTDC) | 可用，有本地显示加速补丁 | 现场 HMI |
| FT5x06 电容触摸 (I2C4) | 可用 | 确认操作 |
| LED | 可用 | 状态 / 告警指示 |
| UART + RS485 收发器 | 可用（扩展板已焊接） | Modbus RTU 主站 |
| RJ45 Ethernet | 可用 | 主网络 |
| ESP-01 Wi-Fi (UART AT) | 已实现 | 备用网络 |
| WM8994 音频 codec | **树内无驱动** | 不使用 |
| eMMC / microSD (SDMMC) | **需自写板级驱动** | 持久化存储 |

扩展板特性：**120 Ω 终端电阻为跳线可切**。这使得终端电阻缺失、波特率失配、地址冲突、从站离线、帧间隔违规等五类故障可以用真实硬件复现，**规则库**的诊断能力因此可验证。

### 3.3 当前软件基线

| 组件 | 状态 |
|---|---|
| defconfig | `stm32h750b-dk:velaguard-lvgl`（`scripts/build.sh` 默认作品主线） |
| 固件体积 | 以当前 `.debug` 构建为准 |
| 网络栈 | 已启用（TCP/UDP/DHCP/DNS/Ethernet MII） |
| MQTT | MQTT-C 已启用 |
| mbedTLS | 已启用（Agent HTTPS） |
| littlefs | 不作为产品主存储（eMMC FAT：`/data/velaguard`） |
| LVGL / LTDC | 已启用（人机界面 + 触摸） |

---

## 4. 总体架构

### 4.1 分层

```text
Application
  hmi_app              LVGL 现场 HMI（只读 + 确认）
  agent_app            告警解释 / 报告生成 / 交互查询会话编排

ai_agent（openvela packages/ai_agent）
  ReAct loop           多轮工具调用（上限 10 轮）
  tool_registry        自定义 C 工具注册（读状态 / 读历史）
  skills               /data/agent/skills/*.md
  proactive task       事件主动（告警解释）+ 定时主动（日报/周报）
  llm_proxy            OpenAI 兼容 HTTPS

VelaGuard Service
  modbus_collector     周期采集
  bus_prober           扫描 / 块探测 / 字序求解（确定性）
  frame_stats          帧级质量统计
  rule_engine          阈值 / 突变 / 离线告警
  diag_rules           故障归因规则库（阶段 2，确定性）
  agent_tools          只读查询工具 + 沙箱
  report_store         日报/周报存储
  point_table          点表存储与热加载
  config_store         双槽掉电安全配置
  event_store          结构化事件日志
  network_manager      RJ45 / ESP-01 单活动链路
  mqtt_client          遥测 / 告警 / OTA
  ota_service          镜像下载与暂存（烧写由 bootstub 执行）

openvela / NuttX
  task / pthread / VFS / FAT / SDMMC / sockets / mbedTLS
  UART(RS485) / Ethernet / LTDC / Touch / GPIO
```

### 4.2 Agent 与确定性逻辑的分工

这是本项目的架构核心，必须分清楚：

| 问题类型 | 性质 | 由谁做 |
|---|---|---|
| 这条总线上有哪些从站？ | 穷举搜索 | 确定性算法 |
| 这几个寄存器是什么数据类型和字序？ | 约束求解 | 确定性算法 |
| 当前链路质量如何？ | 统计 | 确定性算法 |
| 链路故障归因 | 分类 | 规则库（阶段 2，确定性） |
| **这条告警对人意味着什么？** | **解释** | **Agent（告警解释）** |
| **运行概况如何？** | **汇总** | **Agent（日报/周报）** |
| **当前读数是多少？（自然语言问）** | **查询** | **Agent + 只读工具** |

**Agent 不负责排障实验设计**；485 归因由规则库本地完成。Agent 的价值在于把告警、事件与采样数据组织成现场人员读得懂的话。

### 4.3 字序与数据类型的约束求解

不使用 LLM。这是个约束满足问题，算法比 LLM 可靠得多，因为它给的是证据而不是统计先验：

1. 对候选排列 ABCD / BADC / CDAB / DCBA 分别解码
2. **物理合理性筛选**：float32 解出 NaN / inf / 1e-38 量级直接排除；结合寄存器语义猜测的量纲范围筛选
3. **时间连续性筛选**：连续多次采样，真实物理量变化平滑，错误字序解出的序列会剧烈跳变
4. 数据类型（int16 / uint16 / int32 / float32）由寄存器数量、值域、有无负数、变化平滑度共同判定
5. 倍率由值域相对典型量纲的数量级推断，供人确认

两个约束联合通常唯一确定解。无法唯一确定时列出候选，交屏幕确认。

---

## 5. 功能模块

### 5.1 总线自动探查（招牌功能，确定性）

#### 目标能力（完整版）

**地址与波特率扫描**

- 波特率候选：9600 / 19200 / 38400 / 57600 / 115200（可配）
- 从站地址：1–247（可配范围）
- 对每个组合发送最小请求，按响应与 CRC 判定存活
- 扫描期间严格遵守 3.5 字符帧间隔，不打满总线
- 输出存活从站列表及其通信参数

**寄存器块探测**

- 对每个存活从站，按块试读 Holding / Input Register
- 记录哪些地址段可读、哪些返回异常码
- 输出可读寄存器区间图

**点表生成与确认**

- 探测结果 + 字序求解 → 候选点表
- LVGL 展示点表预览（寄存器地址、推断的数据类型、字序、倍率、当前解码值）
- 「测试读取」按钮触发一次实读，展示结果
- 现场人员确认后点表落盘并进入采集循环

#### 阶段 1 现状（2026-08-30）

| 项 | 口径 |
|---|---|
| 入口 | NSH `vgdiscover`（`scan` / `probe` / `dump` / `test-read` / `apply`） |
| 波特率 | **固定 9600**（`VG_DISCOVER_BAUD`）；不做波特率矩阵 |
| 地址扫描 | 默认 1–32；试探 **FC03 @ holding reg0 qty=2** |
| 块探测 | FC03/FC04 块步进；qty 16→2→1 回退（兼容 MThings mock 窄寄存器） |
| 点表 | 写 `/data/velaguard/discover/point_table_candidate.json`；`apply --confirm` 才进 config slot |
| LVGL | **尚未接线**；总线探查页与首页「Scan Bus」在 `stage1-lvgl-hmi` 实现 |

**已知限制（暂接受，后续迭代）**

- 仅 reg0 探活会漏掉首点不在 reg0 的从站（如 MThings 水浸 addr=2，首点在 reg2）
- MThings 32 从站 mock 实测约 **14/32** 命中（1–16 段为主）；17–32 与 reg 偏移问题留阶段 1 后期或阶段 2
- 类型推断为 int16×0.1 启发式，须 test-read + 人工确认

#### UI 策略：扫描开关默认关闭

总线扫描会占用 RS485 并打断正常采集，**不得在上电或进入 HMI 时自动启动**。

- LVGL「总线探查」页（及可选「设置 → 高级」）提供 **「启用总线扫描」开关**
- **出厂 / 默认：关闭** — 网关只做已确认点表的周期采集
- 用户显式打开开关后，才允许发起 scan / probe 流程；关闭后立即停止扫描 UI 状态机（已在途的一次 probe 可跑完或取消，实现时二选一并在 HMI 注明）
- NSH `vgdiscover` 不受此开关约束（维护 / 实验室 bring-up 用）

与 §2.3 一致：Agent **不参与**扫描；扫描全程本地 C，断网可用。

### 5.2 Modbus 采集

基于 nanoMODBUS 实现 RTU 主站，`modbus_port_openvela.c` 提供 transport 回调。

- 周期读取 Holding / Input Register，支持多点位
- 倍率、偏移、单位换算
- 失败重试与离线判定
- 采样结果分发给规则引擎、UI、帧统计与 MQTT

**RS485 方向控制**：优先使用 NuttX 的 `TIOCSRS485` 驱动级 DIR 控制（基于 TC 中断），而非应用层延时。当前 NuttX 的 `tcdrain()` 不等待发送完成（TC）标志，会导致半双工提前切向而截断帧尾——这是 POSIX 语义偏离，应在驱动层修复并向上游提交 PR（见 §14.2）。

### 5.3 帧级质量统计

告警与规则库的证据来源。按从站分别统计：

- CRC 错误率
- 响应超时率
- 响应延迟分布（min / p50 / p95 / max）
- 帧间隔违规次数
- 回声帧检测（收到自己发出的字节，指示 DIR 时序问题）
- 异常码分布

统计窗口滑动，可导出为 Agent 工具与规则库的输入。

### 5.4 板载 AI Agent（运营助手）

基于 openvela `packages/ai_agent`。Agent **不负责**总线排障或寄存器探测实验；只做告警解释、运行报告与自然语言查数。

**自定义 C 工具集**（全部只读，查询已采集数据）：

| 工具 | 作用 |
|---|---|
| `get_live_values(slave, tags[])` | 当前采样值 |
| `get_alarm_context(alarm_id)` | 告警实例、触发规则、关联点位 |
| `get_events(since, until, filter)` | 结构化事件 |
| `get_telemetry_summary(window, slaves[])` | 窗口内 min/max/avg、离线时长 |
| `get_frame_stats(slave, window)` | 通信质量摘要 |
| `get_point_table()` | 点名、单位、阈值 |

所有工具受 §2.3 的沙箱约束。

**Skill**（Markdown，存放于 `/data/agent/skills/`）：

- `alarm_interpretation.md` — 告警解释：如何组织证据、如何把规则触发翻译成人话
- `operations_report.md` — 日报/周报：结构、指标口径、异常写法

**LLM 后端**：OpenAI 兼容 HTTPS。可直连 MiMo，或经自建 Bridge（后续增强）。

#### 5.4.1 自然语言查实时数据（交互渠道）

用户通过 CLI（`vela>`）或 LVGL 提问，例如「5 号从站流量怎么样？」。Agent 解析意图 → 调用 `get_live_values` / `get_point_table` → 用自然语言回答并附原始数值。**满足大赛交互渠道要求**；赛道书面说明的主场景为 §5.5–5.6 的主动能力。

### 5.5 主动告警解释（事件主动 + 执行）

对应大赛「**事件主动**」。

```text
规则引擎产生告警（阈值 / 离线 / 链路劣化 …）
→ 本地告警与 UI 展示（不依赖网络）
→ 若网络可用：自动启动告警解释会话
→ Agent 调用 get_alarm_context / get_live_values / get_events 等
→ 输出结构化解释（summary、evidence、suggested_attention）
→ LVGL 告警详情页（标注「AI 推测」）+ MQTT 附摘要
→ 不自动清除告警、不改配置
```

断网时：仅展示规则引擎原始告警信息，不生成 AI 解释。

### 5.6 定时日报 / 周报（定时主动 + 执行）

对应大赛「**定时主动**」。

```text
定时器到点（如每日 08:00；周报如每周一 08:00，可配置）
→ Agent 汇总窗口内告警、关键指标、通信质量、notable 事件
→ 生成报告写入 /data/velaguard/reports/
→ LVGL「报告」页预览 + 可选 MQTT + 事件日志
```

9/20 里程碑：**日报必做，周报可后补。**

### 5.7 规则引擎与告警

不依赖网络的本地实时判断：

- 超阈值 / 低于阈值（触发与恢复均带持续时间窗口）
- 数值突变（窗口差值 + 持续时间）
- 通信离线
- 链路劣化（帧统计越阈）
- 恢复正常

告警模型见 §16.6。

### 5.8 故障归因规则库（阶段 2）

**485 链路故障的确定性归因路径**，不依赖 Agent 或网络。485 故障模式可枚举，写成决策表：

| 现象 | 归因 |
|---|---|
| 全部从站不响应 | 接线 / 供电 / 全局波特率错 |
| 单个从站不响应 | 该从站地址错、掉线或损坏 |
| CRC 错随总线长度增加 | 终端电阻缺失或反射 |
| 错误集中在长帧 | 波特率轻微失配或时钟漂移 |
| 响应延迟抖动大 | 从站忙或总线竞争 |
| 收到回声帧 | DIR 时序错，半双工回环 |
| 帧间隔违规 | 主站发送过快，3.5T 不足 |
| 仅在某从站响应后出错 | 该从站释放总线慢 |
| 两个地址交替失联 | 地址冲突 |

规则库可单元测试，且扩展板的跳线、可配波特率、多从站使其中五类可用真实硬件复现验证。

### 5.9 点表与配置存储

点表、网络配置、告警规则以 JSON 存储于 eMMC。**FAT 不是掉电安全的**，因此配置写入必须自行实现原子提交：双份互备 + CRC + 单调递增序号，启动时选 `seq` 最大且校验通过的一份，两份皆坏则回退出厂默认。详见 §16.8。

### 5.10 事件日志

结构化事件写入 `events.jsonl`，人类可读日志滚动写入 `latest.log`。详见 §16.7。

至少记录：告警产生与恢复、Agent 会话（告警解释/报告生成，含工具审计）、配置变更及来源、用户确认、网络与 OTA 事件。

---

## 6. HMI 设计

### 6.1 设计原则

**只读 + 确认，屏上不做任何编辑。**

这不是妥协，而是符合现场实际：现场人员需要「看」和「确认」，配置创作是工程师在办公室干的事。屏上编辑意味着软键盘、输入校验、状态机、误操作回退，工作量差两三倍且价值可疑。

其余原则：

- 工业风格，信息密度适中，首页一眼看出设备是否正常
- 告警和报告比配置入口更突出
- 状态色统一：绿正常 / 黄预警 / 红告警 / 灰离线 / 蓝联网
- 「AI 推测」与「确定性结论」在视觉上必须区分
- 触摸控件尺寸适配 4.3 寸屏

### 6.2 页面结构

```text
首页 / 总览
  ├─ 从站详情（当前值、通信质量、点表、帧统计）
  ├─ 实时趋势
  ├─ 告警详情
  │   └─ AI 告警解释（含 AI 推测标注 + 证据列表）
  ├─ 运行报告（日报 / 周报预览）
  ├─ 总线探查
  │   ├─ 扫描进度与结果
  │   ├─ 点表预览
  │   └─ 测试读取与确认
  ├─ 事件日志
  └─ 系统状态
```

### 6.3 首页

```text
┌────────────────────────────────────┐
│ VelaGuard   NET OK  AI OK   10:30  │
├────────────────────────────────────┤
│ Slave 03  Cooling Pump             │
│ Temp 82.4 C          WARNING       │
│ CRC 0.2%  Timeout 0%   2s ago      │
├────────────────────────────────────┤
│ Slave 05  Flow Meter               │
│ Flow 12.3 m3/h       DEGRADED      │
│ CRC 31.4%  Timeout 8%  1s ago      │
├────────────────────────────────────┤
│ Alarms: 2      Highest: Warning    │
├───────────┬───────────┬────────────┤
│  Details  │  Reports  │  Scan Bus  │
└───────────┴───────────┴────────────┘
```

### 6.4 告警解释页

必须显示：现象摘要（规则引擎）、**AI 解释**（标注「AI 推测」）、证据列表、建议关注项、关联点位当前值。

按钮：确认已知、标记已处理。**不提供任何「让 AI 直接修复」的入口。**

### 6.5 运行报告页

展示最新日报/周报：告警摘要、关键指标、通信质量、生成时间。支持历史报告列表。

### 6.6 总线探查页

**扫描开关（默认关）**：页顶或设置项「启用总线扫描」— **默认关闭**；仅当用户打开后才显示「开始扫描」并占用 RS485。避免上电自动扫总线影响已投运采集。

开启后流程：扫描进度 → 存活从站列表 → 点表预览（寄存器地址、推断数据类型、字序、倍率、当前解码值、候选项）→ 测试读取 → 确认 / 返回 / 放弃。

板测 / 维护仍可用 NSH：`vgdiscover scan -a 1-32`（固定 9600）。

---

## 7. 典型场景

### 7.1 场景一：接入一条陌生总线

接上 485 → 在 LVGL 打开「启用总线扫描」（默认关）→ 点「Scan Bus」→ 网关 @9600 扫描地址 1–32，发现存活从站 → 逐个探测寄存器块 → 约束求解推断类型与倍率 → 屏幕出点表预览 → 点「测试读取」→ 现场人员确认 → 关闭扫描开关 → 曲线开始跑。

### 7.2 场景二：告警自动解释

从站 3 温度超阈值触发 WARNING → 本地告警弹出 → 若联网，Agent 自动解释：「当前 82.4°C，高于阈值 80°C 已持续 2 分钟；同从站流量正常，建议检查冷却回路而非总线通信」→ 告警详情页展示 AI 解释 + MQTT 上报摘要。

### 7.3 场景三：日报与断网

每日 8:00 自动生成日报，汇总 24h 告警与关键指标。断网时：采集、告警、统计照常；无 AI 解释与报告生成；恢复网络后补跑 pending 报告。CLI 仍可问「当前最高告警是什么？」——降级为结构化数值回答。

---

## 8. 网络与云端

### 8.1 双栈网络

`network_manager` 统一管理 RJ45 与 ESP-01，**单活动链路**，RJ45 优先，恢复后需稳定窗口再切回。状态：`NET_DOWN` / `NET_CONNECTING` / `NET_ONLINE_RJ45` / `NET_ONLINE_WIFI` / `NET_DEGRADED`。

重连采用指数退避：初始 1 s，倍率 2，上限 60 s，抖动 ±20%，稳定在线 5 分钟后重置。RJ45 与 Wi-Fi 分别维护退避状态；RJ45 物理 link 恢复可立即触发一次尝试。

**本模块已基本建成，进入冻结状态**：只做必要维护，不再投入优化。

### 8.2 LLM 链路

板载 ai_agent 通过 HTTPS 调用 OpenAI 兼容端点。两种落法：

- **直连 MiMo**：`llm_proxy` 内置 `mimo` 预设（`api.xiaomimimo.com`），最简
- **经自建 Bridge**：Bridge 暴露 `/v1/chat/completions` 透传端点。优点是 API key 不落设备、prompt 与模型可云侧热改无需烧板、日志集中；且可演化为「端云协作」加分项

v1 先直连打通，Bridge 化作为后续增强。

### 8.3 MQTT

承载遥测、告警、状态与 OTA 控制面。**不再承载 AI 请求响应、TTS 与语音分片**（这些已从项目中移除）。

```text
vg/{device_id}/telemetry
vg/{device_id}/alarm
vg/{device_id}/diagnosis          告警解释 / 报告摘要上报
vg/{device_id}/status             retained
vg/{device_id}/ota/offer
vg/{device_id}/ota/accept
vg/{device_id}/ota/chunk/request
vg/{device_id}/ota/chunk/data
vg/{device_id}/ota/progress
vg/{device_id}/ota/result
vg/{device_id}/ota/confirm
```

QoS 与 retained 策略见 §16.2。

### 8.4 OTA 架构（因 XIP 而重新设计）

固件从 QSPI XIP 执行，运行期无法擦写 QSPI。因此**不能**在应用态直接写固件。正确形态是利用片内 Flash 中的 boot stub：

```text
应用通过 MQTT 分片下载新镜像
→ 写入 eMMC（不是 QSPI）
→ 校验 sha256 + 数字签名
→ 置升级标志，重启
→ 片内 Flash 的 boot stub（从片内执行，可自由擦写 QSPI）
   校验镜像 → 擦写 QSPI → 跳转
→ 新固件自检通过后 confirm
→ 自检失败：从 eMMC 上保留的旧镜像回滚
```

片内 128 KB Flash 当前只用了 720 B，装下这个 stub 绰绰有余；`frameworks/system/ota/` 的 bootctl 与 verify 可复用。

**这是一个独立的 bootloader 工程，且硬依赖 eMMC 先跑通**，因此排在最后阶段。

其余 OTA 约束见 §16.9。

---

## 9. 存储布局

存储介质为板载 **8 GB eMMC**（SDMMC 接口，需自写板级驱动）。eMMC 是块设备而非 MTD，文件系统采用 FAT。

```text
/data/agent/
  skills/
    alarm_interpretation.md      告警解释 Skill
    operations_report.md         日报/周报 Skill
  config/                        ai_agent 自身配置（含加密后的 LLM key）
  sessions/

/data/velaguard/
  config/
    point_table_a.json           双槽 + CRC + seq
    point_table_b.json
    network.json
    rules.json
  logs/
    latest.log
    debug.log
    archive/
    events.jsonl
  reports/                       日报/周报归档
  ota/
    staging.img
    manifest.json
    rollback.img
```

**Skill 文件的开发期迭代**：本项目无 SD 卡，无法拔卡编辑。使用 PC 上的临时 HTTP 服务 + 板端 `wget` 拉取 Markdown 到 `/data/agent/skills/`（`NETUTILS_WEBCLIENT` 本就是 ai_agent 的依赖）。官方另提供 `com.agent.coapp` 安卓 App 可在局域网内推送技能，作为备选。

---

## 10. Skill 设计

### 10.1 `alarm_interpretation.md`

存放路径：`/data/agent/skills/alarm_interpretation.md`

**用途**：告诉 Agent 如何把规则引擎产生的告警翻译为现场人员读得懂的话。

**内容骨架**：

1. 任务目标与安全约束（只读查询、不得建议写寄存器或自动处置）
2. 可用工具清单
3. 如何组织证据（当前值、近期趋势、同从站其他点、近期事件）
4. 常见告警类型的解释模板（超阈值、离线、链路劣化）
5. 何时承认信息不足（`unresolved: true`）
6. 输出格式约定

**输出结构**：

```json
{
  "alarm_id": "",
  "summary": "",
  "evidence": [],
  "related_points": [],
  "suggested_attention": [],
  "source": "agent",
  "unresolved": false
}
```

### 10.2 `operations_report.md`

存放路径：`/data/agent/skills/operations_report.md`

**用途**：日报/周报的章节结构、指标口径与异常写法。

**输出结构**：

```json
{
  "report_type": "daily|weekly",
  "period_start_ms": 0,
  "period_end_ms": 0,
  "sections": [],
  "source": "agent"
}
```

### 10.3 已移除的 Skill

- `rs485_fault_triage.md` — v3 不再做 Agent 排障
- `sensor_config_generator.md` — v2 已移除

---

## 11. 安全设计

### 11.1 Agent 工具沙箱

见 §2.3。这是本项目最有工程含量的安全设计，也是答辩的核心论点之一：**给 LLM 划一个碰不到写操作的边界**，而且边界在代码结构上成立，不依赖 prompt 约束。

openvela ai_agent 的 `run_shell` 工具本身就有白名单 / Full 双模式设计，本项目的 Modbus 工具沙箱与之同源。

注意：`tool_guard.c` 只做禁用检查、大小检查和限流，**不做 JSON Schema 运行时校验**，参数越界必须在自定义工具内部自行拦截。

### 11.2 操作安全

- AI 只生成建议，不直接执行
- 点表变更必须经测试读取 + 人工确认
- 数字量输出有安全默认态，上电与故障时进入该状态
- 高风险操作长按确认
- 所有变更写入结构化事件并记录来源

### 11.3 数据安全

需保护：LLM API key、MQTT token、网络配置、点表、诊断记录、OTA 签名公钥与回滚状态。

Key 不在 UI、日志、串口输出或 MQTT payload 中出现完整值。

---

## 12. 异常与降级

| 故障 | 系统行为 |
|---|---|
| 网络不可用 | 采集 / 统计 / 告警 / 日志全部继续；无 AI 解释与报告；指数退避重连 |
| LLM 不可用或超时 | 告警解释/报告会话标记失败，展示规则引擎原始信息；不影响本地功能 |
| Agent 输出非法 JSON | schema 校验拒绝，展示错误原因，允许重试，写调试日志 |
| Agent 解释信息不足 | 输出 `unresolved: true`，仅展示已有证据 |
| Modbus 读取失败 | 重试 → 标记通信质量 → 超阈值生成 degraded / offline 告警 |
| eMMC 故障 | 配置回退到内存中的最后一份有效副本；告警提示存储异常；禁止 OTA |
| 配置双槽皆损坏 | 回退出厂默认，写 error 事件，UI 明确提示 |
| OTA 各类失败 | 保持当前固件运行；标记 staging 无效；不影响采集、告警、UI、日志 |

---

## 13. 交付物

- openvela 应用源码（`app/velaguard/`）
- ai_agent 的 STM32H750B-DK defconfig 与必要的 `fix_*.sh` 补丁脚本
- Agent 只读查询工具集与沙箱
- `alarm_interpretation.md`、`operations_report.md` Skill
- LVGL 现场 HMI
- 板级 `stm32_sdmmc.c`（eMMC bring-up）
- 上游 PR：NuttX RS485 `tcdrain` / DIR 时序修复
- 上游 PR：ai_agent 新增开发板支持
- README（产品说明 + 可复现的构建运行步骤）
- 架构图、接线说明
- 用户故事 / 功能清单 / openvela 能力使用说明
- 演示视频（≤ 5 分钟）
- AI Coding 日志

---

## 14. 验收标准

### 14.1 功能

- H750B-DK 不依赖电脑独立运行
- 接入陌生 485 总线后，**用户开启扫描开关**并能扫描出存活从站（阶段 1：9600；完整版含波特率矩阵）
- 能探测寄存器块并推断数据类型与字序，屏幕出点表预览
- 测试读取通过后确认，点表落盘并进入采集
- 帧级统计可见（CRC 率、超时率、延迟分布、帧间隔违规）
- 告警产生时**自动**启动 Agent 解释会话（联网时），无需人工干预
- 定时生成日报（周报可后补），落盘并在屏幕预览
- 自然语言查询实时读数（CLI 或 LVGL）
- 告警解释标注「AI 推测」；规则库归因（阶段 2）标注「规则判定」
- 配置掉电不丢失，双槽任一损坏可恢复
- 断网时采集、统计、告警、日志全部正常，Agent 能力按 §2.2 降级
- Agent 无法绕过确认修改任何配置或写任何寄存器

### 14.2 Agent 运营助手的可证伪判据

**不是演示一次成功，而是一个可能失败的数字**：

> 在 N 次人工注入的告警（覆盖超阈值、离线至少 2 类）中，记录 Agent 告警解释被人工判定「现象描述正确且建议关注项合理」的比例；在 M 份自动生成的日报中，记录「关键指标与 events.jsonl 一致且无编造点位」的比例。全过程写操作触发次数 = 0。

目标：**告警解释 ≥ 70% 合理；日报 ≥ 90% 指标一致；写操作 = 0。**

### 14.3 大赛赛道要求对照

| 官方要求 | 本项目落点 |
|---|---|
| Agent 在硬件设备上跑起来 | ai_agent 移植到 STM32H750B-DK（Cortex-M7 首例） |
| 至少接入一个交互渠道 | CLI（`vela>` 自然语言查数）+ LVGL |
| ≥ 1 个自定义 Skill | `alarm_interpretation.md` + `operations_report.md` |
| ≥ 1 个「主动 + 执行」场景 | **事件主动**：告警自动解释；**定时主动**：日报/周报 |
| 完整场景说明 | 用户故事 / 功能清单 / 技术实现 |
| 加分：端云协作 | Bridge 化的 LLM 链路（后续增强） |
| 加分：LVGL 自定义 UI | 现场 HMI |

> 官方来源：`docs/zh-cn/contest_2026/ai_hardware/ai_hardware_track_guide.md` 与 `ai_agent_quickstart.md`（repo 管理的公共树，分支 `dev-ai-contest-2026`）。本地任何摘要文档均不具权威性，以官方文本为准。

---

## 15. 待确认的前置阻塞项

以下问题无法通过读文档解决，需向组委会确认。**在答复之前不启动 ai_agent 移植的正式开发**（20 小时探针除外）：

1. **STM32H750B-DK 是否属于 AI 硬件赛道的「指定硬件」？** 赛道指引写「烧录到指定硬件」，quickstart 写「具体型号待补充」，而 ai_agent 现有 defconfig 仅覆盖 goldfish 模拟器（arm64）、Gemini-S1（Cortex-A7）、ESP32-S3（Xtensa），无任何 Cortex-M 先例。
2. **模式 A 如何满足「自定义 Skill」要求？** 赛道指引的模式 A 允许「基于设备通信协议和云端大模型独立开发」，但基础要求中的 Skill 定义完全是 ai_agent 术语。

---

## 16. 已确认架构决策

本章记录设计评审中已收敛的约束，实现以本章为准。

### 16.1 设备身份与 MQTT 鉴权

- 量产 `device_id = velaguard_{STM32_UID 派生短 ID}`
- 测试阶段允许编译期宏 `DEVID` 覆盖
- 量产固件不提供任何运行时修改 `device_id` 的接口
- `display_name` 可改，仅用于 UI 展示，不参与权限边界

MQTT token 使用带产品密钥的 HMAC，不能用普通 hash：

```text
mqtt_token = base64url(
  HMAC-SHA256(PRODUCT_AUTH_SECRET, "velaguard:mqtt:v1:" + device_id)
)
```

支持版本轮换（`v1` / `v2` 并存迁移），单台泄露时通过 Broker denylist 禁止该 `device_id` 登录。

### 16.2 MQTT 权限与 QoS

正式环境：MQTT over TLS、每设备独立 token、Broker ACL 限制设备只能访问自己的 `vg/{device_id}/...`。测试环境可在受控局域网使用明文，量产固件关闭。

Topic 根路径固定 `vg/{device_id}/...`，不加环境前缀。

| 类型 | QoS | retained |
|---|---:|---|
| `telemetry` / `trend` | 0 | 否 |
| `status` | 0 | 是 |
| `alarm` | 1 | 否 |
| `diagnosis` | 1 | 否 |
| `ota/*` | 1 | 否 |
| `ack/confirm` | 1 | 否 |

会话策略：固定 `client_id`，v1 用 `clean_session=true`，重连后重新订阅，使用 LWT 发布离线状态，关键事件依赖本地 pending 队列重发而非持久 session。

### 16.3 ID、时间戳与幂等

- `req_id`：请求级，发起方生成，响应原样带回
- `event_id`：事件级，设备生成并持久递增，云端据此去重
- `alarm_id`：告警实例级，同一未恢复告警保持同一 ID

格式 `{device_id}-{boot_id}-{seq}`；`boot_id` 每次启动生成，`event_seq` 必须持久化以避免断电后重复。

时间统一 Unix 毫秒并同时记录运行时长：

```json
{ "ts_ms": 0, "uptime_ms": 0, "time_quality": "unknown|rtc|ntp|cloud" }
```

网络恢复后不回改历史事件时间，云端另存 `received_ts_ms`。

### 16.4 LLM 请求约束

- 请求携带 `req_id`、`device_id`、`created_ts_ms`、`type`、`payload_hash`
- 经 Bridge 时以 `req_id + payload_hash` 做幂等键
- 诊断会话单轮超时 30–60 s；告警解释 / 报告会话上限 10 轮
- 上下文中必须包含告警上下文、事件摘要与工具调用结果
- `llm_proxy` 响应缓冲最大可 realloc 至 512 KB，须确保分配落在 SDRAM 堆而非仅 450 KB 的 AXI 堆

### 16.5 网络、ESP-01 与启动顺序

网络单活动链路，RJ45 优先，不做双链路并发。ESP-01 独立 3.3 V 供电（峰值按 300–500 mA 设计）、独占 UART（不与 Modbus 共用）、AT 状态机非阻塞、连续失败后 GPIO 硬复位。ESP-01 故障只能影响云端能力。

启动顺序：

```text
最小硬件 / 日志 / 看门狗
→ eMMC + 文件系统
→ 加载配置（失败则默认配置）
→ Modbus 采集 + 帧统计
→ 本地告警规则
→ 数字量输出进入安全默认态
→ LVGL UI
→ network_manager → MQTT
→ ai_agent
```

增强模块失败不得拖垮本地安全闭环。

### 16.6 Modbus 状态与告警模型

Modbus 状态：`online` / `degraded`（有错误未判离线）/ `offline`（低频探测，不清除既有告警）/ `recovering`（连续成功确认后才回 `online`）。

告警模型：允许多个 active alarm；首页显示最高优先级，详情页展示全部；`acknowledge` 不等于 `resolved`；同一未恢复告警更新同一 `alarm_id`。

时间字段：`first_seen_ts` / `last_seen_ts` / `ack_ts` / `resolved_ts`。

阈值告警使用持续时间窗口而非回差：

```text
value > threshold 持续 trigger_duration_ms 后触发
value <= threshold 持续 restore_duration_ms 后恢复
```

突变告警：`abs(current - value_N_seconds_ago) >= delta` 持续 `trigger_duration_ms` 后触发。

### 16.7 日志系统

Log4j2-inspired 滚动日志风格，不引入真正的 Log4j2。

文件：`latest.log`（人类可读）、`debug.log`（可选）、`archive/*.log`（滚动归档）、`events.jsonl`（结构化业务事件）。

事件模型：`timestamp + logger/category + level + message + key=value fields`，等级 `debug` / `info` / `warn` / `error`。

不同 appender（文件 / 串口 / UI / cloud）可设不同最小等级与 category filter。云端默认只上传结构化事件与关键 error/warn 摘要。

**Agent 工具调用必须全量写入审计日志**：时间、工具名、入参、返回摘要、耗时。

### 16.8 配置与文件系统

存储介质 eMMC + FAT。FAT 不提供掉电原子性，因此配置采用双槽提交：

- `point_table_a.json` / `point_table_b.json`
- 每份含 `schema_version`、`seq`、`crc32`、`committed=true`
- 写入顺序：写非活动槽 → fsync → 更新 `seq` 与校验 → fsync → 标记 committed
- 启动时选 `seq` 最大且校验通过者；皆坏则进出厂默认

配置版本：所有配置带 `schema_version`；固件声明 `min_supported_schema` 与 `current_schema`；旧版本走迁移函数；未来版本拒绝加载；缺字段补默认并写 warn；关键字段非法时禁用对应模块而不拖垮全系统。

UI 权限：首页只展示状态与告警，不放危险操作；低风险普通确认，中风险二次确认，高风险长按确认；高等级告警存在时 UI 优先展示告警；所有变更写 `events.jsonl` 并记录来源（`local_ui` / `agent_suggestion` / `factory_default`）。

### 16.9 OTA

- 镜像经 MQTT 分片拉取，**下载到 eMMC**，不写 QSPI
- chunk 4 KB 或 8 KB，限制 inflight 数量，不得挤占采集、告警、UI、日志
- 必须校验 sha256 与数字签名，仅 hash 不够
- QSPI 擦写由片内 Flash 的 boot stub 执行（应用态无法擦写 XIP 中的 QSPI）
- 新固件启动后必须自检并 mark confirmed，否则回滚
- 存在高等级 active alarm、存储异常或供电不稳时不允许升级
- 生产固件只接受生产签名
- OTA 全过程写结构化事件

### 16.10 构建模式

```text
VG_BUILD_MODE=test
VG_BUILD_MODE=production
```

`test` 允许：编译期覆盖 `DEVID`、局域网明文 MQTT、详细 debug 日志、开发签名 OTA。

`production` 要求：`device_id` 从 UID 派生、无运行时修改接口、MQTTS + token + ACL、生产签名 OTA。

完整 token、产品密钥、LLM API key、OTA 私钥不得出现在 UI、日志、串口输出或 MQTT payload 中。
