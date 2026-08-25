# 重构 Agent 能力场景：以点表语义化替换故障诊断

> 规划完成，待用户批准后进入实施。技术设计见 `design.md`，改写清单见 `implement.md`。

## Goal

`VelaGuard_项目手册.md` v2 把板载 Agent 的招牌能力定为「485 链路劣化时自主设计并执行只读探测实验，收敛出故障归因」（手册 §1.4 / §5.5 / §14.2）。该场景实现代价高、AI 不可替代性弱、验收判据不可控。

本任务为 Agent 选定新的能力场景，重新评估被 v2 删除的语音决策，确立上位机的产品定位，并把结论回写手册、推进方案与术语表。

**本任务只产出文档，不写产品代码。**

## Background

### 为什么「定位通信故障」应当被替换

五条理由，需完整写入推进方案的「被推翻的决策」章节——理由比结论更重要。

1. **把可枚举问题交给了 LLM。** 手册 §5.7 自己列了 9 条「现象 → 归因」决策表，并声明规则库「可单元测试」。规则库一旦上线，Agent 的增量价值只剩「实验设计」。而 485 的实验设计同样可枚举——降速重试、拉长超时、换寄存器块、隔离测试，就这几招，写成决策树比 LLM 更可靠也更快。手册 §4.2 用「规则库是分类器，Agent 是实验设计者」给 Agent 找位置，但这个位置的宽度撑不起招牌功能。
2. **LLM 在该领域的先验很薄。** RS485 时序、CRC 错误率与终端电阻的定量关系，训练语料中极少。LLM 只能给教科书式泛泛回答，给不出定量判断。
3. **验收判据不可控且验证成本极高。** §14.2 的「5 类故障中 ≥3 类在 ≤6 轮内收敛」要求每次改 Skill 都重新切跳线、改从站波特率、跑一遍真实硬件，且结果不稳定。
4. **演示中 AI 是隐形的。** 屏幕显示「Agent 降速到 9600 重试」，评委无法分辨这是 LLM 决策还是 `if-else`。AI 的价值在展示环节不可见（展示效果占 10 分）。
5. **前置基建过重。** 依赖帧级质量统计、工具沙箱、诊断会话编排、可注入 5 类故障的硬件环境——当前代码库中全部不存在。

### 一条必须先厘清的区分

**语音是交互渠道与输出形式，不是 Agent 能力场景。** 若 Agent 的职责仍是「诊断 485 故障」，只是把结论念出来，本质没有变化。因此本任务的语音决策从属于场景决策，而非替代它。

## Confirmed Facts

### F1. 代码基线：手册描述的功能几乎都未实现

`app/velaguard/` 手写产品代码约 3,540 行（另有 nanoMODBUS 移植 3,024 行）。

| 模块 | 状态 |
|---|---|
| Modbus RTU 主站（nanoMODBUS + `modbus_port_openvela.c` 333 行） | 已实现，但仅 NSH 工具级（`vgmodbus`），未接入主循环 |
| RS485 方向控制 | 半成品，依赖 `CONFIG_UART7_RS485` + 5 ms 发尾延时 |
| RJ45/ESP-01 双栈故障转移（`vg_net_mgr.c` / `vg_net_policy.c` / `vg_esp_bearer.c`） | 已实现，含主机单测 |
| MQTT 客户端（`velaguard_mqtt.c` / `vg_mqtt_session.c`） | 已实现，明文 1883，无 TLS |
| 帧级质量统计 | **不存在** |
| LVGL HMI | **不存在** |
| 配置存储 / 事件日志 | **不存在** |
| ai_agent 集成 / Agent 工具集 | **不存在** |
| 总线扫描 / 寄存器块探测 / 字序求解 | **不存在** |

三个 defconfig（`velaguard-net` / `velaguard-min` / `lvgl`）均未启用 mbedTLS、littlefs、audio。`.trellis/tasks/` 下 3 个活跃任务全部 `in_progress`。

**含义**：换方向的沉没成本很低，现在是最佳时机。

### F2. 手册关于音频硬件的判断是错的

手册 §2.4 / §3.2 与推进方案 §2.3 均写「WM8994 codec 树内无驱动，需自写」，并据此把语音判为「投入产出比极低」。该论据不成立：

