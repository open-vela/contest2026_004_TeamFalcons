# BVO-14 联调、故障验证与阶段 B 验收准备

日期：2026-10-22。状态：板端已实际烧录并完成可自动执行的联调；阶段 B 仍未通过，剩余项都依赖人工操作或外部条件。

## 1. 板端环境与实际烧录

| 项目 | 值 |
|---|---|
| 调试器 | ST-LINK V3（SN 0043001D3137511133333639，FW V3J17M11） |
| 开发板 | STM32H750B-DK，电压 3.27 V，Device ID 0x450 |
| 串口 | Windows COM3，ST-Link Virtual COM Port，115200 |
| 烧录工具 | STM32CubeProgrammer v2.22.0，External Loader MT25TL01G_STM32H750B-DISCO |
| 烧录范围 | 内部引导 0x08000000..0x080002d0，QSPI 镜像 0x90000000 起 |
| 镜像 | 当日多个镜像，最新 `7335d4f0...2672960e`（20:05），见 [firmware-2026-10-22.txt](firmware-2026-10-22.txt)；板端自报 `189e6d8a18d-dirty` |
| 网络 | rj45，192.168.137.83，网关可达，MQTT up |

烧录日志：`flash.log`。每次烧录后都重新抓取串口启动日志。

## 2. 实际完成的板端检查

| 检查 | 结果 | 证据 |
|---|---|---|
| 启动 | NSH 启动，eMMC 挂载，HMI 480×272，`vgptt: watching /dev/buttons`，ai_agent 守护启动 | `boot-after-flash.log` |
| 只读工具 | `vg_anomaly_read` rc=0；`vg_point_read` 无样本时 `reason=no_sample value=-` | `nsh-tools.log` |
| 异常完整列表 | 无在线从站时 `total=14 shown=12 seen=14`，列出 12 条后 `omitted=2`，未采样点 `value=-` | `../bvo-09/board-anomaly-recovery.log` |
| 告警恢复 | `vg_alarm_recovery ups_load` 返回 `state=unknown history_complete=false`，没有把当前离线当成历史结论 | `../bvo-09/board-anomaly-recovery.log` |
| 参数校验 | 旧镜像上 `vg_alarm_recovery` 缺少 point_id 时返回 `reason=bad_arg`；现支持裸词按 point_id 包装 | `nsh-tools.log`、`../bvo-09/board-anomaly-recovery.log` |
| 语音线程 | `vgptt: running=yes state=idle begins=0`，`vgvoice: state=idle worker=9`；`ps` 中 32,744 字节栈的轮次线程存在 | `../bvo-08/board-round-thread.log` |
| 录音与清理 | 2 秒录音：samples=32120 bytes=64240，dropped=0 overruns=0，temp_left=0，目录为空 | `nsh-voice.log` |
| 云 ASR | 160,044 B 合成样本 → 请求 213,561 B、响应 533 B、HTTP 200，转写与样本一致，8.2 s / 12.7 s | `nsh-asr.log`、`nsh-asr-repeat.log` |
| 云 TTS | `pcm16` 流式，84,480 B PCM → 112,684 B 的 16 kHz 双声道 WAV，2.47 s | `nsh-tts.log` |
| 播放 | NxPlayer 接受该 WAV，`play rc=0`，任务回到 idle | `nsh-play.log` |
| 本地故障 | 不存在的文件返回 `rc=io`，不产生请求 | `nsh-fault.log` |
| 坏输入 | 文本文件冒充 WAV 返回 `cannot prepare upload wav: -22`、`rc=io req=0`，不产生网络请求 | `nsh-fault-corrupt-wav.log` |
| 录音后取消 | `vgvoice start` 进入 recording，取消后回到 idle，目录无新增文件 | `nsh-cancel.log` |
| 在线从站 | COM7 从站 1,253 请求 / 1,184 应答 / `crc_scan=0`；温度 36.0 触发 crit，恢复 31 s，离线 44 s 后成项 | `../bvo-09/board-live-slave.log` |
| 串口可用性 | COM3 打开成功、可读 10 字节并回显 `nsh>`；ST-LINK VCP 与 CH340 COM7 均 Present | `board-serial-availability.log` |
| 语音整轮 | 合成按放与合成提问跑通录音→WAV→ASR 200→Agent 调用工具→文字回答；TTS 在预算尾部偶发超时，文字保留 | `../bvo-11/board-round-synthetic.log`、`../bvo-07/board-round-budget.log` |
| 模型问答 | 异常、点位、恢复三问均调用只读工具，数字与从站及工具输出一致 | `../bvo-10/board-llm-answers.log` |
| 堆 | 见第 3 节 | `nsh-*.log` 中的 `free` |

