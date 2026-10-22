# BVO-07 统一截止时间、可取消传输与资源回收

日期：2026-10-22。状态：传输与预算实现完成，主机测试通过；回收时间和堆峰值需要真机测量。

## 1. 交付

| 仓库 | 文件 | 内容 |
|---|---|---|
| ai_agent | `include/agent_http.h` | 公开的 `vela_req_ctl_t`、请求体源、响应汇、错误码与剩余时间计算 |
| ai_agent | `src/infra/req_ctl.h` | 兼容旧调用的薄封装 |
| ai_agent | `src/infra/vela_tls.c` | `vela_https_request_stream()`：流式请求、增量 chunked 解码、按预算设置每片超时、单出口回收 |
| ai_agent | `src/llm/llm_proxy.c` | `llm_http_call_stream()` 与 `agent_http_post_stream()` |
| ai_agent | `tests/test_req_ctl.c` | 截止时间、回绕、取消、三阶段共享一个预算 |
| 竞赛仓 | `app/velaguard/vg_voice_cloud.c` | 识别与合成使用同一个 `vela_req_ctl_t` |
| 竞赛仓 | `app/velaguard/vg_voice_round.c` | 工作线程、取消标志、播放停止与清理 |

## 2. 语义

- **一个截止时间**：会话在提交时算出 `deadline_ms`（单调时钟），识别、等待 Agent、合成三个阶段都只读取它。`vela_req_remaining_ms()` 用无符号差值处理回绕；0 表示没有截止时间，不是已过期。
- **每片超时**：socket 读写的超时取 `min(剩余预算, 500 ms)`，因此取消标志最迟在约 500 ms 内被看到，且任何阶段都不会重新获得 30 秒。
- **取消**：按键、浮层关闭、播放中断都设置同一个 `cancel` 标志并唤醒工作线程。传输层在每个读循环检查它，返回 `AGENT_HTTP_ERR_CANCELLED`。
- **回收**：传输函数只有一个出口，返回前 `shutdown()` 并释放 TLS 上下文与 socket。请求体与响应都通过回调流式处理，不保留整包；识别结束立即删除录音，合成结束删除中间 PCM，本轮结束删除全部临时文件。
- **不取消后台任务**：语音轮次只设置自己的标志，不触碰日报或告警轮次；排队通过 `VG_AGENT_ROUND_OWNER_VOICE` 与既有合并规则完成，同一时刻最多一个语音问题在等待。

## 3. 内存

| 项 | 位置 | 大小 |
|---|---|---|
| TLS 读缓冲 | `vela_https_request_stream` 栈 | 2 KB |
| 请求体分片 | 同上 | 4 KB |
| 响应缓冲 | `vg_voice_cloud_asr/tts` 栈 | 4 KB |
| 回答显示缓冲 | `vr_stage_agent` 栈 | 2.5 KB |
| 工作线程栈 | 堆 | 32 KB |
| 录音与中间音频 | eMMC | 最大约 480 KB |

识别阶段栈上合计约 10 KB，加上 mbedTLS 握手，因此工作线程栈设为 32 KB 并显式 `pthread_attr_setstacksize`；默认 pthread 栈不足以承载这条调用链。

流式发送避免了 BVO-06 估算的 1.2 MB base64 加 JSON 峰值。流式接收避免了 418 KB SSE 正文加 307 KB PCM 的峰值。

## 4. 测试

- `test_req_ctl`：无截止时间、剩余时间递减、单调回绕、取消、每片超时、三个阶段共享一个预算。结果 `test_req_ctl: ok`。
- `test_http_body`：既有分帧测试仍通过。
- `test_voice_pcm`：流式 base64、SSE 行解析、流式 WAV 写入。
- 完整主机回归见 `../bvo-11/host-tests.log`，全部通过。

## 5. 板端截止与回收

[board-deadline-recovery.log](board-deadline-recovery.log)，2026-10-22 18:19，固件 SHA256 `3d6cbc1c...f9039941`：

| 步骤 | 命令 | 结果 |
|---|---|---|
| 300 ms 预算 | `vgvoice asr test.wav 300` | `rc=timeout status=0 req=213561 resp=0 ms=1050` |
| 紧接着完整调用 | `vgvoice asr test.wav` | `rc=ok status=200 resp=533 ms=7950`，转写正确 |
| 堆 | `free` | used 834,384，maxused 918,592，maxfree 5,795,432 |

300 ms 预算在 1.05 秒内结束，超出预算约 750 ms，来自 500 ms 的切片接收超时与收尾；没有拖到 30 秒。紧接着的同一条链路请求成功，说明连接和缓冲已经回收，不是靠重试掩盖。堆已用比开机多 136 字节。

## 6. 整轮预算实测

[board-round-budget.log](board-round-budget.log)，2026-10-22 19:17-19:21，合成语音提问“现在有哪些异常”，无在线从站：

| 阶段 | 观测 |
|---|---|
| ASR | 6.73 s；另有一次 2.00 s 返回 `rc=net` |
| Agent | 10.69 至 16.36 s，异常意图需要两轮模型调用 |
| TTS | 剩余 7 至 9 s 内未完成，`rc=timeout pcm=0` |
| 结果 | 文字保留，状态为 RESULT 且 `failed=1`，可重试播报 |

ASR 与 Agent 合计 17.4 至 23.1 s，30 秒预算下 TTS 只有 7 至 9 秒。云端模型单次首字节在 3 至 19 s 之间波动，因此整轮能否在 30 秒内到达可播放音频取决于服务端当时的状态，不能保证。当前超时行为是保留文字并标记录音失败，不是丢结果；但“30 秒内出声”这一条按实测没有稳定达标。

可选处理（属于产品取舍）：把异常回答的第二轮模型调用省掉，直接用工具快照渲染（预计省 4 至 15 s）；或经用户同意放宽预算。本轮没有擅自改动产品合同。

## 7. 未验证

- 按键取消到资源回收的 2 秒目标仍未测：本轮验证的是截止时间到期，不是人工取消。
- 一轮语音期间的堆空闲、最大空闲块、socket 数量和线程数量没有板端采样。
- 语音的流式连接不复用连接池，与日报等后台请求同时进行时会有两条 TLS 连接。并发内存峰值未测量，也没有全局并发上限；这是已知限制。
- 未做 429、DNS 失败、响应截断的板端注入，只在传输层按错误码分类。
