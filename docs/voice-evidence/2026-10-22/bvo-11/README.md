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

## 5. 未验证与限制

- 未烧录，未用真实麦克风、扬声器和云端 ASR/TTS 跑过一轮。24 kHz 直接播放能力未验证，因此选择重采样到已验证的 16 kHz 立体声格式；该选择基于能力未知，不是实测结论。
- 模型名 `mimo-v2.5-asr`、`mimo-v2.5-tts` 与音色 `冰糖` 未在真实端点确认；Base URL、模型与密钥必须成对核对。
- 未做 30 次成功率和端到端时延统计。
- 云端对录音与识别文本的保留政策未知，本仓库不承诺云端删除。
