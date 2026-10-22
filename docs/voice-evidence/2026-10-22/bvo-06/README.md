# BVO-06 云 ASR/TTS 能力验证与接口合同

日期：2026-10-22。范围：官方资料核验、主机真实调用、格式与延迟测量、板端接入合同。本轮不改板端固件、不烧录、不接入完整语音链路、不做产品 UI，也不补做 BVO-05。

## 1. 结论摘要

| 能力 | 判定 | 依据 |
|---|---|---|
| MiMo ASR（mimo-v2.5-asr） | 主机可用 | 21 次真实调用中 ASR 13 次全部 HTTP 200；1/5/15 秒普通话样本逐字正确；UPS 与点位 ID 可识别 |
| MiMo TTS（mimo-v2.5-tts） | 主机可用 | 6 次合成全部 HTTP 200，另 1 次故意的错误模型请求返回 400；返回 24 kHz 单声道 16 位 WAV 或 PCM，可解码；TTS 音频回灌 ASR 可复原文本 |
| 板端接入 | 未验证 | 本轮未改固件。格式与内存差距已量化，见合同 |
| 30 秒端到端预算 | 未判定 | 只测到云语音段约 0.4 至 3.2 秒，Agent 段未测 |

两点必须先说清楚：

1. ASR 和 TTS 都不是 OpenAI 的 `/v1/audio/transcriptions` 或 `/v1/audio/speech`，而是复用 `/v1/chat/completions`，音频走 `input_audio` 内容块或 `audio` 参数。按 OpenAI 音频接口形状去实现会直接失败。
2. 现有凭据是 Token Plan 的 `tp-` 密钥，只对 `token-plan-cn.xiaomimimo.com` 有效。同一个密钥打到按量计费域名 `api.xiaomimimo.com` 返回 401 `invalid_key`。板端配置必须成对保存 Base URL 与密钥。

## 2. 官方来源与核查日期

| 项目 | 链接 | 页面更新时间 |
|---|---|---|
| ASR 接口 | https://mimo.mi.com/docs/zh-CN/api/audio/Speech-Recognition | 未标注 |
| ASR 使用指南 | https://mimo.mi.com/docs/zh-CN/usage-guide/Speech-Recognition | 2026-07-16 |
| TTS 接口 | https://mimo.mi.com/docs/zh-CN/api/audio/tts | 2026-07-17 |
| TTS 使用指南 | https://mimo.mi.com/docs/zh-CN/quick-start/usage-guide/audio/speech-synthesis-v2.5 | 未标注 |
| 速率限制 | https://mimo.mi.com/docs/zh-CN/api/guidance/rate-limit | 2026-10-20 |
| 错误码 | https://mimo.mi.com/docs/zh-CN/api/guidance/error-codes | 2026-07-15 |
| 按量计费 | https://mimo.mi.com/docs/zh-CN/price/pay-as-you-go | 未标注 |
| 模型下线公告 | https://mimo.mi.com/docs/updates/deprecate | 2026-10-09 |
| 隐私政策 | https://mimo.mi.com/docs/terms/privacy-policy | 生效 2026-10-10 |
| 服务协议 | https://mimo.mi.com/docs/terms/user-agreement | 生效 2026-07-07 |

核查日期：2026-10-22。以上为文档明确说明；实际测得与仍未知的部分分别在 `01` 和 `04` 中标注，不与文档混写。

## 3. 文件索引

| 文件 | 内容 |
|---|---|
| `01-official-vs-measured.md` | 官方能力与账户实测对照表，逐项标注文档、实测、未知 |
| `02-host-verification.md` | 样本来源、调用矩阵、识别结果、延迟与失败率 |
| `03-board-contract.md` | ASR/TTS 板端接口合同，含内存、TLS、超时、清理 |
| `04-gaps-next-steps.md` | 未满足项、阻塞原因、对 BVO-07 和 BVO-11 的建议 |
| `tools/verify_mimo_voice.py` | 主机验证脚本，凭据只从环境读取，带请求上限 |
| `tools/make-samples.sh` | 用本机 SAPI 中文语音生成 1/5/15 秒样本 |
| `tools/gen-test-audio.ps1` | SAPI 合成辅助脚本 |
| `tools/pad_wav.py` | 定长补零、重采样、声道转换 |
| `tools/inspect_audio.py` | WAV/PCM 解码与幅度统计 |
| `samples/` | 合成测试音频与 `manifest.txt`（含 SHA256 前 16 位） |
| `raw/results.jsonl` | 21 次调用的逐条记录，含分阶段耗时 |
| `raw/tts-s1-bingtang.wav` | TTS 返回的可听样本，24 kHz 单声道 |
| `raw/tts-s2-long.mp3` | TTS 返回的 MP3 样本 |

## 4. 复现

```bash
cd docs/voice-evidence/2026-10-22/bvo-06

# 1. 生成样本（需要 WSL 可调用 powershell.exe 与中文 SAPI 语音）
bash tools/make-samples.sh

# 2. 凭据只从环境读取，不写进命令历史
export MIMO_API_KEY=...   # 从 connect_info 或密码管理器读取
export MIMO_BASE=https://token-plan-cn.xiaomimimo.com/v1

# 3. 先确认账户可见的模型
python3 tools/verify_mimo_voice.py models

# 4. ASR 与 TTS
python3 tools/verify_mimo_voice.py asr --file samples/asr-5s-16k-mono.wav --seconds 5.0 --language zh
python3 tools/verify_mimo_voice.py tts --text "测试内容，UPS 一号机输出电压二百二十伏。" --format wav --save-audio /tmp/out.wav
```

脚本内置 40 次请求上限、单次超时、响应大小上限，默认不重试，不打印密钥，响应体只在失败时截断 400 字节输出。

## 5. 数据与隐私处理

- 测试音频全部由本机 Windows SAPI 中文语音合成，文本开头标明测试内容，没有上传任何既有个人录音。
- 仓库不包含 `connect_info`、密钥、请求头原文或完整响应体。
- `raw/results.jsonl` 只保留状态、耗时、字节数和 ASR 文本；TTS 只保留格式与字节数，不保存 base64 正文。
- 云端保留政策见 `01` 第 7 节，能确认的只有文档原文，具体保留时长仍未知。
