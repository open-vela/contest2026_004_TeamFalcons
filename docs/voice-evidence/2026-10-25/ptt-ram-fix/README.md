# 按键语音 RAM 录音交接修复

日期：2026-10-25。范围：修复现有板端按键语音识别失败，不扩展语音功能。

## 原因与修复

`vg_vs_release()` 与 15 秒上限返回 `STOP_RECORD | START_ASR`。
`vr_run_actions()` 原先执行 `STOP_RECORD` 时，在正常结束录音后无条件取消录音并清零
`clip_generation`，导致接下来的 `vg_voice_clip_take_owned(0)` 失败。
识别阶段因此报告 `asr rc=io status=0 req=0`，屏幕显示识别失败，请重试。
参考用户指定会话 `f3569c8c-86a3-4156-a066-e85201623f12`；本次复核源码后修复。

现在仅在没有 `START_ASR` 的停止动作中取消并清理录音。正常松手和到达上限保留
PCM 与句柄，交给识别阶段；取消和录音过短仍清理。

## 本次验证

- `bash scripts/build.sh`：通过，见 `build.log`。
- `make -C app/velaguard/host_tests test`：通过，见 `host-tests.log`。
- `scripts/flash.ps1`：烧录成功，见 `flash.log`。本次烧入镜像哈希见 `firmware.sha256`。
- COM3 合成按键，真实麦克风录音和 RAM 上传：见 `regression.log`、`short.log`。

| 场景 | 本次结果 |
| --- | --- |
| 按住约 3 秒后松手 | RAM 95972 字节，HTTP 200，请求 128193 字节 |
| 连续第二轮约 3 秒 | RAM 93794 字节，HTTP 200，请求 125289 字节 |
| 持续按住到 15 秒上限，随后松手 | RAM 476116 字节，HTTP 200，仅增加一次识别请求 |
| 录音中主动结束 | 识别请求计数保持 4，无 PCM 残留 |
| 极短按下后松手 | `too_short`，识别请求计数保持 4，无 PCM 残留 |

上述场景结束后均为 `state=idle retained_pcm=0`，DMA 错误和录音失败均为 0。
无控制语音输入的自动化场景接受 `asr rc=empty status=200`；这证明录音交接与请求完成，
不证明人声内容识别正确。第一轮返回 `1.` 也不作为内容正确证据。

## 实际说话复测

自动化结束后，助手停止注入按键并监听串口，提示用户在实体按键上说话。
`manual.log` 记录到两轮 RAM 录音，分别为 81450、81830 字节，识别均为
`asr rc=ok status=200`。后一次界面查询输出：

```text
q=A相电压。
a=volt_a A相电压：离线，没有当前读数
采样 23 秒前，已超过新鲜度上限
阈值：警告 ≤ 200 V，严重 ≤ 180 V
数据来自已确认点表，配置版本 1
```

该轮进入只读点位查询和 TTS 阶段，串口记录到 `first_playable`。
采集窗口覆盖到开始播放；不将该日志描述为完整播放验收，也不据此评估整体识别准确率。

## 复测

在仓库根目录使用 Windows PowerShell；板上已配置可用的识别服务凭据和网络：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/voice_test/nsh_plan.ps1 -Plan docs/voice-evidence/2026-10-25/ptt-ram-fix/regression.json -LogFile ptt-capture.log
```

```bash
python3 scripts/voice_test/check_ptt_capture.py ptt-capture.log
```

检查本次分两次采集的结果：

```bash
python3 scripts/voice_test/check_ptt_capture.py docs/voice-evidence/2026-10-25/ptt-ram-fix/regression.log docs/voice-evidence/2026-10-25/ptt-ram-fix/short.log
```

检查器已通过本次日志，并验证会拒绝 IO 失败、空 RAM 录音和缺少场景的记录。
文件输入的 `round ask <wav>` 不能代替本回归测试。
