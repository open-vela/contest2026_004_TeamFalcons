# 语音播报断续及中途停止修复

日期：2026-10-25。承接按键识别修复 `aed8599`，仅处理用户反馈的播报故障。

## 原因

此前发布配置启用流式边收边播，200 ms 预缓冲无法覆盖实际服务端的分块间隔。
`before-stream.log` 中一轮虽然最终成功，却发生 5 次欠载，累计等待 4020 ms，
即用户听到的断续。该记录来自修复前串口采集，保留原始内容。

本次排查直接读取串口时还观察到：LLM 回答在整轮 25950 ms 才完成，TTS 在
28490 ms 开始出声，随后 `tts rc=timeout status=200 pcm=23040`，
整轮在 30130 ms 结束且 `failed=1`。原来的 30 秒总截止时间会中止已经开始的播报。
这段为排查终端观察摘要，不将其冒充为 `before-stream.log` 中的原始记录。

## 修改

- 发布预设关闭 `CONFIG_VG_VOICE_TTS_STREAM`，使用已有的完整 WAV 路径。
  SSE 音频收齐、格式检查和转换成功后才播放，网络分块停顿不再传递到扬声器。
  流式播放器源码保留供后续独立实验；构建脚本同步检查发布配置，避免沿用旧配置。
- ASR 和查询仍共享 30 秒。收到有效文字回答后，TTS 另有最多 30 秒；重试仍为
  独立 15 秒。保留原始提交时刻作为整轮统计锚点，不缩短日志里的整轮耗时。
- 完整音频播放按文件时长加 5 秒等待，不受先前请求截止时间影响。
  合成失败仍保留文字并允许重试，取消、音频清理和只读查询权限沿用原有实现。

取舍：改为收齐后播放，起播等待会增加。原来整轮 30 秒内起播的目标不能再作为硬上限；
查询和首次合成的请求等待合计上限约 60 秒，播放等待另按音频长度计算。
本次修复不宣称优化了首声延迟或解决所有服务端/网络故障。

## 本次验证

- `make -C app/velaguard/host_tests test`：通过，见 `host-tests.log`。
  状态机新增慢查询在第 29 秒给出回答、TTS 独立预算耗尽、完整音频播放跨过请求截止时间的测试。
- `bash scripts/build.sh`：通过，见 `build.log`；烧入镜像哈希见 `firmware.sha256`。
- `scripts/flash.ps1`：成功，见 `flash.log`。
- 两轮真实 MiMo ASR、LLM、TTS 和板端播放：见 `accept.log` 和 `accept-summary.json`。
  使用文件输入明确问句，以便复现播报链路；该测试不能代替按键录音验收。

| 轮次 | 完整合成结束 | 投递播放 | 播放完成 | 结果 |
| --- | --- | --- | --- | --- |
| point01 | 21900 ms | 21900 ms | 26580 ms | `played=1 failures=0 timeouts=0` |
| point02 | 19480 ms | 19490 ms | 23160 ms | 累计 `played=2 failures=0 timeouts=0` |

两轮均为 `tts rc=ok status=200`、`last result=ok`、`temp_left=0`，
`tts_end <= play_posted < play_done`，结束后 `state=idle retained_pcm=0`。
表中投递播放日志来自完整 WAV 入队，不等同于外部声学测量。

另执行播放期间取消，见 `cancel.log`：TTS 在整轮 30600 ms 完成，
确认超过旧 30 秒截止时间后仍可成功；随后主动结束，未产生完整播放成功记录，
音频设备空闲、临时文件清理完成。`cancel_settled` 为 4360 ms，
不将其描述为满足先前流式播放器的 2 秒取消目标。

实际按键复测见 `manual.log`、`manual-tail.log`：从 RAM 上传 91396 字节录音，
识别为查一下A相电压，进入只读查询。TTS 完整收到 84480 字节原始 PCM，
在整轮 25640 ms 投递播放、31710 ms 播放完成，`failed=0`，累计
`played=3 failures=0 timeouts=0 temp_left=0`。该轮覆盖实际按键和超过旧截止时间的完整播放；
日志可以证明设备报告完成，声音是否连续仍以操作者旁听为准。

操作者随后反馈该轮只听到A相电压离线，之后没有更多内容。复核
`vg_va_render_point()`：离线点位的语音固定为点位名称加离线，
屏幕的采样时间、阈值等详情不在语音摘要内。因此该轮句末结束符合现有设计，
无需为了朗读屏幕全部文字而扩展本次修复。此反馈不作为所有语句声学连续性的证明。

## 复测命令

在 Windows PowerShell 中，于仓库根目录执行：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/voice_test/nsh_plan.ps1 -Plan docs/voice-evidence/2026-10-25/tts-continuity/accept.json -LogFile buffered-accept.log
powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/voice_test/nsh_plan.ps1 -Plan docs/voice-evidence/2026-10-25/tts-continuity/cancel.json -LogFile buffered-cancel.log
```

必须检查完整合成成功后才投递播放、每轮 `play_done`、播放成功次数增加、失败数为零以及临时文件无残留。
最终听感仍需真实扬声器旁听，不能只凭 `AUDIOIOC_START` 或 HTTP 200 宣称声音连续。