| 事实 | 证据 |
|---|---|
| NuttX 树内**已有** WM8994 驱动 | `nuttx/drivers/audio/wm8994.c` / `.h` / `wm8994_debug.c`；`drivers/audio/Kconfig:360-419` |
| 真正缺的是 **STM32H7 SAI 驱动** | `arch/arm/src/stm32h7/` 下无任何 `*sai*` / `*i2s*` 文件；`stm32h7/Kconfig:685-687` 只有空 `config STM32H7_SAI bool default n` |
| 可移植的参考实现存在 | `arch/arm/src/stm32f7/stm32_sai.c`（约 1,666 行），Kconfig `stm32f7/Kconfig:1253+` |
| 同 codec 的板级集成参考存在 | `boards/arm/stm32f7/stm32f746g-disco/src/stm32_wm8994.c:256-268`；bring-up `stm32_bringup.c:148-154`；引脚 `include/board.h:529-538` |
| 板级也缺 SAI 引脚与 bring-up | `stm32h750b-dk/include/board.h` 无 `GPIO_SAI*`；`src/Makefile:23-57` 无 audio 源文件 |

### F3. STM32H750B-DK 音频硬件完备，输入输出都有

来自 ST UM2611（MB1381）与 ST BSP `stm32h750b_discovery_audio.h`：

- WM8994ECS/R codec，经 **SAI** + **I2C4**（PD12/PD13，与 FT5x06 触摸共享）控制
- 输出：CN9 绿色 3.5 mm line out；CN17/CN18 外接左右扬声器排针
- 输入：CN8 蓝色 3.5 mm line in，**支持模拟麦克风**（走 WM8994 ADC）
- 另有 1 颗 **MP34DT01TR 数字 PDM MEMS 麦克风**，需 PDM/DFSDM 通路

**关键推论**：模拟麦（CN8）与 line out（CN9）走**同一条 SAI + WM8994 链路**。若语音只用模拟麦，PDM 通路可完全不碰，输入输出共用一次驱动投入。

### F4. ai_agent 自带语音管线，但对 Cortex-M7 是重量级

- `src/voice/` 共 8 个源文件，被 `Makefile:117-125` **无条件编入**，无独立 `CONFIG_AI_AGENT_VOICE` 开关
- ASR：火山引擎（豆包）WSS 流式，`wss://openspeech.bytedance.com/api/v2/asr`（`volc_asr.c:17-24`、`agent_config.h:342-345`）
- TTS：火山引擎，HTTP 批量 + WSS 流式（`volc_tts.c:134-136`、`volc_tts_ws.c:18-20`），`voice_channel_speak()` 优先走流式（`voice_channel.c:1054-1076`）
- 采集/播放依赖 **Vela media framework**（`media_recorder` / `media_player`），而非直接操作 `/dev/audio`（`audio_capture.c:17-23`、`audio_playback.c:17,108`）
- 未启用 `CONFIG_MEDIA` 时由 `src/stubs.c:36-64` 弱符号兜底，可链接但 `voice_channel_start()` 返回 -1
- 三个官方 defconfig 中只有 **gemini-s1（Cortex-A7）** 链路完整；goldfish 无 `CONFIG_MEDIA`，esp32s3-eye 无任何音频配置
- **无唤醒词实现**：全仓搜索 `wake word` / `hotword` / `你好 openvela` 均无匹配。交互是 **PTT**（`voice_channel.c:199,371,750`、`lvgl_ui_channel.c:698-712`）

**含义**：复用 ai_agent voice 管线需额外在 M7 上跑通 Vela media framework，风险叠加。绕开它自写最小 PCM 播放（`/dev/audio/pcm0p`）则不需要 `CONFIG_MEDIA`。

### F5. 已有的廉价发声路径（非语音）

`/dev/pwm0` = TIM15_CH2 @ PE6 无源蜂鸣器，已在 `velaguard-min` / `velaguard-net` defconfig 启用（`board.h:525-526`、`stm32_pwm.c:29-35`），仓内已有 `velaguard_pwm.c`。`drivers/audio/tone.c` 存在，只差板级 `board_tone_initialize()`（参考 `boards/arm/stm32/common/src/stm32_tone.c:118-121`）。**只能出提示音，不能出语音。**

