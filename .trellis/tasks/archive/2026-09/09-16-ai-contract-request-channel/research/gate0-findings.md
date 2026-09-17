# Gate 0 排查记录（2026-09-16）

## 结论摘要

| 轮次 | 结果 | 关键证据 |
|---|---|---|
| 第 1 轮（修复前） | 失败 | `[vela_tls] Handshake OK` 后 `Write request failed: -3`，`END status=fail iters=1 tools=0` |
| 第 2 轮（修 TLS 后） | 部分通过 | `Executing tool: run_shell` 出现，`iters=6 tools=6`，无 TLS 错误；但 `END status=fail`，回 `Sorry, I encountered an error.` |
| 第 3 轮（修工具重复降级后） | 见 `gate0-ask-*.txt` | —— |

## 1. TLS 写失败（已修）

**现象**：到 `token-plan-cn.xiaomimimo.com` 的握手成功，紧接着写请求体时返回 -3（`VELA_TLS_ERR_WRITE`，`packages/ai_agent/src/infra/vela_tls.h:32`）。同一板上到 `www.baidu.com` 的 HTTPS 正常（HTTP 200），说明网络与证书链没问题。

**根因**：`tls_ctx_connect()` 在 `packages/ai_agent/src/infra/vela_tls.c` 里调 `mbedtls_net_set_block()` 把 socket 固定成阻塞模式，而 mbedTLS 的 `net_would_block()`（`apps/crypto/mbedtls/mbedtls/library/net_sockets.c`）明确写着「Never return 'WOULD BLOCK' on a blocking socket」——它先看 `O_NONBLOCK` 标志，是阻塞就直接返回 0。于是发送缓冲满时 `write()` 返回的 EAGAIN 不会被翻译成 `MBEDTLS_ERR_SSL_WANT_WRITE`，而是变成硬错误 `MBEDTLS_ERR_NET_SEND_FAILED`，`tls_write_request()` 就返回 -3。小请求（baidu）放得下缓冲所以能通，16 KB 的 LLM 请求体放不下就失败。

**修复**（公共树分支 `velaguard/llm-tls-send-retry`）：新增 `tls_ssl_write_all()`，把每次写入限制在 4 KB、把短写与失败按 20 ms 退避重试至多 64 次，并记录原始 mbedTLS 码、errno 与 `F_GETFL` 便于后续诊断；原先把 `WANT_WRITE` 当忙等的循环一并去掉。重试是安全的：mbedTLS 把未发出的尾部留在 `ssl->out_left`，下次调用会先冲刷。

**验证**：第 2 轮日志里出现 `[vela_tls] Reusing pooled connection`（连接池路径也恢复正常）且全程无 `Write request failed`。

## 2. 工具重复把整轮判失败（已修）

**现象**：第 2 轮跑完 6 次迭代、6 次真实工具调用（`[tools] Executing tool: run_shell`），却以 `END status=fail iters=6 tools=6` 收尾并回 `Sorry, I encountered an error.`。

**根因**：日志里 `[agent] Tool name repeat limit (run_shell called 5 times)` —— `check_tool_dup()`（`src/core/agent_loop.c:948`）在**同名工具**连续调用第 5 次时返回 true（`AGENT_TOOL_NAME_REPEAT_MAX = 4`，只看工具名不看参数）。循环收到 true 后直接 `break`，此时 `final_text` 仍为空且迭代数没到上限，于是落到循环后的 `else { agent_trace_end(&trace, AGENT_TRACE_FAIL); }` 分支，既不产出回答也不保留已取到的数据。

这对我们的场景很关键：skill 里 `alarm_interpretation` 要连调 `run_shell` 三次（`vgstats`、`vgmodbus`、`vgcfg`），`operations_report` 要两次，都在同名计数上累加。

**修复**：新增 `tool_break` 标志，把这条 break 与迭代上限同等对待——调一次 `force_finish_reply()` 让模型基于已取得的工具结果收尾，trace 按是否真的拿到回答记 `OK` 或 `FAIL`。同时把 `force_finish_reply()` 的日志与提示词从「迭代上限」泛化为传入原因，不再误导。

## 3. 串口脚本的中文问题（已修，两处）

1. **线路编码**：`New-Object System.IO.Ports.SerialPort` 的 `Encoding` 默认是 ASCII，中文写出去就变成 `?`。已给 17 个脚本加 `$port.Encoding = [System.Text.Encoding]::UTF8`。
2. **脚本解析编码**：PowerShell 5.1 读**无 BOM** 的 .ps1 会按系统 ANSI 解析，脚本里的中文字面量在解析阶段就损坏，光改线路编码不够。已给 11 个含非 ASCII 的脚本加 UTF-8 BOM。`stage1_agent_ops_accept.ps1` 里那句 `ask 按 operations_report Skill 生成今日运营日报…` 正是受影响的一例。

前两轮日志里 `[agent] ask: ??????1?????????` 就是第 1 项造成的。

## 4. 时基：板端 deadline 必须用单调时钟

`[trace] END ... elapsed=198s` 与 `llm_ms=17460` 差距过大：`agent_trace_end()` 用 `gettimeofday()` 求差，而板子开机后 `vgtime` 会同步一次北京时间，时钟前跳会把 elapsed 撑大；`llm_ms` 用 `CLOCK_MONOTONIC`，是可信值（单轮 LLM+工具约 17 s）。

**对设计的影响**：子任务 A 里板端请求的 `deadline_ms` 以及 20 s / 300 s 的限流间隔，都必须基于 `clock_gettime(CLOCK_MONOTONIC)` 计算，不能用墙上时钟，否则一次 NTP 同步就会让所有在途请求立刻超时。

## 5. 遗留观察

- `[shell] popen(...) exit=-1, 78 bytes` 紧跟 `[shell] Result: 107 bytes (exit=0)`：`popen` 的返回码在 NuttX 上不可靠，工具本身按内容长度判定成功，暂不处理。
- 单轮真实耗时约 17 s（6 次迭代、6 次工具调用），远低于 120 s 墙钟；日报 skill 限制在 4 次工具调用是合理的。
