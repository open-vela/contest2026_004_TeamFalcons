# VelaGuard Agent 工作规则

本文件是本仓库及其子目录内开发助手的统一规则正文。开始工作时直接遵守以下约束，无需先打开另一份边界摘要。C/V 编号沿用旧文档，仅用于本地追溯，不是官方条款编号。

- **官方竞赛要求优先于本地项目文档和工作流**。本文件区分官方要求与团队约束，不是合规证明；有冲突时核对官方来源并说明差异，不自行放宽限制。
- **开发助手**指协助修改代码和文档的工具；**设备端 Agent**指板载 `ai_agent`。设备端 Agent 的权限限制不能通过开发助手代执行、自动确认或修改提示词绕过。
- 产品细节查项目手册及相关 ADR，术语查 `CONTEXT.md`，执行流程查 Trellis。旧文档、历史任务和术语定义不能恢复已经废止的架构或授权。
- 核查日期：**2026-09-13**。已对照官方 `dev-ai-contest-2026` 文档；本地官方竞赛目录与远端最后修改提交 `cb0389919ea4fc86774702fb36c08ee9b1366e6c` 一致。来源见文末。

## 竞赛与仓库约束

| 编号 | 性质 | 必须遵守的要求 |
|---|---|---|
| <a id="c1"></a>C1 | 官方要求 + 团队落地 | 参赛产品代码和交付物放在 `contest2026_004_TeamFalcons/`。公共依赖的驱动、框架等改动按 C2 放在各自仓库。 |
| <a id="c2"></a>C2 | 官方要求 + 团队约束 | 官方要求公共仓改动通过 fork + PR 提交到 `dev-ai-contest-2026`，由组委会 review 合入。团队要求直接修改 `nuttx`、`apps`、MQTT-C、`packages` 的实际 git checkout，在 VelaGuard 功能分支开发；禁止创建、应用或继续使用 `scripts/openvela-*.patch`、`scripts/apply-openvela-*-patch.sh`。`vendor/` 只通过本仓 manifest 的 `<linkfile>` 映射接入，不直接修改。 |
| <a id="c3"></a>C3 | 官方要求 | 大赛仅在 GitHub 提交；Gitee/GitCode 仅可作为源码镜像。专属仓流程为 fork -> 开发 -> commit/push -> PR 回专属仓 -> 自行 review 合入，无需等待组委会审核。 |
| <a id="c4"></a>C4 | 官方要求 | 提交截止 **2026-09-20**，代码、交付物和所需 AI Coding 日志须在截止前完成提交。截止后统一收回 push 权限，仍可查看和 clone；具体时间调整以官方公告为准。 |
| <a id="c5"></a>C5 | 官方要求 + 团队约束 | 正确配置采集器后，openvela 工作区内的会话结束时自动写入 `logs/<github_login>/`，采集器不自动 push。不得忽略 `logs/`，不得改写或篡改 AI Coding JSONL 的正文、序号或来源。涉及隐私时，由本人决定在 commit 前删除整份会话文件，不删改其中片段。提交与推送由参赛者控制。 |
| <a id="c6"></a>C6 | 官方要求 + 本地扩展说明 | 官方支持 Claude Code、OpenCode、Codex、AIoT-IDE 的开发日志。团队扩展采集的 Grok Build、Cursor 等记录必须保留真实来源，例如 `grok-build`、`cursor`；不得伪装成 `claude-code`，也不得把采集成功当作官方认可或计分承诺。直接 API 调用不能冒充受支持工具的会话日志；这不限制产品正常调用 LLM API。 |
| <a id="c7"></a>C7 | 官方要求 | 作品必须原创，遵循 Apache 2.0，不能存在版权、专利等法律纠纷。首次提交 PR 按官方要求签署 CLA；`cla/signature` 未通过时先处理签署，再在原 PR 评论 `/check-cla` 复检。 |
| <a id="c8"></a>C8 | 官方要求 + 团队选题 | VelaGuard 主选题为 AI 硬件产品创新，必须在 openvela 设备上运行，不能只交纯云端应用或无主动能力、无工具调用的聊天机器人。官方三个方向是选题指引，可组合，不是互斥的独立赛道。 |
| <a id="c9"></a>C9 | 官方 AI 硬件要求 | 设备端固件实际运行，LLM 后端和基础对话可用；接入**至少 1 个交互渠道**，提供**至少 1 个自定义 Skill 及使用演示**、**至少 1 个主动 + 执行场景**，并交付用户故事、功能清单、技术实现说明。主机单测或编译通过不能代替设备运行证据。 |
| <a id="c10"></a>C10 | 官方要求 | 赛事 MiMo Token 仅用于本次大赛。若作品使用语音唤醒，指定唤醒词为 `你好，openvela` / `Hello，openvela`；VelaGuard 当前仍按 V2 不做语音。 |
| <a id="c11"></a>C11 | 官方要求 | 必须使用 openvela 开源项目提供的系统能力，**仅使用 NuttX 内核不满足判定**，且至少落地图形、AI、多媒体三项核心能力之一。 |
| <a id="c12"></a>C12 | 官方要求 + 团队落地 | 每队只有一个专属仓。本队为 `contest2026_004_TeamFalcons`，不同作品形态放在子目录，通过 `contest2026_004_TeamFalcons.xml` 的 `<linkfile>` 映射到 openvela 工程，不另建参赛专属仓。 |
| <a id="c13"></a>C13 | 官方要求 | 每队 1-5 人，每人仅加入一队。项目需要的服务端由团队自行搭建，赛事期间须稳定运行并可正常演示。 |

