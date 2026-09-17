# 执行计划：OPENVELACLAW 真实生成日报

前置：子任务 A 的 `vg_ai_report_validate` 与请求通道已就位，Gate 0 通过。`app/velaguard/` 用 NuttX 风格（2 空格、大括号换行）；`gui/` 用 LVGL 风格（4 空格、`if(`）。

## 步骤

### S1. skill 重写（`app/velaguard/vg_agent_seed.c`）

1. 按 `design.md` 第 2 节整段替换 `g_operations_report_skill[]`，删掉「不要用 dump 原文覆盖」「禁止 write_file 覆盖该文件」这类措辞。
2. 同步改 `g_heartbeat_md[]` 中指向日报的那句。
3. 确认 `write_seed_file()` 的内容比对逻辑能让已 provisioned 的板子拿到新文本（长度不同即重写，无需额外迁移）。

### S2. 来源判定（`app/velaguard/vg_ui_backend_board.c`）

1. `pick_latest_daily()` 改为只认 `daily-<今天>.md`，不再取字典序最大的 `daily-*`。
2. `board_read_latest_report()` 按 `design.md` 第 3 节的五步流程改写：先刷新固件报告，再试当天 Agent 产物并校验，通过则 `from_agent = true`，否则回退固件报告。
3. `vg_ui_report_snapshot_t` 加 `bool from_agent`（`gui/main/ui/model/vg_ui_backend.h`），`g_report_snap` 同步填；PC mock 填 false。

### S3. 主动发起

1. `board_request_daily_report()` 改为经 A 的通道真实发起，删除旧的空实现与过时注释，新注释说明主动轮次由板端 worker 发起、heartbeat 仍保持 gated。
2. worker 每 tick 按 `design.md` 第 4 节的四个条件判断是否发起，写当天标记文件。
3. 产物出现且校验通过后切 `READY`；超时沿用 A 的 deadline 切 `ERROR`。

### S4. 页面标注（`gui/main/ui/pages/vg_page_report.c`）

`apply_snapshot` 按 `from_agent` 分派标题与来源行，其余分支不动。

### S5. 取证与脚本

1. 扩展 `scripts/stage1_agent_ops_accept.ps1`：按顺序断言 `Executing tool: get_current_time`、`Executing tool: run_shell`、`Executing tool: write_file`、`END status=ok`，再确认 `/data/velaguard/reports/daily-<当天>.md` 存在且 mtime 新鲜。
2. 补 NSH 回读上一轮的 `status/iters/tools/elapsed`（新增 `vgagent` 或扩展 `vgruntime dump`），供屏幕与验收使用。

### S6. 离线回归

拔网后刷新报告页，确认显示固件 `runtime-report.md` 且标注本地来源；确认已有历史 `daily-*.md` 时不会把它当今天的报告。

## 验证门禁

| 门 | 命令 | 通过标准 |
|---|---|---|
| G0 | Gate 0（见子任务 A） | 先确认板端能完成一轮带工具的 LLM 调用，否则本任务不可交付 |
| G1 | `bash scripts/build.sh` | 默认 `velaguard-lvgl` 通过 |
| G2 | `scripts/stage1_flash_and_accept.ps1` | COM3 开机正常 |
| G3 | `scripts/stage1_agent_ops_accept.ps1`（已扩展） | 工具链顺序、`END status=ok`、当天文件落盘三项通过 |
| G4 | 板上查看报告页 | 标题含 OPENVELACLAW 与来源标注，正文数字与 `vgruntime dump` 一致 |
| G5 | 板上断网序列 | 回退固件报告并标注本地来源，无 Agent 文本 |
| G6 | 历史文件干扰用例 | 只存在旧 `daily-*` 时页面显示固件报告，不显示旧文件 |

每个子任务收尾按 `velaguard-board-inner-loop` 的内环跑完再汇报，格式为命令 / pass-fail / 关键日志一行，失败先自行修一轮；COM3 抢占失败按该 skill 的用户介入条款停下问用户。

## 回滚点