板端命令是诊断入口，不是产品交互：`vgvoice asr/tts/play` 复用与语音轮次相同的 `vg_voice_cloud_*` 和 `vg_voice_play_file`，因此它们证明的是同一条云与播放链路。

## 3. 内存实测

`free` 的 `used` 是已用量，不是可用堆。启动后（HMI、ai_agent、语音轮次、语音任务都已创建）：

| 时点 | used | free | maxused | maxfree |
|---|---|---|---|---|
| 开机空闲 | 834,216 | 6,004,200 | 834,792 | 5,795,432 |
| 2 秒录音后 | 834,624 | 6,003,792 | 882,912 | 5,795,432 |
| 云 ASR 后 | 834,304 | 6,004,112 | 919,168 | 5,795,432 |
| TTS + 播放后 | 834,632 | 6,003,784 | 1,062,336 | 5,795,432 |
| 连续 ASR 后 | 918,104 | 5,920,312 | 1,076,904 | 5,795,432 |
| 新镜像开机空闲（18:07） | 834,224 | 6,004,192 | 834,784 | 5,795,432 |
| 新镜像运行 3.5 分钟后（18:10） | 918,048 | 5,920,368 | 997,736 | 5,795,432 |

- 语音任务在 `vg_voice_init` 中一次分配约 484 KB 的 PCM 缓冲，已包含在开机 used 内。
- 单轮语音的峰值增量约 230 KB（TTS 解码、SSE 行、重采样缓冲），最大已用约 1.08 MB。
- `maxfree` 在全部采样中保持 5.79 MB，说明没有出现大块连续内存被耗尽的情况。
- 连续 ASR 后 used 比开机高约 84 KB，同期 ai_agent 后台日报仍在运行，无法据此判定泄漏；需要在关闭后台任务的条件下重复测量。
- 新镜像上电 3.5 分钟的增量 84 KB 与上一组一致，`maxused` 未超过 1.0 MB，`maxfree` 仍为 5.79 MB。两次开机值与上一次开机值相差不足 20 字节，说明语音缓冲在启动时一次分配、不随轮次增长。

## 4. 板端发现并修复的缺陷

本轮共在真机发现并修复 15 项，按发现顺序：

