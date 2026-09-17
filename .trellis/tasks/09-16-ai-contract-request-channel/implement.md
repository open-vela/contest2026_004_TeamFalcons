# 执行计划：AI 文本契约与请求通道

前置：读 `design.md` 与父任务 `prd.md`。风格按目录走——`app/velaguard/` 用 NuttX 风格（2 空格缩进、大括号换行），`gui/` 用 LVGL 风格（4 空格、`if(`）。

## 步骤

### S0. Gate 0：板端 LLM 轮次复测（最先做，不通过就停下功能 2）

1. 跑 `powershell.exe -ExecutionPolicy Bypass -File scripts/serial_net_test_ask.ps1`，或按该脚本的步骤手工在 COM3 上 `ask 读取从站1温湿度`。
2. 判定三条同时成立：返回中文回答而非 `Sorry, I encountered an error.`；日志出现至少一行 `Executing tool: `；出现 `END status=ok iters=N tools=M`。
3. 结果落盘为 `research/gate0-ask-<日期>.txt`，并记录 `CONFIG_VG_AGENT_AUTOSTART=y` 下 agent 循环是否已自启。
4. 不通过时：停止功能 2，把结论写进本任务 notes 并告知用户；功能 1 的契约与降级部分继续做。`ask` 不在 `velaguard-board-inner-loop` 覆盖范围内，若 LLM key 未配或 COM3 抢占失败，按该 skill 的用户介入条款停下来问用户，不据此宣称成功。

### S1. `vg_ai_contract.{h,c}`（纯 C）

1. 按 `design.md` 第 3 节定义结构体与函数；头文件用 `__APP_VELAGUARD_VG_AI_CONTRACT_H` 卫哨，风格对齐 `app/velaguard/vg_alarm_eval.h`。
2. 解析器按行推进，键固定偏移比对；每行先查长度再查字符；`boot`/`req` 与期望值比对；任何违规立即返回负值且不写 `*out`（先解析进栈上临时结构，全部通过后再整体拷贝）。
3. 请求构造器是纯函数：格式化浮点时对 `NaN`/`Inf` 输出 `-`，不输出 `nan`/`inf`；所有写入经统一的截断安全封装（参考 `.trellis/spec/backend/quality-guidelines.md` 的偏移累加禁令与 `ctx_append()` 饱和写法）。
4. 日报校验器检查首行 `AI-DAILY v1`、`date=` 等于期望日期、长度区间、`mtime` 新鲜度、UTF-8 合法性。

### S2. host 单测

1. 新增 `app/velaguard/host_tests/test_ai_contract.c`，沿用既有 `PASS`/`FAIL` 与返回非零的约定。
2. 在 `app/velaguard/host_tests/Makefile` 按 `test_alarm_eval` 的方式加编译规则、`test` 目标依赖与运行行、`clean` 列表。
3. 覆盖 `prd.md` 验收清单里的全部边界用例，另加构造器 → 解析器回环一致性的正例。

### S3. 板端请求通道

1. `app/velaguard/Makefile` 加 `ai_agent/include` 到 include 路径。
2. 在 `vg_ui_backend_board.c` 里加 backend 级 boot 随机数（初始化时由 device id 与单调时钟派生，非零）。
3. 在 `vg_hmi_file_worker_thread` 加调度分支，实现 `design.md` 第 4 节的状态机、pending 槽、110 s 超时与限流；回调只置标志与信号量，不做打印与文件 IO。
4. 只在本子任务内暴露最小的内部接口，B、C 通过后续子任务接入，不提前改界面。

### S4. 公共树只读加固（`velaguard/*` 分支）

1. 在 `/home/hello19y/openvela/packages/ai_agent` 切 `velaguard/*` 功能分支。
2. 按 `design.md` 第 5 节实现子命令门槛表、`runtime-report.md` 写保护、工具调用审计。
3. 公共树自带 `tests/` 下补审计格式化与门槛判定的单测。
4. 准备 fork + PR 材料；不使用任何 `scripts/openvela-*.patch`。

### S5. 文档与收尾

1. Trellis 步骤 3.3：把 `VGADV1` 格式与上限、校验器必须为纯 C 以便 host 测试、AI 文本降级规则写入 `.trellis/spec/backend/`。
2. 更新 `VelaGuard_项目手册.md` §10.1 相关段落，说明 `VGADV1` 是 JSON 骨架的板端投影。

## 验证门禁

