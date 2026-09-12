# 心跳触发的 LLM 轮次导致系统静默挂死

## 现象（2026-09-12 板上实测，两次复现）

1. 16:12:42 自然心跳触发（`[heartbeat] Triggered agent check`），agent lazy loop 启动，
   LLM 配置加载成功，构造 OpenAI API 请求（tools JSON 15198 字节，mimo-v2.5），
   开始 Dispatching response（system:heartbeat 通道）。
2. 随后整个系统静默死亡：串口 console 无任何输出（连回车 echo 都没有），
   vgtime 3 分钟持久化停止（最后一次 16:11:15），无 panic/assert 打印。
   SWD 复位后系统恢复正常。
3. 该轮 LLM 未产出任何文件：daily-20260912.md 未生成，pending_alarm.txt 未消化，
   /data/agent/sessions/tg_console.jsonl 无增长。
4. 更早一次复现：串口盲发 ai_agent attach + heartbeat_trigger 后同样全静默。

## 关键对照

- CLI 交互路径（vela> ask）历史上正常（stage1_agent_accept PASS，14:50 同板）。
- 挂死仅出现在心跳 system 通道触发的 lazy agent_loop LLM 轮次。
- 自启本身无嫌疑：手动 daemon 与自启 daemon 在心跳触发时刻运行态等价；
  此前无人发现是因为手动部署时心跳同样会 30 分钟后触发，只是演示会话短没等到。

## 怀疑方向

- run_shell 工具调用（vgstats/vgmodbus/vgcfg）与 HMI 采集线程/NSH console 的锁交互死锁。
- mbedTLS 堆耗尽导致无打印的 hard fault（HMI 构建无 mallinfo 命令，需其他手段）。
- agent loop 与 console/syslog 锁死锁（最后日志停在 Dispatching response）。

## 待办

- [ ] 抓死前最后日志（连续串口抓取已具备，疑点在 tool 执行阶段，需更长窗口）
- [ ] HMI 构建下取 heap/stack 证据（procfs meminfo 或 JTAG）
- [ ] 对照 CLI ask 路径与心跳路径的代码差异（agent_loop lazy start）
- [ ] 修复或临时规避（演示期可用 vela> ask 驱动日报生成，已验证可行）

## 排查结论（2026-09-12 深入）

### 已确认机制

1. 崩溃 = 堆空闲块链表元数据被写坏。DEBUGASSERT 断言在 mallinfo 遍历时解引用坏指针
   0xf1552148（blink->flink），HARDFAULT 全系统停机。两次复现 BFAR/MMFAR 完全相同
   (0xf1552148)，说明是确定性的越界/释放后写入模式。
2. 崩溃链（-Og 调试构建 + sched backtrace，带行号）：
   agent_loop_task(agent_loop.c:1581) -> run_react_loop(:1269) ->
   add_tool_result_messages(:220) -> agent_tool_exec_streamed(agent_mem.h:272) ->
   agent_mem_safe_size -> mallinfo -> mm_foreach(mm_foreach.c:128) -> 断言。
   即第一轮 LLM（返回 1 个 read_file 工具调用）解析完成后、工具尚未执行时，
   堆已经是坏的。
3. ask 路径同样崩溃：vela> ask 按 operations_report Skill 生成日报（README 演示命令）
   走同一 ReAct+工具链路，串口同样静默挂死。ask hello（无工具调用）正常。
   => 崩溃与心跳/ask 通道无关，与工具调用轮次相关；演示主线在 HMI 构建上当前不可用。
4. 已审计为边界正确的代码：llm_proxy.c(llm_chat_tools/tool_calls 提取/http_direct/resp_buf)、
   vela_tls.c(tls_write_request/tls_read_response/decode_chunked/连接池)、
   agent_loop.c(add_assistant_message/add_tool_result_messages/inject_cron_context)、
   context_builder.c(本轮无截断, off=3489 < ctx 4096)。未审计：llm_parse.c、
   tls_ctx_connect/free 内部、mbedTLS 与 16KB agent loop 栈的余量。

### 260228 谜底（顺带破案）

vela_tls.c tls_ctx_connect 里有时钟护栏：时钟早于 2024 就 clock_settime 硬设为
1772275200（= 2026-02-28）。旧固件时钟是 1970 时，agent 首次 TLS 握手把系统时钟
强行设成 2026-02-28，get_current_time 因此取到该日期，日报才叫 daily-20260228.md。
时间同步上线后（恢复+SNTP 先于 agent 握手）该护栏不再触发。

### 复现工具

- .debug/hb_repro.ps1：同步等 vela> 提示符 + heartbeat_trigger + 330s 全程抓取，约 5 分钟一轮。
- .debug/ask_report_test.ps1：ask 驱动日报生成复现。
- 当前板上烧的是插桩调试构建（CONFIG_DEBUG_FEATURES/DEBUG_MM/SCHED_BACKTRACE/
  MM_RECORD_STACK，-Og），恢复正式构建：bash scripts/build.sh && scripts/flash.ps1。

### 修复归属与建议

- 修复在 packages/ai_agent（C2: 比赛仓不得直接改，走公共仓 PR）。
- 建议上游修法（按优先级）：
  1. HMI 构建的 agent loop 栈 16KB 对 TLS+JSON 轮次太紧，恢复 32KB 或把 TLS/解析
     移出该栈；先用 CONFIG_STACK_COLORATION 验证栈水位。
  2. llm_http_direct 绕过 resp_buf 扩容、把 4096 定死为响应上限，恢复 resp_buf_append 路径。
  3. context_builder.c 的 off += snprintf 模式存在截断后 size-off 下溢隐患（本轮未触发），
     应改为钳制 off。
- 赛前（9/20）规避：日报页素材改为离线生成——用无 HMI 的构建（velaguard-min 等堆余量
  正常）在同一 eMMC 上跑 agent 生成真实日报，或推动上游修复后回归。