1. **产品工具白名单缺两项**：`vg_anomaly_read`、`vg_alarm_recovery` 被 `vg_product_tool_allowed()` 拒绝，语音的异常与恢复意图在真机会直接失败。已加入 ai_agent 的白名单。
2. **跨启动快照被当成当前样本**：eMMC 上的 live 快照带有上一次启动的单调 tick，简单相减得到约 49 天的 `age_ms`，回答会念出 `4294967 秒`。已改为 tick 超前时视为无样本，并加主机回归。
3. **NSH 诊断任务栈溢出**：`vgvoice` 命令任务只有 4 KB 栈，跑 ASR 时触发 `stack_dump` 并卡死。已把该命令的栈设为 32 KB，与轮次工作线程一致。
4. **SSE 行缓冲过小**：真实 TTS 返回的单行超过 24 KB，原实现直接报错。已改为 32 KB 起、按需翻倍、上限 512 KB。
5. **base64 分片超过解码缓冲**：真实分片 40,960 字符，原实现按固定 16 KB 输出缓冲一次解码而失败。已改为按 4 字符组循环解码。
6. **TTS 输出 FILE 泄漏**：短读路径跳过了 `vg_voice_pcm_writer_close`。已改为无条件关闭。
7. **聊天模型名与账号不符**：板端 provision 文件保存的是 `mimo-v2.5`，账号返回 `400 Unsupported model`。账号模型列表与 `connect_info` 都是 `mimo-v2.6-flash`。仓库内 `scripts/gen-agent-secrets.sh` 已改为 `mimo-v2.6-flash`；板端已保存的 provision 文件未改动，见待补清单第 J 项。
8. **异常列表把未采样显示为 0.0**：离线且从未读到的点渲染成 `value=0.0`，模型会当成真实读数。已改为 `value=-`。
9. **单标志带前导逗号**：离线行输出 `flags=,offline`。已改为只在非首项前加分隔符。
10. **首轮采集前 `total=0` 读成健康**：新增 `seen=` 与 `note=first_cycle_pending`，把未采集与无异常分开。
11. **浮层首次显示即崩溃**：`poll_cb` 对按钮调用 `lv_label_set_text`，写坏按钮对象后在样式查询里断言，`vghmi` panic。已改为更新按钮内的标签，见 `../bvo-12/board-overlay-crash.log`。
12. **云端路径不写录音文件**：结束请求的静默分支只发布空闲，不写 WAV，ASR 得到 `cannot prepare upload wav: -2`。已改为两条结束路径都先写文件，见 `../bvo-11/board-round-synthetic.log`。
13. **语音等待不驱动 Agent 调度**：工作线程只轮询状态，提交依赖 HMI 定时器；HMI 忙于报表时问题一直停在 QUEUED，30 秒后报“查询超时”。已在等待循环内驱动 `vg_agent_round_tick()`。
14. **后台轮次占用通道时语音直接失败**：`-EBUSY` 被当成提交失败，用户问题立即报错。已改为保持排队并在队列超时内重试。
15. **重试播报沿用已用尽的截止时间**：第一次超时后重试立即再次超时，按钮等于无效。重试现在获得 15 秒的播报专用预算，主机用例覆盖。
16. **相同问题的缓存让工具被跳过**：语音第二次问同一句时命中 LLM 缓存，没有执行工具，也就没有可核对的数据。语音轮次在入队前按问题文本清除该条缓存；由工具样本生成的回答也不再写入缓存。
17. **查询中再次按键不释放 Agent 槽位**：新问题被替换后仍报“上一个问题仍在处理”。现在处理 `VG_VS_ACT_CANCEL_AGENT`，用 abandon 释放槽位；实测第二问正常完成。
18. **超时轮次占用槽位最长 300 秒**：超时只是丢弃结果，客户端仍 active。超时路径改为 abandon，迟到回复按 serial 丢弃。
19. **模型不调用工具时没有快照**：模型偶尔直接凭历史作答，轮次因无数据而报错。意图已在 C 层确定，现按意图直接调用对应只读工具补取快照再渲染。
20. **首轮采集前回答“无异常”**：采集未完成与现场健康被混为一谈。事实快照增加 `seen`，为 0 时回答“采集尚未完成，暂时无法确认异常”。
21. **连续合成按键间隔过短时偶发“录音未正常结束”**（已收敛）：根因方向是上一轮的 WAV 写盘/停止尚未完成时又开始录音，录到的是一段空采集。现在开始录音前先等语音任务空闲（上限 2 s），忙则明确提示“录音设备忙，请稍后再试”；结束等待放宽到 5 s；并修复了 abandon 与提交并发时把轮次重新置为 RUNNING 的竞态。修复后 5 轮 1.5 秒间隔与两次连续替换压测均为 0 次该错误。真实按键节奏仍待人工确认。
22. **连续替换时握手硬故障**（已定位并修复）：语音工作线程 32 KB 栈不足，mbedTLS 握手与 ASR 请求构造叠加时溢出，破坏相邻内存，表现为 `strh` 落到奇地址的 UNALIGNED 硬故障，并使整机卡死。32 KB 下 12 轮“1 秒后再按”压测触发 3 次；同一压测在 48 KB 栈下 24 轮 0 次。代价是堆上多 16 KB。栈高水位未插桩，溢出深度未直接测量。见 `../bvo-07/board-handshake-crash.log`。