## 产品与安全边界

以下是团队已确认的产品约束，不是官方对所有参赛作品的要求。基于项目手册 v3.1，并结合当前构建入口和 NSH 点表协议更新。

| 编号 | 必须遵守的要求 |
|---|---|
| <a id="v1"></a>V1 | VelaGuard 是 STM32H750B-DK 上的独立网关，不依赖长期连接电脑。USB CDC/UART 用于开发调试、配置和救援，不作为正式运行依赖。 |
| <a id="v2"></a>V2 | 本地确定性流程必须离线运行：Modbus 采集、帧统计、规则告警、LED 与屏幕显示、配置和事件日志、数字量输出安全默认态。网络、MQTT、MiMo、设备端 Agent、OTA 或电脑不可用时，这些功能仍继续。联网 AI 失败时展示本地规则和真实数据，不伪造 AI 解释或报告。不做 ASR/TTS、语音或本地音频告警。 |
| <a id="v3"></a>V3 | 板载 openvela `ai_agent` 使用 ReAct、自定义只读 C 工具及 `/data/agent/skills/` 下的 Markdown Skills，定位为告警解释、日报/周报和自然语言查数的运营助手；不做总线排障、寄存器写入或探测实验。 |
| <a id="v4"></a>V4 | LLM 链路为板端经 `ai_agent` / `llm_proxy`、OpenAI 兼容 HTTPS 直连 MiMo。密钥通过 `vgprovision` 加密存于 eMMC，不得写进固件镜像。MQTT 仅承载遥测、告警和 OTA，不承载 AI 请求/响应。旧 MQTT AI Bridge 和探针期编译密钥方案不适用于当前产品。 |
| <a id="v5"></a>V5 | 设备端 Agent 不得写寄存器、应用配置、清除告警、控制数字量输出或覆盖本地安全规则。点表/配置变更必须经过确定性校验、试读和人工本地确认。AI 输出必须经板端 schema 校验，解释明确标注 `AI 推测`，与规则判定区分。具体确认步骤见下文。 |
| <a id="v6"></a>V6 | 总线发现由确定性代码完成，阶段 1 使用 NSH `vgdiscover`，固定 9600 波特率。LVGL `Scan Bus` / 启用总线扫描开关默认关闭，用户显式开启后才扫描；设备端 Agent 不参与扫描。 |
| <a id="v7"></a>V7 | OTA 采用 MQTT-only 分片拉取到 eMMC，再由片内 boot stub 烧写 QSPI；不做板端 HTTPS 固件下载器。QSPI XIP 运行期间不能擦写正在执行的 QSPI。 |
| <a id="v8"></a>V8 | 每次实现聚焦一个获准的任务目标，遵守依赖及 `Blocked by`。当前任务和阶段以 Trellis 为准；`.scratch/.../issues/` 只作历史参考，不再作为必须存在的活动任务入口。是否新建任务遵守 Trellis 流程及用户明确选择。 |
| <a id="v9"></a>V9 | 产品统一名为 **VelaGuard**。产品细节及验收查 `VelaGuard_项目手册.md`，术语查 `CONTEXT.md`，架构变更核对相关 ADR；历史描述与现行边界冲突时须说明并核实，不能据此恢复旧方案。 |
| <a id="v10"></a>V10 | 不擅自引入延期范围：生产级 OTA、阶段 2 故障归因规则、生产级 Bridge、ASR/TTS、手册上传解析、屏幕配置编辑。只有明确获准的任务才可调整这些范围；废止的功能不能仅因旧文档提到而恢复。 |
| <a id="v11"></a>V11 | 涉及真实开发板电路的驱动、pinmux、外设 bring-up、扩展板接线或任何 MCU 引脚/网络判断，必须先按下方硬件资料要求核对。纯应用逻辑、MQTT、提示词或文档整理不要求打开硬件资料包。 |

