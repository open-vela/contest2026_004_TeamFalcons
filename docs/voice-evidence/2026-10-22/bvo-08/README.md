# BVO-08 语音会话与调度状态机

日期：2026-10-22。状态：状态机与主机测试完成，已接入板端语音轮次；按键驱动的板端流程待人工按放。

## 1. 交付

| 文件 | 内容 |
|---|---|
| `app/velaguard/vg_voice_session.h` | 状态、动作位、会话结构与公开接口 |
| `app/velaguard/vg_voice_session.c` | 纯决策实现，无设备、无线程、无自带时钟 |
| `app/velaguard/vg_voice_round.c` | 板端接线：一个工作线程持有会话与整条链路 |
| `app/velaguard/vg_voice_ptt.c` | USER1 按下/提交事件转成会话入口 |
| `app/velaguard/host_tests/test_voice_session.c` | 主机状态测试，13 个用例 |
| `app/velaguard/Makefile` | 在 `CONFIG_VG_VOICE_LOOPBACK` 下编译 |

## 2. 接口形状

每个入口接收调用方读到的单调毫秒值，返回动作位掩码。状态机不做 I/O，调用方按位执行：停止播放、停止录音、取消 ASR、取消 Agent、停止 TTS、开始录音、开始 ASR、提交 Agent、开始 TTS、显示结果、显示错误、收起浮层、释放音频。

状态：IDLE、RECORDING、STOPPING、ASR、WAIT_AGENT、QUERYING、TTS、PLAYING、RESULT、ERROR。STOPPING 表示播报尚未真正停止，录音必须等设备回收完成。

## 3. 板端接线

`vg_voice_round.c` 的 32 KB 工作线程读按键事件，把真实结果送回状态机，而不是另写一套判断：

| 板端事件 | 状态机入口 |
|---|---|
| 按下、松开 | `vg_vs_press`、`vg_vs_release` |
| 15 秒上限、结果 15 秒、空闲两分钟 | `vg_vs_tick` |
| ASR 返回 | `vg_vs_asr_result` |
| Agent 提交与返回 | `vg_vs_agent_started`、`vg_vs_agent_result` |
| TTS 与播放结束 | `vg_vs_tts_result`、`vg_vs_play_done` |
| 设备真正停止 | `vg_vs_audio_stopped` |
| 主动结束、仅重试播报 | `vg_vs_end`、`vg_vs_retry_tts` |
| 滚动暂停、静音 | `vg_vs_result_hold`、`vg_vs_set_muted` |
| 点位上下文 | `vg_vs_context_point`、`vg_vs_set_context_point` |

浮层只读 `vg_voice_round_ui()` 发布的快照，不持有会话指针。

## 4. 已实现的合同

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

## 5. 主机测试

```
cd app/velaguard/host_tests
make test_voice_session
./test_voice_session
```

结果：`test_voice_session: ok`。用例：

1. 正常链路
2. 替换与迟到结果
3. 播报打断
4. 后台隔离
5. 重启清空
6. 静音跳过 TTS
7. 空识别
8. TTS 失败只重试播报
9. 30 秒预算（ASR 与 TTS 两种到期）
10. 15 秒录音上限与过短按放
11. 空闲释放与页面切换
12. 结果保留与滚动暂停
13. 主动结束与配置版本变化

本轮全量 `make test` 通过：36 个 C 目标与 2 个 Python 检查，退出码 0。此前 `test_voice_pref` 只在运行列表里、不在 `test:` 依赖中，导致整套测试在这一步失败，已补入依赖。

## 6. 板端证据

[board-round-thread.log](board-round-thread.log)，固件 SHA256 `0d517611...1504dbd0`，2026-10-22 18:07 上电后采集：

- `vgptt: running=yes opened=yes state=idle`、`begins=0 submits=0`：按键层正常运行，本轮无人按键。
- `vgvoice: state=idle ... worker=9`：语音工作线程存活。
- `ps`：PID 10 为 32,744 字节栈的 pthread，即 `vg_voice_round` 工作线程；PID 9 `vgvoice` 与 PID 11 `vgptt` 各 4,056 字节。
- 本轮没有按下按键，因此没有 RECORDING 及之后的状态迁移。

## 7. 未完成

- 按键驱动的板端流程未做：替换旧问题、播报中打断、取消后资源回收、结果 15 秒收起，都需要人工按放。
- 浮层是否随状态正确绘制属于 BVO-12 的验收，本轮只确认线程与状态机在运行。
- 后台日报与语音并发时的互不取消在主机用例中覆盖，板端需要一次定时日报与一次语音轮次重叠才能取证。