## 5. 尚未验证

- **真人语音**仍未进入链路：用合成 WAV 已把 录音→WAV→ASR→Agent→文字→TTS→播放 跑通整轮（13.4 至 13.8 s），并用合成按键验证了替换问题与超时让位；缺的是人对着麦克风说话以及语音质量。
- **一次 ASR 卡住**：14:40 的一次调用在 TLS 握手成功后长时间没有返回，命令任务一直占用 NSH，超过 30 秒 deadline。复位后连续两次调用均成功（8.3 s、12.7 s），未能复现。已记录为未解决缺陷，未用重试掩盖。
- 在线从站已接入并跑过阈值告警与恢复；同一测点阈值与离线同时出现、以及事件环挤出导致的 `RECOVERED_PARTIAL` 仍只有主机用例。
- 听音与触摸点击未做；浮层几何、按钮命中与回答区已在板端测量，见 `../bvo-12/board-overlay-geometry.log`。真实断电、以及长回答的滚动量仍未取证。

## 6. 各任务状态

| 任务 | 状态 | 依据 |
|---|---|---|
| BVO-07 | 已实现；截止与回收已测；整轮 30 s 预算按实测未稳定达标 | `bvo-07/README.md` |
| BVO-08 | 已实现，已接入板端轮次线程；真机按键未测 | `bvo-08/README.md` |
| BVO-09 | 已实现；在线从站下取到 crit=36、recovered 31 s、离线 44 s 判定与完整事件链 | `bvo-09/README.md` |
| BVO-10 | 已实现；模型调用两个新工具与点查捷径已在板端验证；30 条真人问法未做 | `bvo-10/` |
| BVO-11 | 已实现；录音到 ASR 的整轮已在板端跑通（合成按键），TTS 与播放分段验证 | `bvo-11/README.md` |
| BVO-12 | 已实现；首次显示崩溃已修复，显示路径板端通过；布局与触摸待人工 | `bvo-12/README.md` |
| BVO-13 | 已实现；开关跨两次硬件复位保持，开机清理无残留音频；真实断电未测 | `bvo-13/README.md` |
| BVO-05 | 未关闭，维持原状态 | `bvo-05/followup/README.md` |

阶段 B 仍未通过：上表没有一行满足人工验收条件。板端当前固件 `102d2d68...0137a35`，模型 `mimo-v2.6-flash`，主机侧 RTU 从站已停止，语音目录只剩 `pref.txt` 与两只测试 WAV。

## 7. 待补清单（需要人或外部条件）

### A. 按键与完整语音轮次（BVO-08、11、12、14）

- 缺少：真人按住 USER1 说话。
- 原因：无法自动产生按键与语音。
- 操作：按住 USER1 说“现在有哪些异常”，松开；再试 1 秒、5 秒、15 秒和超过 15 秒。
- 现状：用 `vgvoice round press/release` 的合成事件已把 录音→WAV→ASR 跑到 HTTP 200，浮层随状态显示并在未命中意图时给出错误；`vgptt` 仍 `begins=0`。缺的是真人语音带来的可识别文本，以及之后的 Agent 与播报。
- 预期：浮层出现并显示录音中，松开后识别、查询、播报，文字与播报来自同一快照。
- 判定：30 次按键无重复提交、无录放重叠、无残留文件；每次回答的数字与同轮快照一致。

### B. 30 条固定问法（BVO-10、11）

- 缺少：普通话发音与真实点位。
- 操作：三类各 10 条，包含点位 ID、完整名称、UPS 名称、多温度点同名、不存在的 ID。
- 判定：事实正确率 100%；识别错误单列；不静默选错点。

### C. 听音与屏幕（BVO-11、12、13）