### F6. 赛道对交互渠道与能力落点的要求

- `BOUNDARY.md:93-95`（C9）：≥1 个自定义 Skill、≥1 个「主动 + 执行」场景（timer / threshold / event / context-driven）、书面场景说明
- 手册 §14.3 引官方原文：交互渠道 **CLI 即可满足**，语音是可选项
- `BOUNDARY.md:65`（C11）：需落地图形 / AI / 多媒体三者之一。LVGL 已覆盖「图形」，故语音非必需
- `BOUNDARY.md:64`（C10）：若用语音唤醒词，必须是「你好 openvela / Hello openvela」；F4 已确认需自行实现
- `BOUNDARY.md:107`（V1）：独立网关，非 PC sidecar demo
- `BOUNDARY.md:111`（V5）：AI 候选配置须经设备端 Local Confirmation
- 评分权重（`BOUNDARY.md:177-184`）：技术难度 30 / 产品创新性 20 / 项目完整度 20 / AI 开发 10 / 商业潜力 10 / 展示效果 10

## Decisions

### D1：以「点表语义化」为核心场景，「跨点位告警解释」作为阶段 2 延伸

分工口径：**确定性算法解决「这是什么数」，Agent 解决「这是什么」。**

- **阶段 1 招牌 = 点表语义化。** 扫描与约束求解能算出「40010-40011 是 CDAB float32，当前 12.34」，但算不出它是冷却水流量、单位 m³/h、正常区间 8–15。后者是开放世界知识，算法无解，LLM 不可替代性极强；演示可见性也极强（屏幕从 `40010: 12.34` 变成 `冷却水流量 12.34 m³/h`）
- **阶段 2 延伸 = 跨点位告警语义解释**（如「流量骤降 + 温度上升 = 泵可能空转或堵塞」）。点位组合爆炸使规则库不可行，但它依赖 A 产出的语义层，且可证伪性弱（难判定解释对错），故排后
- **原 485 故障归因降级**：9 条决策表保留为确定性规则库（阶段 2），不再是 Agent 招牌。净效果是诊断能力**不再依赖网络**，断网降级口径反而简化

未采纳的备选：**语音现场助手**——它是交互渠道而非能力场景，归入 D2；**自然语言告警规则配置**——「规则是人的意图，扫描无法取代」这条理由成立（v2 删除「自然语言创作点表」的理由对规则不适用），但主动性弱、逼近赛道排除的纯问答，只能作为附属能力，本次不纳入。

完整技术设计见 `design.md` §1–§3。

### D2：先做 SAI + WM8994 探针（20 h 封顶），通过后再规划完整语音

照搬推进方案 §1「先证伪最大的未知」与 §4 ai_agent 探针的做法。探针通过前，不把 65–120 h 的完整语音链路写进计划。

语音输入输出共用同一条 SAI 链路（F3），故不拆分；输出的强场景在阶段 2 的 B，输入的价值由 D3 的上位机替代承接。

成本构成（基于 F2–F5）：

| 项 | 粗估工时 | 说明 |
|---|---:|---|
| STM32H7 SAI 驱动移植 | 40–60 | 参考 `stm32f7/stm32_sai.c`（1,666 行）；树内无 H7 先例 |
| 板级 `stm32_wm8994.c` + SAI 引脚 + bring-up | 15–20 | 参考 `stm32f746g-disco`，同 codec |
| 最小 PCM 播放（自写，走 `/dev/audio/pcm0p`） | 10–15 | 绕开 `CONFIG_MEDIA` |
| 复用 ai_agent `src/voice/`（替代上一项） | 20 + 未知 | 需先在 M7 跑通 `CONFIG_MEDIA` + media_server |
| 云端 ASR/TTS 客户端（火山引擎 WSS） | 10–15 | 硬依赖 mbedTLS（当前未启用） |
| PTT 触发（LVGL 按钮） | 5 | 唤醒词需自写，M7 上不现实 |

收益：命中 C11「多媒体」（非必需，LVGL 已覆盖图形）、技术难度分、两个上游 PR（H7 SAI 驱动、H750B-DK WM8994 板级支持）。

