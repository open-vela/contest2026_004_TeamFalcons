# BVO-13 播报偏好持久化与隐私清理

日期：2026-10-22。状态：实现、主机测试与板端复位保持已验证；真实断电与静音时 TTS 计数未测。

## 1. 交付

| 文件 | 内容 |
|---|---|
| `app/velaguard/vg_voice_pref.h/.c` | 解析、加载、原子保存、开机清理 |
| `app/velaguard/velaguard.c` | 开机先清理遗留音频，再启动轮次，最后应用已保存的开关 |
| `app/velaguard/vg_voice_round.c` | 静音由状态机跳过 TTS 请求；本轮结束统一清理 |
| `app/velaguard/host_tests/test_voice_pref.c` | 解析、保存/加载、缺失与损坏、转发、清理 |
| `app/velaguard/vgvoice.c` | `vgvoice mute [on|off]` 诊断，走与浮层相同的持久化入口 |

## 2. 行为

- 开关保存在 `/data/velaguard/voice/pref.txt`，内容只有 `muted=0` 或 `muted=1`；写入走临时文件、fsync、rename。
- 开机读取；文件缺失或损坏按未静音处理，不阻止启动。
- 静音时状态机直接结束语音阶段并保留文字，不发起 TTS 请求。
- 本轮结束（成功、失败、取消）删除录音、单声道副本、24 kHz 中间文件和输出 WAV；开机再清理一次，覆盖异常掉电留下的文件。
- 不保存录音、识别文本或完整回答；偏好文件之外不写任何对话内容。

## 3. 测试

`make test_voice_pref`，使用临时目录编译 `VG_VOICE_DIR`，覆盖真实文件读写：

- 合法与非法输入、NULL 参数；
- 保存后重新加载、临时文件不残留；
- 文件缺失返回 `-ENOENT` 且保持未静音；损坏内容返回 `-EINVAL`；
- 设置开关转发给轮次并落盘；
- 四个音频文件在开机清理后都不存在。

结果见 `../bvo-11/host-tests.log` 中的 `test_voice_pref: ok`。

## 4. 板端验证

[board-pref-reset.log](board-pref-reset.log)，2026-10-22 18:16-18:17，固件 SHA256 `58b7fee4...f2e47b9e`：

| 步骤 | 命令 | 结果 |
|---|---|---|
| 复位后初始 | `vgvoice mute` | `muted=0` |
| 打开静音 | `vgvoice mute on` | `muted=1` |
| 硬件复位后 | `vgvoice mute` | `muted=1`，说明从 eMMC 恢复 |
| 关闭静音 | `vgvoice mute off` | `muted=0` |
| 再次复位后 | `vgvoice mute` | `muted=0` |
| 复位后目录 | `ls -l /data/velaguard/voice` | 只有 `pref.txt`，没有录音或 TTS 中间文件 |

复位是 ST-LINK 硬件复位，不是拔电。它能证明偏好文件在 eMMC 上跨启动保留、开机清理不留音频，但不能代替真实断电时序。

## 5. 未验证与限制

- 未做真实断电；rename 与 fsync 在掉电瞬间的行为仍按未验证处理。
- 未统计静音时 TTS 调用次数；实现上静音路径不调用 `vg_voice_cloud_tts`，需要真机日志确认。
- 清理只覆盖本轮四个固定文件，不扫描目录；这是有意的上限，避免误删其他模块文件。