- 缺少：人耳与人眼。
- 操作：听正常回答、长异常列表、静音后再问、TTS 失败后点“重试播报”；看浮层在 480×272 上的换行与按钮。
- 判定：语音清晰、无截断数字；静音后 TTS 调用计数为 0；重试不触发新查询；浮层可读可点。

### D. 取消与回收（BVO-07）

- 缺少：真人按键取消。
- 操作：识别中、查询中、播报中分别取消，记录按键到清理完成的时间，并采样 `free`、`ps`。
- 判定：2 秒内回收；线程与 socket 回到基线；堆不单调下降。

### E. 断电与偏好（BVO-13）

- 缺少：真实断电。
- 操作：关闭播报，重启；录音中断电，重新上电后检查 `/data/velaguard/voice/`。
- 判定：开关保持；无 `clip*.wav`、`tts-*.pcm`、`tts-*.wav`。
- 已完成：硬件复位两次，开关从 `muted=0` 到 `1` 再回到 `0` 均保持，目录只剩 `pref.txt`，见 `../bvo-13/board-pref-reset.log`。剩余的是拔电时序。

### F. 在线从站与恢复事件（BVO-09、10、14）

- 缺少：在线 Modbus 从站。
- 操作：制造超阈值告警并恢复，再问当前异常与告警恢复。
- 判定：总数与列表和事件环一致；0 项、1 项、超过 3 项、超过旧报告 8 项、多类别重叠各测一次。
- 已完成：主机 RTU 从站在 COM7 提供 14 点，板端取到 all-online（total=0）、温度 36.0（total=1，rank=crit）、恢复 `duration_ms=31000`、单从站离线约 44 秒后 `flags=offline,stale`（total=2），以及 4 条 RAISE/CLEAR 事件。见 `../bvo-09/board-live-slave.log`。
- 剩余：同一测点阈值与离线同时出现、事件环挤出后的部分恢复。

### G. 服务故障注入（BVO-07、11）

- 缺少：可控断网与配额。
- 操作：断网、错域名、无效密钥、429、响应截断各 3 次。
- 判定：30 秒内明确失败；恢复网络后下一轮成功；无残留连接。
- 已完成：本地文件缺失、坏 WAV、300 ms 预算、57 次调用中的 3 次 `rc=net`，都在 11 秒内结束且下一次成功；握手阶段的栈溢出故障也已复现并修复。
- 阻塞：错域名/连接拒绝需要在主机 hosts 文件写入映射，当前账户无管理员权限（`hosts not writable: 访问被拒绝`）；无效密钥与 429 需要重写板端 provision 或可控的服务端证书，前者会把密钥写到串口，均不在本轮授权范围。以上保持未执行。

### H. 云端保留政策（BVO-06、13）

- 缺少：服务方书面答复。
- 判定：得到答复或继续标注“未知”；不承诺云端删除。

### I. ASR 卡住问题（BVO-07、11）

- 缺少：稳定复现条件。
- 现象：2026-10-22 14:40 一次 ASR 在握手成功后未返回，超过 30 秒 deadline，命令任务占用 NSH。
- 下一步：在板上连续跑 50 次 ASR，记录每次耗时与 `maxused`；若复现，抓 `ps` 与 `netstats`，确认阻塞在 send 还是 recv。当前实现依赖 socket 超时让读循环回到 deadline 检查，若 NuttX 在某个路径上不返回，需要改为非阻塞加 poll。
- 补充：该次调用已运行在 32 KB 命令栈的镜像上（`vgvoice` 栈已改），因此不是栈溢出，没有用提高栈的方式掩盖。
- 追加：2026-10-22 18:20 至 19:33 连续 57 次 ASR 未复现挂起：54 次 200（4.5 至 11.1 s），3 次 `rc=net` 在 6.89、7.95、7.21 s 返回且下一次成功；第 57 次前后 used 均为 910,136，`maxfree` 不变，`ps` 线程数与基线一致。见 `../bvo-11/board-asr-soak.log` 与 `board-asr-soak2.log`。本构建无 `netstats`，socket 计数未取证。挂起仍按未解决记录，57 次未复现不足以排除。