### 人工确认与设备端工具

- 上位机通过 **ST-LINK 虚拟串口 -> 板端 NSH** 修改候选点表；`add` / `set` / `del` 和 `test` 不得改变已确认表。周期采集、首页和告警只使用已确认表。
- `vgpoint test` 后必须展示结果并**停下来等人确认**，再单独发送 `vgpoint apply --confirm`。当前允许人在 PC 提示处回车或亲自在 NSH 输入确认命令，由板端执行生效；本期不要求屏幕长按确认。不得自动代答确认，也不得把编辑、试读和 apply 合成一条命令；固件不得在试读成功后自动 apply。
- 设备端 Agent 禁止调用 `vgpoint` 的全部子命令，包括 `get`，以及 `vgdiscover apply`、`vgcfg commit`。开发助手进行获准的调试也必须遵守上面的人工确认步骤。
- 只读权限、参数上限、限流、会话门控和工具调用审计必须落实在工具注册与 C 实现中，不能只依赖提示词。`tool_guard.c` 不提供 JSON Schema 参数运行时校验，自定义工具必须检查越界参数。
- RS485 只由板端作为 Modbus 主站使用，同一总线不得同时存在第二个主站。不经 RS485、MQTT 或第二路 UART 下发点表配置；主机模拟从站时关闭其主站采集会话。
- 完整 API key、MQTT token、产品密钥和 OTA 私钥不得出现在 UI、日志、串口输出或 MQTT payload 中。命令和数据格式继续以 [NSH 协议](docs/velaguard-host-nsh-protocol.md) 为准，不在各任务重复维护命令表。

### 硬件资料要求

- Windows 官方资料包：`F:\Project\Embeded\H750B-DK\BOARD INFO\H750B-DK`。
- WSL 对应路径：`/mnt/f/Project/Embeded/H750B-DK/BOARD INFO/H750B-DK`。
- 资料优先级：**原理图 PDF / SchDoc > `bsp/` 中 ST BSP > ST UM / data brief**。
- 文件名含 `unofficial` 的资料只能辅助检索，编码前必须用原理图或 BSP 交叉核对。资料包可用时，不得仅凭网上 UM 摘录或第三方 pinmux 笔记作结论；无法访问必要资料时先说明缺少的证据，不猜引脚。

## 开发与日志操作

以下是团队操作约定。Trellis 只管理开发流程，不能修改竞赛或产品安全要求。

1. 先确认当前 git 状态、任务目标和改动所属仓库，保留用户及其他任务的未提交改动。按当前任务读取相关手册、ADR 和 `.trellis/spec/`，不要求每次读取全部专项文档。
2. 日常固件入口是在**本仓根目录**运行 `bash scripts/build.sh`，默认 `velaguard-lvgl`；不把父目录通用 `./build.sh` 当作日常入口。该脚本使用父级完整 openvela 工作区。
3. 构建脚本只验证公共树已具备所需 VelaGuard 改动，不应用竞赛仓补丁。将 `scripts/configs/*.defconfig` 安装到 NuttX 板级配置属于允许的配置复制。
4. 产品协议或点表逻辑改动按范围执行 `make -C app/velaguard/host_tests test`，板端改动按相应 skill 验收。文档修改不要求编译烧录；不得将未执行的测试、推测或历史验收写成当前成功证据。

