# BVO-03/BVO-05 人声无声定位与修复

日期：2026-10-22。本轮只做原因定位、最小修复和听音复测，未启动 20 轮验收，未改动 ASR/TTS、云接口、产品 UI 或点表。

## 结论

播放路径此前把 NxPlayer 的音量参数当成百分比，直接传 15。wm8994 驱动按 `63 * volume / 1000` 换算成耳机 PGA 寄存器，15 落到寄存器 0，即 -57 dB，实际等同于静音。已知参考音用 NxPlayer CLI 的 `volume 15` 播放时落到寄存器 9，即 -48 dB，同样低于可听范围。两次测试都在极低增益下播放，完全无声符合预期。

修复只改一处：`app/velaguard/vg_voice.c` 的播放音量改为 `VOICE_PLAY_VOLUME 900`，对应寄存器 56，约 -1 dB，与听音确认可辨的 CLI `volume 90` 增益一致。采集链路、PDM 配置、滤波、DMA 和队列均未改动。

## 音量换算链

| 入口 | 传给驱动的值 | 寄存器 | 增益 |
|---|---|---|---|
| 原 `nxplayer_setvolume(15)` | 15 | 0 | -57 dB |
| CLI `volume 15` | 150 | 9 | -48 dB |
| CLI `volume 90` | 900 | 56 | -1 dB |
| 修复后 `VOICE_PLAY_VOLUME` | 900 | 56 | -1 dB |

换算位置：`apps/system/nxplayer/nxplayer.c` 的 `nxplayer_setvolume` 把参数原样写入 `AUDIO_FU_VOLUME`；`nuttx/drivers/audio/wm8994.c` 在 AUDIOIOC_CONFIGURE 中执行 `63 * volume / 1000`；寄存器 0 为 -57 dB，63 为 +6 dB，步进 1 dB。

## 路由与静音核对

播放结束后读取的寄存器值，逐项对照驱动定义：

- PM1 `0x0001=0x0303`：BIAS、VMID 2x40k、HPOUT1L/R 输入级使能。
- PM3 `0x0003=0x0030`：MIXOUTL/MIXOUTR 使能。
- PM5 `0x0005=0x0303`：DAC1L/R 与 AIF1DAC1L/R 输入使能。
- R45/R46 `0x002D/0x002E=0x0001`：DAC1 到 MIXOUT 未静音；HPOUT1 输入选择 MIXOUT，属于驱动默认路径。
- R96 `0x0060=0x00EE`：HPOUT1L/R 的输出级、中间级、去短路均使能。
- R28/R29 `0x001C/0x001D`：播放中 `0x0078`，低 6 位为 56 且未静音；停止后 `0x0038`，保留 56 并静音。
- DAC1 数字音量 `0x0610/0x0611=0x00C0`，即 0 dB。

因此静音、路由、输出级和数字音量都不是无声原因，只有耳机 PGA 增益异常。

## 证据顺序

| 记录 | 内容 | 听音结论 |
|---|---|---|
| 01-one-second-window.log | 上一轮 1 秒录放 | 完全无声，失败 |
| 02-output-reference.log | CLI `volume 15` 播放参考 WAV 三次 | 完全无声；寄存器 0x1C=0x0009 |
| 03-ref-volume90.log | CLI `volume 90` 播放参考 WAV 两次 | 清楚可辨；寄存器 0x1C=0x0038 |
| 04-postboot.log | 新固件启动后状态 | 空闲，寄存器 0x1C=0x000A |
| 05/06/07 | 修复后 5 秒、5 秒、8 秒 | 操作者未戴耳机，听音无效 |
| 08-record5000-listened.log | 5 秒说话 | 能听清内容 |
| 09-record1000-listened.log | 1 秒说话 | 有声音但听不清，zcr=8 |
| 10-record3000-listened.log | 3 秒未说话 | 完全无声，与录到静音一致 |
| 11-record2000-listened.log | 2 秒说测试一二三 | 能听清，内容一致 |
| 12-record1000b-listened.log | 1 秒说测试 | 能听清，没有杂音 |

05 至 07 三次回放期间操作者未戴耳机，其听音结果不作为证据；对应日志只保留采集计数。

09 的 1 秒记录 `zcr=8`、`rms=95`，12 的同长度记录 `zcr=74`、`rms=125`。两次差异说明首次是说话窗口和内容太少，链路本身在 1 秒长度上可以复现可辨人声。

## 格式与幅度

- 回放 WAV 为 16000 Hz、双声道、16 位，单声道样本复制到左右；参考 WAV 与录音 WAV 使用同一格式，NxPlayer 均接受。
- 参考 WAV：peak 7809、rms 570。
- 2 秒录音：peak 7186、rms 109、zcr 33；1 秒录音：peak 7185、rms 125、zcr 74。
- 未做增益归一化或动态处理，录音样本原样写入。

## 实际构建与烧录

- 构建：竞赛仓根目录 `bash scripts/build.sh`，日志 build-volume-fix.log。
- 烧录：`scripts/flash.ps1`，日志 flash-volume-fix.log，`Download verified successfully`。
- `.debug/nuttx.hex` SHA256：`ce984bf49b3dee8967a0fb3f9068946d273b60b30f9e209ce5353b37792f8eb2`
- `.debug/nuttx.elf` SHA256：`ed9d7401d3de0d6e4817af6c580eaf8bd9362799caab8c955519b3ab9e6bb3b1`
- `app/velaguard/vg_voice.c` SHA256：`cfc535d581b6dbf3bf90ad2f00b7773cfbe63d3414c945308d94642a6c0a63c5`
- 主机测试：`make -C app/velaguard/host_tests test` 全部通过，含 test_pdm_filter 与 test_ptt_cycles。

## 剩余问题

- 每段录音的 peak 都固定在 7176 至 7187 之间，3 秒未说话的片段仍有 rms=60。这个固定瞬态的位置和来源本轮没有定位，也没有修改；三次有效听音没有报告咔哒声。
- `sai_underruns` 继续增长，1 秒回放后累计 207；原始 DMA 状态仍显示 `31`，FEIF 属于既有观察。该计数在每次 DMA 完成中断读取 SR 的 OVRUDR 粘滞位，标志未清除时会持续累加，不能直接当作独立欠载事件次数或可听断续次数。本轮没有修改。
- 3 秒未说话片段回放为静音，说明链路不会凭空产生可听噪声；这不等于播放底噪已经测量。

## 复现步骤

1. `bash scripts/build.sh`，`scripts/flash.ps1` 烧录。
2. 串口 115200 下发 `vgvoice record 2000`，在窗口内说一句短话。
3. 约 0.3 秒后自动回放，绿色 CN9 耳机应能听清内容。
4. `vgaudio codec` 核对 `0x001C/0x001D`：播放中为 `0x0078`，停止后为 `0x0038`。