| 门 | 命令 | 通过标准 |
|---|---|---|
| G0 | `scripts/serial_net_test_ask.ps1` | 中文回答 + `Executing tool: ` + `END status=ok`；证据落盘 |
| G1 | `make -C app/velaguard/host_tests test` | 全绿，含 `test_ai_contract` |
| G2 | `bash scripts/build.sh` | 默认 `velaguard-lvgl` 构建通过 |
| G3 | `scripts/stage1_flash_and_accept.ps1` | COM3 开机，`vgagent: ai_agent autostart ok` |
| G4 | 板上 NSH 手测 | 上一轮 `status/iters/tools` 可回读；超时后状态回到可重试 |
| G5 | 板上 NSH 手测只读加固 | `vgstats inject` / `vgnet inject` / `vgruntime report <path>` 被拒；`dump` / `status` 正常 |

每个子任务收尾按 `velaguard-board-inner-loop` 的内环跑完再汇报，格式为命令 / pass-fail / 关键日志一行，失败先自行修一轮。

## 回滚点

- **R0** S1、S2 只新增文件，删除即回到现状。
- **R1** S3 的调度分支可用编译期开关或直接删除该分支关闭，Agent 仍可手动 `ask` 驱动。
- **R2** S4 在独立公共树分支，未合入前对本仓无影响。

## 执行结果（2026-09-16）

### 门禁

| 门 | 结果 | 证据 |
|---|---|---|
| G0 | 通过 | `research/gate0-ask-20260916-r3.txt`：中文提问与中文回答正确往返，`Executing tool: read_file` 加 3 次 `run_shell`，`END status=ok iters=6 tools=5 llm_ms=105010` |
| G1 | 通过 | `make -C app/velaguard/host_tests test` 13 个用例全绿，含新增 `test_ai_contract` |
| G2 | 通过 | `bash scripts/build.sh`，SRAM 使用 51.18% |
| G3 | 通过 | `scripts/stage1_flash_and_accept.ps1`，`stage1_agent_accept` pass=8 fail=0 |
| G4 | 见 `research/vgagent-round-20260916-r*.txt` | `vgagent` NSH 探针 |
| G5 | 见 `research/vgagent-round-20260916-r*.txt` | 只读边界 |

### 过程中发现并修掉的两个真实缺陷

1. **TLS 写请求体必失败**（第 1 轮 Gate 0）：`tls_ctx_connect()` 把 socket 固定成阻塞模式，而 mbedTLS 的 `net_would_block()` 在阻塞 socket 上不返回 WOULD BLOCK，于是发送缓冲满时的 EAGAIN 变成硬错误 `MBEDTLS_ERR_NET_SEND_FAILED`，`tls_write_request()` 返回 -3。小请求能通、16 KB 的 LLM 请求体必失败。修法是新增 `tls_ssl_write_all()`：每次写限制 4 KB、短写与失败按 20 ms 退避重试至多 64 次，并记录原始 mbedTLS 码与 errno。公共树分支 `velaguard/llm-tls-send-retry`。
2. **工具重复把整轮判失败**（第 2 轮 Gate 0）：`check_tool_dup()` 在同名工具第 5 次调用时让循环 `break`，此时既没有 `final_text` 又没到迭代上限，于是落到 `AGENT_TRACE_FAIL` 并回一句通用报错——6 次工具调用的结果全被丢掉。我们的 skill 大量走 `run_shell`，正好撞在这条线上。已改为与迭代上限同等对待：调一次 `force_finish_reply()` 基于已有工具结果收尾。

### 另外两处影响演示的工具问题

1. 串口脚本没有设 `$port.Encoding`（.NET 默认 ASCII），中文提问发到板上变成 `?`。已给 17 个脚本加 `$port.Encoding = [System.Text.Encoding]::UTF8`。
2. PowerShell 5.1 读**无 BOM** 的 .ps1 会按 ANSI 解析，脚本里的中文字面量在解析阶段就已损坏。已给 11 个含非 ASCII 的脚本加 UTF-8 BOM。

### 板端联调又发现并修掉的两处（22:40 之后）

1. **两个消费者抢同一个轮次**。`vg_advice_tick()` 把任何完成的轮次都当成自己的：日报轮次结束后它去读 `alarm_advice.txt`，拿当前 `req` 去比对一份上一轮写的文件，解析器返回 `VG_AI_ERR_STALE`（-6），日志为 `[vgadvice] advice rejected rc=-6 (1840 bytes)`——文件本身没错，是归属判断错了。修法：`vg_agent_round_queue_owned()` 记录发起方（advice / daily / manual），消费者只在 `vg_agent_round_owner()` 等于自己时才读产物，并在读完 `vg_agent_round_clear()` 释放通道。`vgagent status` 同时打印 owner，便于板端排查。
2. **日报请求里的文件名交给模型拼**。原请求写的是字面量 `daily-<当天日期>.md`，要模型自己把日期翻译成路径。板端本地就知道当天日期，改为在 C 里用 `report_stamp()` 拼好完整路径再发，少一个出错点。