- **R0** S1 的 skill 文本可单独回退，回到「复述固件报告」的旧行为。
- **R1** S3 的发起分支删掉后，退化为今天的「手动刷新才生成」，S2、S4 的逻辑仍然正确。
- **R2** `from_agent` 保持 false 时页面行为等同今天。

## 执行结果（2026-09-16）

### 落地位置

| 内容 | 文件 |
|---|---|
| skill 授权 Agent 自己写日报 | `app/velaguard/vg_agent_seed.c` 的 `g_operations_report_skill[]` |
| 板端按天主动发起 | `app/velaguard/vg_ui_backend_board.c` 的 `daily_maybe_request()`、`board_request_daily_report()` |
| 来源判定与回退 | 同文件的 `pick_agent_daily()`、`board_read_latest_report()` |
| 页面标题与来源行 | `gui/main/ui/pages/vg_page_report.c` 按 `from_agent` 分派 |
| 校验 | `vg_ai_report_validate()`（首行标记、date=、source=agent、长度、新鲜度） |
| 验收 | `scripts/stage1_agent_ops_accept.ps1` |

### 门禁

| 门 | 结果 | 证据 |
|---|---|---|
| G0 | 通过 | 见子任务 A 的 `research/gate0-ask-20260916-r3.txt` |
| G1 | 通过 | `bash scripts/build.sh` |
| G2 | 通过 | `scripts/stage1_flash_and_accept.ps1`，`stage1_agent_accept pass=8 fail=0` |
| G3 | 通过 | `research/board-driven-daily-20260916-r1.log`：`vghmi: daily report requested`、`round submitted: 124 bytes`、工具行 read_file/get_current_time/run_shell/run_shell/write_file、`END status=ok iters=4 tools=4 llm_ms=191090 elapsed=194s`，产出 `daily-2026-09-16.md`（824 B） |
| G4 | 待肉眼复核 | `vgagent status` 的 gen 由 0 增至 1，说明这一轮由板端发起并完成；报告页署名需要在屏上确认 |
| G5 | 见下方离线回归 | |
| G6 | 由 `pick_agent_daily()` 只认当天文件名覆盖，host 与代码路径已具备 | 历史 `daily-20260912.md`、`daily-20260913.md` 与 `daily-20260228.md` 在板上共存时未被选中 |

### 计划外的两处改动

1. **请求里的文件名由板端拼好**。原请求写 `daily-<当天日期>.md` 字面量，要模型自己把日期翻成路径；改成 C 里用 `report_stamp()` 拼完整路径再发。
2. **轮次归属**。日报与告警建议共用一条通道，`vg_agent_round_queue_owned()` 记录发起方，日报侧在轮次结束后释放通道，不再让告警侧去读它没请求过的产物。详见子任务 A 的 implement.md。

### 一轮的真实成本

`elapsed=194s`、`llm_ms=191090`，4 次工具调用。这解释了验收脚本为什么必须留 10 分钟窗口：板端要等时钟同步才发起，一轮本身又是三分钟以上。

## 执行结果补充（2026-09-17）

### 构建与单测

- `bash scripts/build.sh` 退出码 0，0 个 error、0 个 warning，产物 `.debug/nuttx.hex` 为 3508039 字节（2026-09-17 08:09）。上一轮的 3 个 warning 里属于本子任务的两处：`vg_ui_backend_board.c` 的 strip_markdown 里 `char *line_end` 去掉 const；`daily_maybe_request()` 里的局部变量 `st` 与 `struct stat st` 重名，改名为 `rst`。
- `make -C app/velaguard/host_tests test` 13 个测试二进制全绿；headless `ctest --test-dir .debug/hmi-headless` 5/5 通过。

### 板端 agent-ops 验收

`scripts/stage1_agent_ops_accept.ps1` 结果 pass=19 fail=1。与日报相关的通过项：