| 公共源码树 | 常见功能分支 | 改动归属 |
|---|---|---|
| `../nuttx` | `velaguard/*` | 板级引脚、驱动、defconfig、LTDC/FT5x06 |
| `../apps` | `velaguard/netinit-esp8266` | netinit / ESP8266 兼容 |
| MQTT-C 的实际 checkout | `velaguard/mqtt-pal-hook` | PAL send/recv hooks |
| `../packages` 下对应实际 checkout | `velaguard/*` | `ai_agent` 等框架修复，含 HMI 内存安全 |

### AI Coding 日志

- 采集范围通过向上查找 `.repo/` 确定。工作区内日志可能在会话结束后自动入仓，staging 不应视为隐私隔离区；工作区外不采集。自动入仓与上传 GitHub 是两个步骤。
- 当前机器身份记录为 `~/.claude/contest-collector.env`，`TEAM_ID=contest2026_004_TeamFalcons`、`GITHUB_LOGIN=Foleaf`。其他成员必须使用自己的报名 GitHub 账号，不能照抄 Foleaf。
- 新环境需要安装时，在本仓运行 `bash ../.claude/skills/contest-log-collector/onboarding/install.sh --team-id contest2026_004_TeamFalcons --github-login <本人的GitHub账号>`。
- 健康检查：`bash ../.claude/skills/contest-log-collector/onboarding/verify-setup.sh`。会话清单：`contest-snapshot --list`。发现日志缺失时先查工作区和采集配置，不自行编造或改写日志补齐。
- 提交前官方日志校验命令：`python3 ../.claude/skills/contest-log-collector/tools/validate-log.py logs/`。校验通过不代表已满足全部竞赛要求，也不代表日志已推送。
- 日志经人工检查后可与代码一起 commit/push；采集器不自动推送。不得因本地存在日志而宣称已完成竞赛提交。

## 参赛证据与提交检查

这是待逐项验证的清单，不是已完成状态：

- 产品源码及 manifest 映射留在专属仓，公共树改动分别按 C2 提交 PR；获奖后按官方要求将作品 PR 至对应上游 `dev-ai-contest-2026`，通过标准 PR + CI。
- README 是产品介绍，包含名称、选题方向、运行方式和可复现的构建步骤；另需作品介绍文档（`.docx` / `.pdf` / `.pptx`）、不超过 **5 分钟**的演示视频（如 `.mp4` / `.mov`）及专属仓地址。
- VelaGuard 以设备端 `ai_agent + LVGL` 为主线，提供真机固件运行、LLM 基础对话和 CLI/LVGL 交互证据。官方指引也列出设备协议结合云端 LLM 的模式 A，不能据此把本项目替换为 PC 或纯云端演示。
- 设备 Skill 证据按项目方案提供 `alarm_interpretation.md`、`operations_report.md` 及使用演示；主动场景为事件触发告警解释、定时生成日报，周报可按任务安排。自然语言查数是补充交互，不能单独替代主动 + 执行场景。
- 使用 AI Coding 时须提交开发日志，并沉淀至少一个有效的开发 Skill。开发助手使用的 Skill 与 `/data/agent/skills/` 下的设备 Skill 是两类证据，不能默认互相替代。
- 场景材料说明用户故事、实际实现的功能和使用的 openvela/ai_agent 能力；区分已验证、未验证及延期内容，不把 MQTT 遥测或 LLM 转发本身宣称为已满足端云 Agent 协作加分项。
- 核对原创/Apache 2.0、CLA、MiMo 用途及服务端演示稳定性；截止前确认代码、交付物和校验后的日志已实际提交并推送。

官方评分权重供组织证据参考：技术难度 30、产品创新性 20、项目完整度 20、AI 开发 10、商业潜力 10、展示效果 10。评分由评委结合仓库、作品说明和演示判断，不存在本地一键合规或计分证明。

<a id="rule-sources"></a>
## 来源与维护