另外日报轮的 LLM 侧偶发 `[llm] Failed to parse API JSON` 导致整轮 `status=fail`。这属于链路抖动，`VG_DAILY_RETRY_MS`（300 s）会让板端自己重试，不做特殊处理。

### 计划之外的实现细节

- 新增 `app/velaguard/vgagent.c`（NSH 探针）：让轮次通道能独立验收，并回读上一轮状态，不必翻 syslog。新增 NSH 程序需要重跑配置阶段才能生成 `apps/builtin/registry/vgagent.bdat`，增量构建不会补，用 `bash scripts/build.sh --clean` 一次即可。
- 公共客户端新增 `velaclaw_ask_async()`：HMI 下 ReAct 循环是惰性启动的，必须先请求启动再入队。
- 子任务 B、C 的板端实现也已在本次一并落地（`vg_advice.c`、报告来源判定、日报主动触发），因此 G4/G5 的板测同时覆盖它们的通路。

## 执行结果补充（2026-09-17）

### 构建与单测

- `bash scripts/build.sh` 退出码 0，0 个 error、0 个 warning，产物 `.debug/nuttx.hex` 为 3508039 字节（2026-09-17 08:09）。上一轮的 3 个 warning 已全部修掉，涉及 B、C 文件的明细写在各自的补充记录里。
- `make -C app/velaguard/host_tests test` 无失败，13 个测试二进制全绿（含 test_ai_contract）。
- headless：`ctest --test-dir .debug/hmi-headless` 5/5 通过。

### 烧录与板端验收

- `scripts/stage1_flash_and_accept.ps1` 通过，`stage1_agent_accept` pass=8 fail=0。
- `scripts/stage1_agent_ops_accept.ps1` 结果 pass=19 fail=1。请求通道相关通过项：`vgagent status` 能回读轮次通道状态；审计日志 `/data/velaguard/logs/agent_tools.log` 存在且记录了本轮的 run_shell / write_file / read_file 调用。
- 唯一失败项是手动 `ask` 那一步断言串口出现 `Executing tool:` 没通过。原因是这一问排在板端自己触发的建议轮之后，120 秒的采集窗口被通道排队占满，属采集窗口不够，不是链路问题；同一轮里 `agent replied` 已通过，审计日志也证明工具确实被调用过。

### 公共树

变更已提交为 ai_agent 仓库 `velaguard/llm-tls-send-retry` 分支的 a3ecb9b，已 push 到 fork FoLeaf，并向上游开了 PR：https://github.com/open-vela/packages_ai_agent/pull/40 。内容为 TLS 大请求体分块写加重试、同名工具重复改为正常收尾、子命令门槛表驱动、runtime-report.md 写保护、工具调用审计、velaclaw_ask_async。

### 待办

- 断网降级尚未在板端复现。

## 代码校验与随之而来的修正（2026-09-17）

对未提交改动做了一轮独立校验，查出并修掉五处：

1. **解析器暂存量落在 8 KB 栈上**：`vg_ai_advice_parse()` 里的 `vg_ai_advice_doc_t tmp` 约 4 KB，而调用链是 HMI file worker（栈 8192）→ `vg_advice_tick()` → `advice_load()`。同栈上还有约 2 KB 的其它帧，余量太小。已改为模块静态暂存 `s_parse_scratch`，并注明调用者只有 file worker 与 host 测试。
2. **日报轮会抢走未被消费的建议轮**：轮次队列原设计在 DONE 状态允许被覆盖。开机即有告警时建议轮先发起，约三分钟后 DONE，若日报侧恰在同一 tick 请求，就会把状态覆盖掉，那份已经落盘的建议没人采纳。已改为 state 不是 IDLE 一律返回 -EBUSY；两个消费者各自清自己的结果，`vgagent ask` 收尾也补上 `clear()`，`vg_advice_tick()` 在活动告警清零时先释放自己的轮次再 drop。
3. **日报完成判定退化成「文件存在」**：`daily_maybe_request()` 原来只 `stat()`。一份日期行写错或缺少 `AI-DAILY v1` 的日报会让当天剩下的重试全部不再发起。已抽出 `agent_daily_load()`，页面与调度共用同一份读文件加校验的实现，两边都按校验结果判定。
4. **无 Agent 的 HMI 配置编不过**：`daily_maybe_request()` 用了 `vg_agent_round_*` 却没有宏保护，把函数体整体放进 `CONFIG_EXAMPLES_AI_AGENT_VELA`。
5. **boot 十六进制只认小写**：模型把 8 位随机数大写回填时整份被丢弃，且报 RANGE 而非 STALE。已接受 `A-F`，并在 `test_ai_contract` 补了大小写用例。

