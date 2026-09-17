# OPENVELACLAW 真实生成日报

## Goal

日报内容改由板端 `ai_agent` 真实读取 Skill、调用只读工具、聚合真实数字后自己产出，落到 `/data/velaguard/reports/daily-<日期>.md`；报告页展示它并标明来源是 OPENVELACLAW。固件生成的 `runtime-report.md` 保留为离线兜底，断网或 Agent 失败时页面显示它并标明本地来源。

现阶段 AI 建议与日报都以官方要求为准：设备上要能看到 Agent 真的读 skill、调工具、聚合数字，而不是固件在 C 里拼模板。

## Requirements

- **R1** 重写 `operations_report.md` skill：由只许复述固件报告，改为授权并用格式约束 Agent 自己产出日报。当前 skill 里「不要用 dump 原文覆盖」「禁止 write_file 覆盖该文件」的措辞必须删除。
- **R2** 日报由 Agent 产出到 `/data/velaguard/reports/daily-<YYYY-MM-DD>.md`，首行标记 `AI-DAILY v1`，日期行正确，正文限定在固定三节且不超过 1400 B。
- **R3** 板端主动发起日报轮次：联网、当天还没有新鲜日报、当天未发起过时自动发起一次；报告页手动刷新也可发起，但要限流。
- **R4** 完成判定以产物文件为准，不以回复文本为准；产物必须经板端 C 校验（标记、日期、长度、mtime 新鲜度、UTF-8 合法性）才装入快照。
- **R5** 报告页按来源标注：Agent 日报显示 OPENVELACLAW 署名，否则显示本地统计来源；断网或失败时自动回退到固件报告，全程不伪造 Agent 产出。
- **R6** 只能采纳当天的日报；不得把历史 `daily-*.md` 当成今天的报告展示。
- **R7** 保留并继续刷新固件 `runtime-report.md` 作为确定性兜底，且它必须保持 Agent 不可写（C 层保护见子任务 A）。

## 非目标

- 不做周报。
- 不改告警页（属于子任务 B）。
- 不打开 `heartbeat_send()` 在 `CONFIG_VG_HMI` 下的提前返回。

## Acceptance Criteria

- [ ] 板上串口日志按顺序出现 `Executing tool: get_current_time`、`Executing tool: run_shell`、`Executing tool: write_file`，并出现 `END status=ok iters=N tools=M`。
- [ ] `/data/velaguard/reports/daily-<当天>.md` 存在，首行 `AI-DAILY v1`，`date=` 为当天，正文不超过 1400 B，数字与 `vgruntime dump` 的输出一致。
- [ ] 报告页标题显示 OPENVELACLAW 署名与来源标注，正文为 Agent 产出内容。
- [ ] 断网后刷新报告页，显示固件 `runtime-report.md` 并标注本地来源，无 Agent 文本。
- [ ] 已有历史 `daily-*.md` 但当天没有新产物时，页面不得把历史文件当今天的报告展示。
- [ ] `scripts/stage1_agent_ops_accept.ps1` 扩展后通过，`bash scripts/build.sh` 通过。
- [ ] 收尾按 `velaguard-board-inner-loop` 内环跑完并汇报。

## 依赖

- 依赖子任务 A 的 `vg_ai_report_validate` 与请求通道。
- 依赖 Gate 0：板端能完成一轮带工具的 LLM 调用。Gate 0 不通过时本任务不可交付，应改为书面说明加固件报告，且不得把固件模板冒充 Agent 产出。

## Notes

- 详细设计见 `design.md`，执行顺序见 `implement.md`。
- 历史唯一一次真实 Agent 报告是 `daily-20260228.md`（1796 B，`END status=ok iters=6 tools=6 elapsed=115s`），可作为格式与耗时的参考。
