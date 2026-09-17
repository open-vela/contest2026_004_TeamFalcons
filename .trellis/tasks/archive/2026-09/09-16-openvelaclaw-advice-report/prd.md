# OPENVELACLAW 告警页逐点建议与真实生成日报

## Goal

在设备交互面上落地 AI 能力，并把日报交回板端 Agent 生成：

1. 告警页每条告警都显示 OPENVELACLAW 给出的行内短建议，选中该条时在详情区给出完整解释（摘要 / 依据 / 建议关注）。
2. 日报由板端 `ai_agent` 真实读取 Skill、调用只读工具、聚合真实数字后自己产出，不再由固件在 C 里拼模板。

OPENVELACLAW 是板端 `ai_agent` 的对外名称，只用于界面与报告文案署名；底层仍是 openvela `ai_agent` + ReAct + 只读工具 + `/data/agent/skills/` 下的 Markdown Skills。

## 背景

判定基准是官方竞赛要求，不是本地项目文档。官方对 AI 硬件方向的要求是：作品必须在 openvela 设备上实际运行，LLM 后端与基础对话可用；接入至少 1 个交互渠道，提供至少 1 个自定义 Skill 及使用演示、至少 1 个主动 + 执行场景；不能交纯云端应用，也不能交无主动能力、无工具调用的聊天机器人；主机单测或编译通过不能代替设备运行证据。

按此基准核对当前设备状态，两处站不住：

- 告警页 `gui/main/ui/pages/vg_page_alarm.c` 的每条告警只有一行确定性摘要，详情区 `ai_head`/`ai_lab` 也由 C 模板填充并标「规则摘要」。板上唯一 AI 文本落在 `/data/velaguard/reports/last_alarm.md`，屏幕完全不读它，用户在设备上看不到任何 AI 输出。
- 日报由固件 `app/velaguard/vg_runtime.c` 的 `vg_runtime_fprint_report()` 拼模板写入 `runtime-report.md`，`operations_report.md` skill 甚至明确禁止 Agent 写报告文件。这样主动 + 执行场景在设备上看不到 Agent 真的读 skill、调工具、聚合真实数字。

## Requirements

- **R1** 告警页每条活动告警有 OPENVELACLAW 的行内短建议；选中行在详情区给出摘要、依据、建议关注项。
- **R2** 建议在告警触发时由板端自动发起一轮并缓存，打开告警页读内存缓存。页面每 tick 不做文件 IO、不发请求、不阻塞。
- **R3** 建议与具体告警轮次绑定，同一测点重新起一轮告警时不得沿用上一轮的旧建议。
- **R4** 日报内容由板端 Agent 真实调用只读工具与 skill 生成，落到 `/data/velaguard/reports/daily-<日期>.md`。
- **R5** Agent 产出的一切上屏文本必须经板端 C 校验，校验不过整份丢弃，不做部分应用。
- **R6** 离线、AI 不可用、生成超时或校验失败时，告警页与报告页退回到确定性本地内容并明确标注来源，不伪造 AI 文本。
- **R7** Agent 的只读边界落实在工具注册与 C 实现里，不只依赖提示词。
- **R8** Agent 真实调用工具的过程要有可复现的设备证据（工具调用行、轮次统计、产物文件）。

## 子任务地图

| 子任务 | 目录 | 交付物 |
|---|---|---|
| A 基础 | `09-16-ai-contract-request-channel` | 纯 C 契约模块 `vg_ai_contract`（VGADV1 解析与校验、请求构造、日报校验）、板端 `velaclaw_ask` 请求通道、公共树只读边界加固 |
| B 功能 1 | `09-16-alarm-per-point-ai-advice` | 告警页行内短建议与详情完整解释，按 `(sensor_id, al_epoch)` 命中后端缓存，离线降级 |
| C 功能 2 | `09-16-agent-authored-daily-report` | 重写 `operations_report.md` skill，日报由 Agent 产出，报告页按来源标注并保留固件离线兜底 |

依赖关系：B 与 C 都依赖 A 的契约模块与请求通道；A 完成后 B、C 可独立推进与验收。C 另依赖 Gate 0（板端能完成一轮带工具的 LLM 调用）。

## Acceptance Criteria

- [x] A 的 `vg_ai_contract` 在 host 上跑通全部边界用例，`make -C app/velaguard/host_tests test` 全绿。
- [x] Gate 0 通过：板上 `ask` 返回中文回答而非报错，日志出现至少一行 `Executing tool: `，出现 `END status=ok iters=N tools=M`，证据落盘到 A 的 `research/`。
- [x] B：注入至少 2 条告警后，告警页行内出现 `AI · ` 短建议，选中行详情头含 `OPENVELACLAW`，并带 AI 推测标注。（板端 8 条告警的建议文档已生成并通过 C 校验；行内与详情的渲染由 headless 断言覆盖，屏幕肉眼确认待做）
- [x] B：断开网络后重新注入告警，行内与详情都回到规则摘要（本地），全程无伪造 AI 文本。（板端：`vgnet inject` 置两条链路不可用后删掉建议文件与当天日报，五分钟内 `net_connect ret=0x42` 与 `LLM call failed` 反复出现，两个文件都没有重新生成；页面文字回退由 headless 覆盖）
- [x] B：同一测点重新起一轮告警时，旧建议不再命中。
- [x] C：`/data/velaguard/reports/daily-<当天>.md` 出现，首行 `AI-DAILY v1`，日期正确，且串口日志按顺序出现 `get_current_time`、`run_shell`、`write_file` 的 `Executing tool:` 行与 `END status=ok`。
- [x] C：报告页显示 AI 日报标题与来源标注；断网后回退到固件 `runtime-report.md` 并标注本地来源。（当天日报由板端生成并通过校验，`from_agent` 为真；断网时当天文件不生成，读取方回退固件报告；屏幕肉眼确认待做）
- [x] 全部上屏文本经 C 校验，校验失败时降级而非显示未校验内容。
- [x] 每个子任务收尾都跑完 `velaguard-board-inner-loop` 的内环（编译、烧录、host 单测、对应 `*_accept.ps1`），按命令 / pass-fail / 关键日志一行汇报。

## Constraints

- 官方竞赛要求优先于本地项目文档；本地手册、`CONTEXT.md`、ADR 只作落地说明，冲突时以官方为准。
- `VelaGuard_项目手册.md`、`docs/demo-video-script.md`、`README.md`、`docs/submission/` 的相关描述需与实现同步更新，不得保留与实现不符的宣称。
- 公共树（`../packages/ai_agent`）改动按 C2 在 `velaguard/*` 分支开发并经 fork + PR 提交，不使用任何 patch。
- 不擅自扩大范围：周报、生产级 OTA、ASR/TTS、阶段 2 故障归因规则均不在本次范围。
- 资源上限很紧：HMI 下 agent 上下文 4 KB、stream 缓冲 4 KB、agent 栈 32 KB、LLM 墙钟 120 s。

## Notes

- 详细设计见父任务计划 `.claude/plans/1-ai-2-openvelaclaw-aiagent-skills-distributed-bird.md`（已获用户批准），子任务各自另写 `design.md` 与 `implement.md`。
- 最大风险是 Gate 0。仓库里现存的板端证据 `.trellis/tasks/09-16-skill-summary-oob-heap-crash/research/board_ask_3rounds_after_fix.txt` 三轮全部 `END status=fail iters=1 tools=0`；用户已确认链路此后已修复，但仍需复测确认。Gate 0 不通过时功能 2 不可交付，不得把固件模板冒充 Agent 产出。