| 官方来源 | 用途 |
|---|---|
| [大赛总览](https://github.com/open-vela/docs/blob/dev-ai-contest-2026/zh-cn/contest_2026/contest_overview.md) | 参赛要求、提交物、评分及 openvela 判定 |
| [代码提交指南](https://github.com/open-vela/docs/blob/dev-ai-contest-2026/zh-cn/contest_2026/code_submission_guide.md) | 专属仓、公共仓、截止日期和 CLA |
| [AI Coding 日志手册](https://github.com/open-vela/docs/blob/dev-ai-contest-2026/zh-cn/contest_2026/ai_coding_log_guide.md) | 采集、隐私、工具支持和日志校验；通用文档中的旧手动导出描述以本专项手册为准 |
| [AI 硬件指引](https://github.com/open-vela/docs/blob/dev-ai-contest-2026/zh-cn/contest_2026/ai_hardware/ai_hardware_track_guide.md) | 设备、交互渠道、Skill、主动场景及进阶要求 |
| [AI 硬件导航](https://github.com/open-vela/docs/blob/dev-ai-contest-2026/zh-cn/contest_2026/ai_hardware/ai_hardware_guide_index.md) / [ai_agent 上手](https://github.com/open-vela/docs/blob/dev-ai-contest-2026/zh-cn/contest_2026/ai_hardware/ai_agent_quickstart.md) | 开发教程 |
| [官方 skills](https://github.com/open-vela/.claude/blob/dev-ai-contest-2026/README_zh-cn.md) | openvela AI 开发工具 |
| [大赛官网](https://www.openvela.com/#/contest) / [CLA](https://openvela.com/#/community/cla) | 最新公告和贡献者签署 |

本地官方文件位于父工作区 `docs/zh-cn/contest_2026/`。产品资料为 [项目手册](VelaGuard_项目手册.md)、[推进方案](VelaGuard_推进方案.md)、[术语](CONTEXT.md)、[ADR](docs/adr/) 和 [README](README.md)；它们不替代官方竞赛文件，也不免除实际验收。

只在本文件维护 agent 必守规则正文，修改时复核来源并更新核查日期。`CLAUDE.md` 导入本文件；Trellis 任务上下文直接引用本文件；[旧边界文档](docs/agents/BOUNDARY.md) 只保留来源与编号索引。历史归档不批量重写。Trellis 托管块之外的内容由团队维护；不要随意修改指向工作区 `../.claude` 的官方 skills。

## 输出风格

适用于问答、代码注释和文档：使用易懂、自然的中文，只保留必要的技术关键词。

- 除非用户明确要求逐字引用，不得使用 Unicode U+201C、U+201D 中文双引号；也不靠引号强调缩减词、行业黑话或短句。
- 避免翻译腔、生硬直译和不自然的比喻，例如接住、击穿、锋利、不崩、不爆、打穿、扛住。
- 不过度缩减专业词汇，使用服务器高负载、微服务、数据库连接池等完整且明确的表达。
- 不生造词，不将英文概念生硬糅合成中文术语。
- 无明确对比、纠错或用户要求时，不使用 `不是 A，而是 B` 句式；直接陈述事实。

<!-- TRELLIS:START -->
# Trellis Instructions

These instructions are for AI assistants working in this project.

This project is managed by Trellis. The working knowledge you need lives under `.trellis/`:

- `.trellis/workflow.md` — development phases, when to create tasks, skill routing
- `.trellis/spec/` — package- and layer-scoped coding guidelines (read before writing code in a given layer)
- `.trellis/workspace/` — per-developer journals and session traces
- `.trellis/tasks/` — active and archived tasks (PRDs, research, jsonl context)

If a Trellis command is available on your platform (e.g. `/trellis:finish-work`, `/trellis:continue`), prefer it over manual steps. Not every platform exposes every command.

If you're using Codex or another agent-capable tool, additional project-scoped helpers may live in:
- `.agents/skills/` — reusable Trellis skills
- `.codex/agents/` — optional custom subagents

Project-local skills (also mirrored under `.cursor/skills/` for Cursor):
- `mthings-automation-config-skill` — generate or modify MThings `.mthings` from Modbus/S7/DL/T645/CJ/T188/DL/T698.45 point tables; trigger when importing registers, building SCADA pages/widgets, or validating `.mthings` XML.
- `velaguard-board-inner-loop` — compile velaguard-lvgl, QSPI flash, COM3 NSH self-accept.
- `velaguard-candidate-confirm` — vgpoint candidate table, test-read, human `apply --confirm`.
- `velaguard-alarm-to-screen` — local threshold/offline alarm on the HMI and `pending_alarm.txt`.
- `velaguard-board-hw` — H750B-DK schematic-first pinmux/driver work.

Managed by Trellis. Edits outside this block are preserved; edits inside may be overwritten by a future `trellis update`.

<!-- TRELLIS:END -->