另外顺手补了一处：日报校验原先用 `strlen()` 取长度，文件里若有内嵌 NUL，被截断的前缀会被放行。现在先比对 `st_size` 与 `strlen`，不一致直接拒绝。

校验同时确认了这些是对的：AI 文本只有经校验的缓存这一个来源，没有能显示未校验内容的路径；`advice_load` 的文件 IO 在锁外；两个互斥量从不嵌套；请求构造与签名拼接都没有按 `snprintf` 返回值累加偏移。

已知但未改的遗留：手动刷新日报的入口没有限流，且 `VG_UI_REPORT_GENERATING` 没有任何赋值（改动前就是死状态）；`vg_ui_report_request(true)` 与 `vg_ui_alarm_advice_request()` 目前都没有调用者，属预留 API。这几项不影响本次验收，记在这里避免被当成已实现。

## 长时间运行后 Agent 总线被灌满（2026-09-17 观察）

连续跑了几十轮之后，板端出现 `[bus] Queue full, dropping message` 与 `[vgagent] round submit failed: -5`，此后所有轮次都递不进去，日报与建议都不再产出。`ps` 里能看到上一轮 `run_shell` 派生的 `popen -c vgmodbus` 与 `vgmodbus -a 1 -r 0 -c 2 -n 3 -i 2000` 任务一直挂着没有结束，Agent 的 ReAct 循环停在等它的位置，于是不再消费总线；冷启动后一切正常。这与 `09-12-heartbeat-llm-round-system-wedge` 记的是同一类故障，不是本次改动引入的。

我们这一侧有一个会加速它的错误：提交失败时没有退避。建议流失败后每 `VG_ADV_MIN_GAP_MS`（20 s）重试一次，日报流失败后每个 worker tick（100 ms）都重试，而后者的队列深度只有 16，几分钟就能被自己灌满。已改成：

- 建议流提交失败也计入 `VG_ADV_RETRY_MS` 退避。
- 日报流区分两种失败：`-EBUSY`（另一个流还没消费完自己的结果，下一个 tick 重试）与其它错误（按 `VG_DAILY_RETRY_MS` 退避），并打印 `vghmi: daily report not delivered (rc)`。

这不能修好 Agent 侧卡住的问题，但能让板端在那种状态下保持安静，而不是持续往一个没人读的队列里塞请求。

## 第二轮校验与三处修正（2026-09-17）

独立校验确认前面五处改对了，同时指出三处新问题，都已修：

1. **未认领的结果会永久占住通道**（我在这一轮引入的）。把入队条件收紧成「非 IDLE 一律 -EBUSY」之后，owner 为 NONE 的终态没有任何 HMI 流程认领：手工 `vgagent ask` 中途被 Ctrl-C 掉，回复落地成 DONE(owner=NONE)，此后建议与日报都只清自己的结果，队列永远返回 -EBUSY，两条 AI 流一起瘫痪，只能手动 `vgagent clear` 或重启。修法是给 `vg_agent_round` 加 `vg_agent_round_reclaim(mine, grace_ms)`：任何非自己属主的终态，停留超过 `VG_AGENT_ROUND_RECLAIM_MS`（30 s）就释放。两个消费者每个 tick 各调一次。
2. **请求里的格式示例与解析器矛盾**：为省字节，我把条目模板压成了一行 `id=... epoch=... sev=... unres=0|1`，而解析器要求这四个字段各占一行，模型照字面输出就会被 `vg_ai_id_valid` 判非法、整轮按 RANGE 丢弃。已改回四行，实测请求 1410 B，仍在 1536 以内。
3. **建议流把 -EBUSY 也当成需要长退避的失败**：日报流已经区分，建议流没有。已对齐，只有真正的投递失败才退避 5 分钟。

另外把 `get_current_time` 补进 HEARTBEAT.md 的工具白名单（日报 Skill 第一步要用它）。
