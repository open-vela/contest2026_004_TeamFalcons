# BVO-11 云语音适配：ASR 上传、识别文本、TTS 播放准备、取消与静音

日期：2026-10-22。状态：主机实现与构建接入完成，未烧录，未用真实麦克风和云端服务验证。

## 1. 交付

| 文件 | 内容 |
|---|---|
| `app/velaguard/vg_voice_cloud.h/.c` | ASR 与 TTS 适配，流式上传/接收，失败清理 |
| `app/velaguard/vg_voice_pcm.h/.c` | WAV 探测与单声道化、24k→16k 重采样、流式 WAV 写入、流式 base64、SSE 行解析 |
| `app/velaguard/vg_voice_round.h/.c` | 一轮语音的编排：按键、录音、ASR、Agent、回答、TTS、播放、清理 |
| `packages/ai_agent/include/agent_http.h` | 公开的流式 HTTP 接口、错误码、截止时间与取消 |
| `packages/ai_agent/src/infra/vela_tls.c` | 增量 chunked 解码、流式收发、按剩余预算设置每片超时 |
| `packages/ai_agent/src/llm/llm_proxy.c` | `llm_http_call_stream` 与 `agent_http_post_stream` |
| `app/velaguard/Kconfig`, `scripts/configs/velaguard-lvgl.defconfig` | ASR/TTS 模型名与音色 |
| `app/velaguard/host_tests/test_voice_pcm.c` | 重采样、WAV、base64、SSE 解析 |
| `packages/ai_agent/tests/test_req_ctl.c` | 共享截止时间与取消 |

## 2. 链路

1. USER1 按下开始录音，松开提交；录音 15 秒硬上限由 BVO-08 状态机控制。
2. 录音为 16 kHz 单声道 WAV。ASR 前降混为单声道副本，按 4 KB 分片 base64 编码后作为请求体流式发送，不再把 640 KB 的 base64 与 JSON 同时放进堆。
3. 识别文本为空返回 `VG_VC_EMPTY`，界面提示重说；文本超长返回格式错误，不截断后继续。
4. 回答文本交给 TTS，请求 `pcm16` 与 `stream:true`。24 kHz PCM 先写入临时文件，再按 15360 样本（3 的整数倍）分块重采样到 16 kHz，写成 16 kHz 立体声 WAV 交给 NxPlayer。
5. 识别结束立即删除录音与单声道副本；TTS 结束删除 24 kHz 中间文件；失败时删除未完成的输出。

## 3. 截止时间、取消与回收

- 识别、等待 Agent、合成共用同一个 `vela_req_ctl_t`，截止时间来自会话的单调时钟。任何阶段都只取剩余时间，不会重新领取 30 秒。
- 每个 socket 读写的超时取剩余预算与该片上限（500 ms）的较小值，取消标志在约 500 ms 内被看到。
- 传输函数只有单一退出点，返回前执行 `shutdown()` 并释放 TLS 上下文；请求体与响应都通过回调流式处理，不保留整包。
- 取消后不再发起新请求；播放中取消会停止 NxPlayer 并等待任务空闲，2 秒内未空闲则结束本轮并提示。

## 4. 测试

主机测试（`host-tests.log`）：

- `test_voice_pcm`：24k→16k 整数 3:2 重采样、WAV 头、流式写入短写、base64 编解码、SSE `data:` 行与 `[DONE]`。
- `test_req_ctl`：无截止时间、剩余时间、单调回绕、取消、单预算被三个阶段共享。

构建：`bash scripts/build.sh`，退出码 0，flash 1,471,020 B（干净构建），sram 336,752 B（64.23%），见 `build.log`。镜像 SHA256 见 `firmware.sha256`。

## 5. 板端结果

2026-10-22 已在真实固件上分段跑通，细节与日志见 [../bvo-14/README.md](../bvo-14/README.md)：

- 录音 2 秒：samples=32120，dropped=0，临时文件清零。
- 云 ASR：160,044 B 合成样本，请求 213,561 B，HTTP 200，转写与样本一致。
- 云 TTS：84,480 B PCM，生成 16 kHz 双声道 WAV 112,684 B。
- 播放：NxPlayer 接受，`play rc=0`，任务回到 idle。
- 连续 10 次 ASR（[board-asr-loop.log](board-asr-loop.log)）：9 次 200，用时 9.2 至 10.3 s；1 次 `rc=net` 在 6.89 s 返回，下一次立即成功。没有复现 14:40 的挂起。
- 300 ms 预算：1.05 s 返回 `rc=timeout`，随后完整调用成功，见 [../bvo-07/board-deadline-recovery.log](../bvo-07/board-deadline-recovery.log)。

## 6. 未验证与限制

- 没有真人语音：识别质量、口音、噪声下表现未测。
- 24 kHz 直接播放能力未验证，因此重采样到已验证的 16 kHz 立体声；这是按已验证能力做的选择。
- 模型名 `mimo-v2.5-asr`、`mimo-v2.5-tts` 已在真实端点返回 200；音色 `冰糖` 是否被端点接受未单独核对（板端请求成功，未记录 voice 字段的回显）。
- 30 次成功率与端到端时延仍缺；目前只有 10 次连续样本。
- 云端对录音与识别文本的保留政策未知，本仓库不承诺云端删除。