### J. 聊天模型名（BVO-10、14）——已解决

- 处理：新增 `agent_llm_set_model()` 与 `vgagent model <name>`，只替换 backend 0 的模型并保留主机与密钥，避免把密钥经串口写入 staging。板端执行 `vgagent model mimo-v2.6-flash` 后 HTTP 200，复位后启动日志仍为 mimo-v2.6-flash。
- 结论：模型名问题关闭；下方原始记录保留，说明为什么没有走重新 provision。

- 缺少：用户的 provision 决定。
- 现象：板端 provision 文件保存 `mimo-v2.5`，账号返回 `400 Unsupported model mimo-v2.5`；账号模型列表为 `mimo-v2.5-asr`、`mimo-v2.5-tts`、`mimo-v2.5-tts-voiceclone`、`mimo-v2.5-tts-voicedesign`、`mimo-v2.6-flash`、`mimo-v2.6-pro`，`connect_info` 写的是 `mimo-v2.6-flash`。
- 为什么不能自行完成：重新 provision 需要把密钥写入 staging 文件，而 staging 通过串口十六进制下发会把密钥写进日志；本轮没有这样做。板端 `set_llm` 在当前构建里不可用（ai_agent 以 daemon 方式启动，跳过 nsh_commands），没有第二条不改密钥的修改路径。
- 操作：用现有 provision 流程（主机侧）把模型改为 `mimo-v2.6-flash`，或确认应使用 `mimo-v2.6-pro`。
- 判定：`vgagent ask 现在有哪些异常` 返回 HTTP 200，模型调用 `vg_anomaly_read` 并按快照回答。
- 影响：BVO-10 的模型路径、BVO-14 的端到端联调。仓库内 `scripts/gen-agent-secrets.sh` 已改，未动板端 provision 文件。

### K. 30 秒预算与异常回答的第二轮模型调用（BVO-07、10、14）——已按可逆工程方案处理

- 处理：单条 `vg_anomaly_read` / `vg_alarm_recovery` 调用后直接结束本轮，不再等第二次模型调用；语音侧仍从同一份快照渲染。实测整轮 13.4 至 13.8 s，同一问题连续两次都重新执行工具。通用 `vgagent ask` 的单条异常查询会返回工具文本，作为已知取舍保留。
- 剩余：不需要用户决定；若日后要恢复二次模型润色，回退该提交即可。

- 缺少：产品取舍决定。
- 现象：ASR 6.7 s，异常意图的 Agent 阶段 10.7 至 16.4 s（两轮模型调用），剩余 7 至 9 s 内 TTS 未完成，整轮以“文字保留、播报失败”结束。
- 选项：一是在工具返回后直接用快照渲染异常回答，省掉第二轮模型调用（预计省 4 至 15 s，回答仍是同一份已校验数据）；二是经用户同意放宽 30 秒合同；三是维持现状，接受偶发播报失败。
- 为什么不能自行决定：这改变产品回答合同或验收预算，属于取舍，不是缺陷修复。
- 判定：选定方案后重新测三轮异常问答，记录 ASR、Agent、TTS 和首次可播放时刻。

## 8. 复现命令

```bash
# 烧录（Windows PowerShell，文件先复制到 Windows 临时目录）
powershell.exe -File flash.ps1 -OutDir C:\Users\19y\AppData\Local\Temp\vgflash

# 串口执行（COM3，115200）
vgvoice asr /data/velaguard/voice/test.wav
vgvoice tts 现在没有异常
vgvoice play /data/velaguard/voice/tts-16k.wav
vgvoice record 2000
free
```

## 续接核验更正

2026-10-22 22:44：48 KB 栈仍出现 HARDFAULT，栈水位 24680 / 49128 字节。缺陷 22 的栈溢出根因及已修复结论撤回，重新打开；阶段 B 仍未通过。当前续接证据及固件来源见 [continuation](../continuation/README.md)。
