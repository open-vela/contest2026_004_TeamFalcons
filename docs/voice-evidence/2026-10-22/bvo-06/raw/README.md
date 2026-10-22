# raw 目录说明

本目录只保留最小必要证据。

| 文件 | 说明 |
|---|---|
| `results.jsonl` | 21 次调用的逐条记录，含状态、分阶段耗时、字节数与 ASR 文本 |
| `tts-s1-bingtang.wav` | TTS 返回的可听样本，24000 Hz 单声道 16 位，6.080 秒 |
| `tts-s2-long.mp3` | TTS 返回的 MP3 样本，65256 字节 |

以下文件在取证后删除，可由 `tools/make-samples.sh` 重新生成，避免仓库膨胀：

- `1s.wav`、`5s.wav`、`15s.wav`：SAPI 原始合成输出
- `tts-s1-default.wav`、`tts-s1-pcm.raw`、`tts-s1-pcm-stream.raw`、`tts-s2-long.wav`：与保留样本同批的 TTS 输出，格式与字节数已记录在 `02-host-verification.md`

没有保存任何请求头、密钥或完整响应体。