- 脚本先删掉当天的 daily 文件并断言确实删掉了，随后板端没有人提问就自己把当天日报重新生成出来。
- 日报文件 `/data/velaguard/reports/daily-2026-09-17.md` 首行 `AI-DAILY v1`，第二行 `date=2026-09-17`，第三行 `source=agent`，正文分通信质量、点位在线、异常时间线三节，里面是真实数字：累计发送 141 帧、成功 0 次、14 个点位、本次上电运行 172 秒。
- 审计日志 `/data/velaguard/logs/agent_tools.log` 存在且记录了本轮的 run_shell / write_file / read_file 调用。
- `vgagent status` 能回读轮次通道状态。
- 唯一失败项是手动 `ask` 那一步断言串口出现 `Executing tool:` 没通过。原因是这一问排在板端自己触发的建议轮之后，120 秒的采集窗口被通道排队占满，属采集窗口不够，不是链路问题；同一轮里 `agent replied` 已通过，审计日志也证明工具确实被调用过。

### 一轮真实开销

来自更早一次专门抓的串口记录（证据文件 `.trellis/tasks/09-16-ai-contract-request-channel/research/board-driven-daily-20260916-r1.log`）：`vghmi: daily report requested`、`round submitted: 124 bytes`，工具行依次是 read_file、get_current_time、run_shell、run_shell、write_file，`END status=ok iters=4 tools=4 llm_ms=191090 elapsed=194s`，产物 824 字节。

### 待办

- 屏幕肉眼确认没有做：报告页标题与来源行尚未在板上肉眼核对。
- 断网降级尚未在板端复现。
- `daily-2026-09-17.md` 的板端内容里中文在串口日志中是乱码，那是控制台编码问题，文件本身是 UTF-8 且已过板端校验。

## 断网降级（2026-09-17）

用 `vgnet inject rj45 down` 与 `vgnet inject wifi down` 在软件上把两条链路置为不可用，删掉当天的 `daily-2026-09-17.md` 与 `alarm_advice.txt`，然后连续观察五分钟。串口日志里 `[vela_tls] net_connect token-plan-cn.xiaomimimo.com:443 ret=0x42`、`[agent] LLM call failed`、`END status=fail` 反复出现，本地采集、帧统计、配置读取照常；窗口结束时 `/data/velaguard/reports` 里两个文件都没有重新出现。

也就是说断网时板端不会凭空产出任何 AI 文本或其占位文件，报告页读不到当天日报就按设计回退固件统计的 `runtime-report.md` 并标注本地来源，告警页回到规则摘要。页面文案与渲染路径由 headless 覆盖，屏幕肉眼确认仍待做。

最后用 `vgnet inject rj45 auto` 与 `vgnet inject wifi auto` 恢复，随后 `vgnet status` 回到 `state=online_rj45`。

证据：`/tmp` 下的三轮串口捕获已并入本任务 research 目录（`offline-20260917-r*.log`）。

## 把一轮压到两步（2026-09-17）

用户指出：如果 MiMo 侧没问题，一份日报要三分钟以上才出来，就该先怀疑自己这边的实现。核对下来确实是我们的问题，而且不在固件逻辑，在 Skill 的写法。

单次 LLM 调用有 120 s 墙钟，但一轮 ReAct 的耗时是按**迭代次数**累加的：`END status=ok iters=4 tools=4 llm_ms=191090 elapsed=194s` 说明四次模型调用各花了约 48 s，`llm_ms` 是累加值。原来的 operations_report Skill 把三件互不依赖的事分成三条消息发（先 get_current_time，再 vgruntime dump，再按最差从站去 vgstats dump），每次都要重发一遍增长中的上下文，等于白等三倍时间。

改成两步：

1. 同一条消息里一次发出 `get_current_time`、`run_shell vgruntime dump`、`run_shell vgstats dump`（不带参数，也就是全部从站，这样就不依赖上一步的输出）。
2. `write_file`。

alarm_interpretation Skill 同样改成一条消息发齐 `vgstats dump`、`vgmodbus`、`vgcfg dump`，再写文件。两份 Skill 都写明了理由：每条消息里的调用会一次执行完，一条消息只发一个调用就多等一整轮。

这不是提示词风格问题，是**轮次预算**问题，所以写进了 Skill 正文而不是只写在请求里。