风险：推进方案 §9.1 已判定阶段 1（160–260 h）大概率装不进 9/20，语音再加 65–120 h。

探针设计与出口条件见 `design.md` §5。

### D3：上位机进入 9/20 参赛范围，走 USB CDC / 串口

产品事实：**后续会配一个上位机，补全屏幕不允许的配置功能。**

这条同时解决了手册内部的一个矛盾。A 场景中 LLM 命中率的最强线索是设备型号 / 铭牌，但手册 §6.1 定了「屏上不做任何编辑」（无软键盘），§2.1 又定了「USB CDC / UART 只作为开发调试和救援通道，不是正式运行链路」——**正式运行链路中没有任何可输入型号的通道**。上位机补上了这个洞，也为手册 §6.1「配置创作是办公室的事」提供了此前缺失的承接落点。

选择 USB CDC 的附带好处：不依赖网络，现场接上即用，与「独立网关」叙事一致。

必须写进手册并守住的三条边界：

1. **运行期不必需**（守 V1）：断开上位机后采集、帧统计、告警、日志、安全默认态全部照常。它是配置期工具，不是 PC sidecar
2. **只创作，不激活**（守 V5 与手册 §2.3）：上位机可编辑配置，但激活必须经屏幕上的 Local Confirmation
3. **手册 §2.1 需修订**：USB CDC / UART 定性改为「配置期正式通道 + 救援通道」

### D4：上位机形态为轻量本地 Web GUI

Python 后端持串口，浏览器前端，粗估 55–85 h。功能范围：设备型号 / 铭牌线索输入、表格化点表编辑、告警规则表单、实时值监视、配置下发。

选型理由：出界面效果最快、跨平台、演示视频观感好；后续可再打包为桌面应用。

## Requirements

- R1：手册与推进方案中「Agent 自主诊断 485 故障」的全部表述替换为「Agent 语义标注」，并保留 9 条决策表作为规则库
- R2：新场景须满足赛道 C9 的「主动 + 执行」——采用**事件主动**（扫描完成自动启动语义标注会话）
- R3：Agent 工具沙箱的全部硬约束（只读功能码白名单、参数硬上界、限流、会话门控、审计日志）一条不减地沿用
- R4：输出 schema 沿用 v2 的两条决策——不含 `confidence` 自报置信度、不含任何写操作字段
- R5：手册中关于 WM8994 / 音频硬件的事实性错误按 F2/F3 全部更正
- R6：上位机的三条边界在手册中显式成文
- R7：新场景须有可离线复现的量化验收判据，替换原 §14.2

## Out of Scope

- 不写任何产品代码，不改 `app/` 下现有实现
- 不重启已冻结的双栈网络模块
- 不修订 `docs/adr/0002` 与 `0005`（描述已被 v2 推翻的云端 Bridge 架构，属既有问题，另开任务）
- 手册 §16 的保留决策（推进方案 §2.5 列举）不动

## Acceptance Criteria

- [ ] `VelaGuard_项目手册.md` 按 `implement.md` 第二步的 32 项清单完成改写
- [ ] `VelaGuard_推进方案.md` 按 `implement.md` 第三步的 12 项清单完成改写，其中 §2 新增「v2 → v3」被推翻决策章节，含 Background 的五条理由
- [ ] `CONTEXT.md` 新增 Register Semantics、Semantic Annotation Session、Host Configurator 三个术语，并复核 Manual Profile / Candidate Configuration 是否仍然成立
- [ ] 新的 §14.2 是「一个可能失败的数字」且可离线复现：≥ 60% 点位语义与单位正确、写操作触发次数 = 0、未确定点位显式列入 `unresolved`
- [ ] 全文搜索确认无残留的「诊断模式」「rs485_fault_triage」「Agent 自主探测实验」措辞
- [ ] 全文搜索 `WM8994`，确认所有「树内无驱动」表述已更正
- [ ] 上位机三条边界（运行期不必需、只创作不激活、§2.1 定性修订）在手册中都能找到
- [ ] 对照 `BOUNDARY.md` 的 C9 / C11 / V1 / V5 复核，新方案全部合规
