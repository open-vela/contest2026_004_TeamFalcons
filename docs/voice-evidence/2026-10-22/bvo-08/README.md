# BVO-08 语音会话与调度状态机

日期：2026-10-22。状态：状态机与主机测试完成；尚未接入板端流程与界面，板端未验证。

## 1. 交付

| 文件 | 内容 |
|---|---|
| `app/velaguard/vg_voice_session.h` | 状态、动作位、会话结构与公开接口 |
| `app/velaguard/vg_voice_session.c` | 纯决策实现，无设备、无线程、无自带时钟 |
| `app/velaguard/host_tests/test_voice_session.c` | 主机状态测试 |
| `app/velaguard/Makefile` | 在 `CONFIG_VG_VOICE_LOOPBACK` 下编译 |
| `app/velaguard/host_tests/Makefile` | 加入 `make test` |

## 2. 接口形状

每个入口接收调用方读到的单调毫秒值，返回动作位掩码。状态机不做 I/O，调用方按位执行：停止播放、停止录音、取消 ASR、取消 Agent、停止 TTS、开始录音、开始 ASR、提交 Agent、开始 TTS、显示结果、显示错误、收起浮层、释放音频。

状态：IDLE、RECORDING、STOPPING、ASR、WAIT_AGENT、QUERYING、TTS、PLAYING、RESULT、ERROR。STOPPING 表示播报尚未真正停止，录音必须等设备回收完成。

## 3. 已实现的合同

- 每次新问题提升 generation。取消、替换、结束、进入结果、进入错误、配置版本变化都使旧结果失效；所有结果入口先比对 generation。
- 播报中按键先停止播放，收到真实停止后才开始录音。
- 等待或查询中再次按键取消旧问题并立即录音，不等待网络回收。
- 空识别进入错误，不提交 Agent。
- 静音时跳过 TTS，文字照常显示；静音不发起 TTS 请求，也不提供播报重试。
- TTS 失败保留文字，`retry_tts` 只重新播报，不重跑 Agent。
- 30 秒预算从提交起算，`vg_vs_remaining_ms()` 供各阶段设置自己的截止；TTS 阶段到期但文字已有时转为结果并标记播报失败。
- 结果保留 15 秒；滚动暂停计时，松手后重新计满 15 秒；收起不结束上下文。
- 两分钟空闲清空上下文，录音、识别、查询、播报期间不计空闲。
- 页面切换不改状态、不改截止时间、不清上下文。
- 配置版本变化清空点位上下文；在途问题直接结束，避免用旧点表回答。
- 后台轮次不经过本模块。后台结果 generation 不匹配，不能顶替用户问题；用户问题替换也不会产生后台取消动作。

## 4. 主机测试

```
cd app/velaguard/host_tests
make test_voice_session
./test_voice_session
```

结果：`test_voice_session: ok`。

覆盖：正常链路、替换与迟到结果、播报打断、后台隔离、重启清空、静音、空识别、TTS 失败重试、30 秒预算（ASR 与 TTS 两种到期）、15 秒录音上限与过短按放、页面切换、空闲释放、滚动暂停、主动结束、配置版本变化。

## 5. 未完成

- 未接入 `vg_voice_ptt.c`、`vg_voice.c` 与云语音模块。当前模块只提供决策，实际流程在 BVO-11 接线。
- 未烧录。真机按键节奏、音频停止延迟和线程调度不在主机测试范围内。
- 动作位到具体调用的映射尚未实现，不能据此宣称语音链路可用。
