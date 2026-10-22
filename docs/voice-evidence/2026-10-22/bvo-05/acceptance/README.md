# BVO-03 15 秒门槛与 BVO-05 真实按键验收

日期：2026-10-22。本轮只做验收和必要取证，没有修改源码，没有重新构建或烧录。BVO-05 和阶段 A 未关闭，未完成项在文末单列。

## 1. 基线与固件

| 项目 | 值 |
|---|---|
| 竞赛仓 HEAD | `6b43cdbe2f62a2f5a785b5390fe7c81d415327c9`，分支 `velaguard/maintenance-stability` |
| NuttX HEAD | `189e6d8a18d8866e628af8c3f7b541c0e659b8bc`，分支 `velaguard/ptt-user1-stm32h750b-dk` |
| apps HEAD | `0ea954b79740756318ce1f0b13271dd94235fe2a`，分支 `velaguard/netinit-esp8266` |
| 实际烧录 HEX | `.debug/nuttx.hex`，SHA256 `ce984bf49b3dee8967a0fb3f9068946d273b60b30f9e209ce5353b37792f8eb2` |
| 实际 ELF | `.debug/nuttx.elf`，SHA256 `ed9d7401d3de0d6e4817af6c580eaf8bd9362799caab8c955519b3ab9e6bb3b1` |
| 语音源码 | `app/velaguard/vg_voice.c`，SHA256 `cfc535d581b6dbf3bf90ad2f00b7773cfbe63d3414c945308d94642a6c0a63c5` |
| 播放音量 | `VOICE_PLAY_VOLUME 900`，与 CLI `volume 90` 一致，本轮未改 |
| 输入/输出 | 输入 SAI1A PDM，输出 SAI2A -> WM8994 -> 绿色 CN9/HPOUT1 |

开机后板端状态：`vgvoice state=idle`，`rounds=8 played=8 cancelled=0 failures=0 timeouts=0`，`begins=0 submits=0`。串口为 COM3、115200。本轮未复位、未清零统计、未重复烧录。

原始日志按时间顺序放在 `raw/`，计划文件在 `plans/`。日志里混有后台 Agent 的周期输出，属于同期串口原文，没有改写。

## 2. BVO-03 门槛

### 2.1 已有的 1 秒和 5 秒记录

沿用 `../human/README.md` 中已听音确认的两条记录：

| 时长 | 样本数 | 结果 | 人工反馈 |
|---|---|---|---|
| 1 秒 | 16123 | `ok`，无丢块 | 能听清ã测试ã，没有杂音 |
| 5 秒 | 80013 | `ok`，无丢块 | 能听清内容 |

未戴耳机的 05/06/07 记录、未赶上说话窗口的记录都不计入。

### 2.2 本轮 15 秒真实说话

命令：`vgvoice record 15000`，由串口控制开始，期间没有要求按 USER1。操作者在约 3 秒准备时间后连续说话，听到回放开始后停止。

| 指标 | 值 |
|---|---|
| 状态 | `rounds 8->9`，`played 8->9`，`failures=0` |
| 录音时长 | `last_ms=15000` |
| PCM 样本 | `samples=239929`，`bytes=479858`（单声道 16 位） |
| PDM 交付 | `halves=234`，`tail_bytes=2505`，`drained=235`，`overwritten=0` |
| 样本守恒 | `floor((234*8192+2505)/8)=239929`，与 `samples` 一致 |
| 时长误差 | 239929/16000 = 14.9956 s，比 15 s 短 4.4 ms，小于 10 ms 时钟分辨率 |
| 字节率 | 127962 B/s，与 16 kHz、64 倍抽取的预期一致 |
| DMA | `dropped=0 overruns=0 clockerrors=0 errors=0` |
| 写盘/整轮 | `stage_ms=1240`，`round_ms=31380` |
| 队列 | `queue_highwater=1`，`copy_max_us=0`，`filter_max_us=10000` |
| 清理 | `temp_left=0`，`/data/velaguard/voice` 为空，可继续下一轮 |

人工反馈：整段人声清楚，速度正常，没有明显缺段或提前停止，没有听到固定咔哒或明显断续。

格式核对：源码 `vg_voice.c:360-395` 写 WAV 头为 16000 Hz、2 声道、16 位、block align 4；单声道 PCM 样本在 `frames[2*i]` 和 `frames[2*i+1]` 明确复制到左右声道。因此录音源是 16 kHz、16 位单声道 PCM，回放 WAV 是双声道复制。

结论：结合 1 秒、5 秒和本轮 15 秒记录，BVO-03 的录音前置门槛满足。15 秒测试由命令触发，符合本轮任务书要求。

## 3. BVO-05 正常按键轮次

全部使用 USER1 真实按放，没有用 NSH `vgvoice start/stop` 冒充按键。每组用开始和结束计数差值核对。

| 轮次 | 操作 | 计数差值 | 人工反馈 |
|---|---|---|---|
| 1 | 按住说ã测试一ã，松开听回放 | `begins 0->1`，`rounds 9->10`，`played 9->10` | 清楚 |
| 2-6 | 依次说ã测试二ã至ã测试六ã | `begins 1->6`，`rounds 10->15`，`played 10->15` | 全部清楚 |
| 7-11 | 依次说ã测试七ã至ã测试十一ã | `begins 6->11`，`rounds 15->20`，`played 15->19` | 操作者称 5 轮都完整清楚；计数少 1 次回放 |
| 12-13 | 说ã测试十二/十三ã | `begins 11->13`，`rounds 20->22`，`played 19->20` | 操作者称正常；计数少 1 次回放 |
| 14-18 | 操作者称为 11-20 的其中 5 轮 | `begins 13->18`，`rounds 22->27`，`played 20->24` | 操作者称 11-20 都正常；计数少 1 次回放 |

板端在进入边界工况前共记录 18 次按下、18 次录音、15 次完整回放。操作者主张已经完成 20 轮（1-20）并且都能正常回放，但计数只支持 18 次按下，差 2 次；另有 3 次回放没有计入 `played`。

差异原因：下一次按下在上一段回放尚未被播放线程标记空闲时到达，`voice_start` 先停止旧播放，因此旧回放不增加 `played`。操作者称听到的音频是完整的，可能是播放线程收尾与下一次按下之间的竞态。该现象没有导致重复提交、长录音或录放重叠，但不能把 18 次按下写成 20 次，也不能把 15 次 `played` 写成 20 次。正常 20 轮按未完成记录。

## 4. 取消、上限、打断和快速按放

### 4.1 显式取消

当前 USER1 状态机没有取消手势，因此取消轮次采用真实 USER1 按下开始录音，再由串口在录音期间发 `vgvoice cancel`，随后松手。没有使用 NSH start/stop。取消必须发生在录音期间。

| 组次 | 操作 | 计数差值 | 结果 |
|---|---|---|---|
| 边界尝试 | 第 1 次按键取消 | `begins 18->19`，`rounds 27->28`，`cancelled` 仍 0，`played` 仍 24 | 取消落在回放边界，不计入 |
| 诊断 | 命令 `record 5000` 后取消 | `cancelled 0->1`，`rounds 28->29` | 仅用于确认取消计数通路，不计入真实按键 |
| 连续 3 轮 | 取消一/二/三，每轮按住 5-6 秒 | `begins 19->22`，`rounds 29->32`，`cancelled 1->4`，`played` 保持 24 | 3 轮均丢弃，无回放 |
| 后 3 轮 | 取消四/五/六，每轮按住 5-6 秒 | `begins 22->25`，`rounds 32->35`，`cancelled 4->7`，`played` 保持 24 | 3 轮均丢弃，无回放 |

6 轮真实按键取消全部发生在录音期间，`played` 没有增加，临时目录为空，操作者确认耳机里完全没有回放声音。

### 4.2 持续按住超过 15 秒

| 轮次 | 计数差值 | 结果 | 人工反馈 |
|---|---|---|---|
| 1 | `begins 25->26`，`submits 25->26`，`vgptt timeouts 0->1`，`rounds 35->36`，`played 24->25`，`last_ms=15000` | 只提交一次、只回放一次；按住期间状态进入 `held`，没有第二次提交 | 操作者确认只听到一次回放 |
| 恢复 1 | `begins 26->27`，`rounds 36->37`，`played 25->26` | 释放后短按ã恢复一ã受理并回放 | 正常 |
| 2 | `begins 27->28`，`submits 27->28`，`vgptt timeouts 1->2`，`rounds 37->38`，`played 26->27` | 只提交一次、只回放一次 | 操作者未逐轮报告，仅表示结果可代表正常 |
| 3 | `begins 28->29`，`submits 28->29`，`vgptt timeouts 2->3`，`rounds 38->39`，`played 27->28` | 只提交一次、只回放一次 | 同上 |
| 4 | 未执行 | 操作者确认跳过 | 记为 3/4 未完成 |

说明：`vgvoice timeouts` 只在命令入口 `record 15000` 自己到达上限时增加；按键入口的上限由 `vgptt` 状态机提交，所以按键长按增加的是 `vgptt timeouts`，不是 `vgvoice timeouts`。这不是漏计。

### 4.3 回放中再次按键

| 轮次 | 计数差值 | 结果 |
|---|---|---|
| 1 | `begins 29->31`，`submits 29->31`，`rounds 39->41`，`played 28->29` | 第一段回放被停止且不计 `played`，第二段ã打断一ã正常录音和回放；操作者确认停止有效、第二段清楚 |
| 2 | 未执行 | 操作者确认跳过，记为 1/2 未完成 |

### 4.4 快速按放

连续两次按下后立即松开，每次约 60 ms，不说话。

| 指标 | 值 |
|---|---|
| 按下/提交 | `begins 31->33`，`submits 31->33`，`last_ms=60` |
| 录音轮次 | `rounds 41->43` |
| 过短拒绝 | `too_short 0->2`，最后样本 22 个、44 字节 |
| 回放 | `played` 保持 29，没有回放 |
| 遗留 | 临时目录为空，状态回到 `idle` |

结果：两次快速按放都没有遗留长录音，也没有重复提交或回放。

## 5. 受控写文件失败与同次开机恢复

沿用此前的同名目录注入方法，只操作语音临时路径 `/data/velaguard/voice/clip.wav`，不触碰用户文件。

| 步骤 | 结果 |
|---|---|
| 注入前 | `/data/velaguard/voice` 为空 |
| `mkdir /data/velaguard/voice/clip.wav` | 创建同名目录，`ls` 显示 `clip.wav/` |
| `vgvoice record 1000` | `failures 0->1`，`last_result=err_file`，`rounds 43->44`，`played` 保持 29，`temp_left=1` |
| `rmdir /data/velaguard/voice/clip.wav` | 删除注入目录，`ls` 为空 |
| 同次开机再次 `vgvoice record 1000` | `last_result=ok`，`rounds 44->45`，`played 29->30`，`failures` 保持 1，`temp_left=0` |

注入期间的 `temp_left=1` 指被同名目录占住的路径，不是遗留的 WAV 文件；删除目录后归零。恢复成功发生在同一次开机，没有复位。

## 6. 并行与资源

| 项目 | 空闲/稳定阶段 | 录音/回放阶段 | 结束后 |
|---|---|---|---|
| 显示与触摸 | 操作者触摸切页并返回，确认正常 | 未操作 | 正常 |
| Modbus 采集 | 7 个从站、14 个点全部 `offline`；`bus boot_total 1979->2032`，窗口 `timeout=64` | 任务继续运行 | 统计继续推进 |
| 存储 | 写、读、删 `/data/velaguard/bvo05-acceptance.tmp` 成功 | 语音临时文件正常写入和删除 | 语音目录为空 |
| 网络 | `eth0 RUNNING`，`192.168.137.210` | 未依赖网络 | 仍 RUNNING |
| 堆 | 开机 `used=880184`，`nused=1055`；取消后 `880176/1055` | 回放期间无异常 | 最终 `836632/1008` |
| 任务 | `vgvoice` PID 9、`vgptt` PID 10、`vghmi`、`ai_agent` | 回放时出现 `playthread` PID 796、`wm8994` PID 797 | 两者退出，任务清单不再出现 |
| 队列/滤波 | `queue_highwater=1` | `copy_max_us=0`，`filter_max_us=10000` | 无持续增长 |
| DMA | `dropped=0 overruns=0 clockerrors=0 errors=0` | 同上 | 同上 |

堆从约 880 kB 降到约 837 kB，时间上与后台 Agent 的轮次和失败回复重合，不能归因于语音；本轮没有看到随录音轮次持续增长。`copy_max_us=0` 只表示没有跨 10 ms tick，不是微秒级零耗时。

没有在线从站，`vgstats` 和 `vgruntime` 只能证明采集任务与超时统计在推进，不能证明正常读数通过。

## 7. 已知问题

- 固定短促瞬态仍在：本轮 15 秒录音 `peak=7184`、`rms=48`，此前 3 秒未说话片段仍有 `rms=60`。本轮听音没有报告咔哒，但瞬态来源没有定位，也没有修改。
- `sai_underruns` 继续增长：本轮最终 `sai_underruns=889`，`dma_done=1024`，`txbytes=8252820`。原始 DMA 状态仍出现 `sr=0x30009`（早前为 `0x40009`），包含 FEIF。该计数读取 OVRUDR 粘滞位，不能直接当作独立欠载事件次数，也不能据此证明每次都有可听缺损。本轮没有清除、屏蔽或改写统计。
- 3 次正常轮次的回放没有计入 `played`，与下一次按下早于播放线程空闲的竞态一致。操作者称听到完整音频，但计数不把它算作完整回放。
- 操作者主张 20 轮正常，板端只记录 18 次按下。差异没有定位，不能按 20/20 通过。
- `vghmi perf` 在本构建为 `disabled`，触摸只有操作者目视确认，没有帧率或输入延迟统计。
- 没有在线 Modbus 从站，不能宣称正常读数、规则联动或告警恢复已通过。

## 8. 判定

| 项目 | 判定 | 依据 |
|---|---|---|
| BVO-03 录音前置 | 满足 | 1/5/15 秒真实说话可辨；15 秒格式、样本守恒、时长误差、零丢块和清理均符合 |
| BVO-05 正常 20 轮 | 未完成 | 板端 18 次按下、15 次完整回放；操作者主张 20 轮，计数不支持 |
| BVO-05 取消 6 轮 | 完成 | 6 轮均在录音期间取消，`cancelled 1->7`，无回放，操作者确认无声 |
| BVO-05 上限 4 轮 | 未完成 | 3/4，第 4 轮由操作者跳过 |
| BVO-05 回放打断 2 轮 | 未完成 | 1/2，第 2 轮由操作者跳过 |
| BVO-05 快速按放 2 轮 | 完成 | `too_short 0->2`，无长录音、无重复回放 |
| BVO-05 错误恢复 | 完成 | 注入 `err_file` 后同次开机恢复 `ok`，临时路径归零 |
| BVO-05 整体 | 未通过 | 正常 20 轮、上限 4 轮、打断 2 轮未满足 |
| 阶段 A | 未通过 | BVO-05 未关闭；触摸只有目视确认；无在线从站 |
| BVO-04 质量问题 | 未解决 | 固定瞬态和 SAI 欠载/FEIF 仍保留，本轮不以调音或根因为目标 |

## 9. 证据索引

| 文件 | 内容 |
|---|---|
| `raw/00-baseline.log` | 开机空闲基线、堆、任务、帧统计、网络、临时目录 |
| `raw/01-bvo03-15s.log` | 15 秒真实说话与回放 |
| `raw/02-round1.log` | 第 1 轮真实按键 |
| `raw/03-rounds2-6.log` | 第 2-6 轮 |
| `raw/04-rounds7-11.log` | 第 7-11 轮 |
| `raw/05-rounds12-16.log` | 第 12-13 轮及计数差异 |
| `raw/06-fresh-baseline.log` | 重定基线 |
| `raw/07-after-user-claim.log` | 操作者主张 11-20 后的计数 |
| `raw/08-counter-check2.log` | 再次核对，计数未再前进 |
| `raw/09-cancel1.log` | 第 1 次取消落在回放边界 |
| `raw/10-cancel1-check.log` | 边界取消后的状态 |
| `raw/11-cancel-diagnostic.log` | 命令级取消诊断 |
| `raw/12-cancel3.log` | 连续 3 轮真实按键取消 |
| `raw/13-cancel3b.log` | 后 3 轮真实按键取消 |
| `raw/14-after-cancels.log` | 取消后计数、堆、临时目录 |
| `raw/15-hold1.log` | 上限第 1 轮 |
| `raw/16-recovery1.log` | 释放后短按恢复 |
| `raw/17-hold2-3.log` | 上限第 2-3 轮 |
| `raw/18-interrupt1.log` | 回放中再次按键 |
| `raw/19-quickpress.log` | 两次快速按放 |
| `raw/20-error-recovery.log` | 受控写文件失败与同次开机恢复 |
| `raw/21-parallel-snapshot.log` | 并行资源、存储、网络、帧统计 |
| `raw/22-player-thread.log` | 回放线程出现和退出 |
| `raw/23-final-snapshot.log` | 最终计数、堆、任务、SAI 原始快照 |
| `plans/` | 各窗口的串口命令计划 |
| `auto-cancel.ps1` | 检测录音后发取消的串口脚本 |
| `serial-monitor.ps1` | 周期读取计数的串口脚本 |

## 10. 复现

1. 确认实际固件 SHA256 为 `ce984bf...`，不要重新烧录。
2. 串口 COM3、115200，先读 `vgvoice status` 和 `vgptt status` 作为基线。
3. 15 秒门槛：`vgvoice record 15000`，在窗口内连续说话，听完回放，核对 `samples`、`halves`、`tail_bytes`、`overwritten` 和 `temp_left`。
4. 正常轮次：只按 USER1，不用 start/stop；每轮记录开始和结束计数差值。
5. 取消：真实按下开始录音，在录音期间发 `vgvoice cancel`，确认 `cancelled` 增加且 `played` 不变。
6. 上限：按住 USER1 超过 15 秒，确认 `vgptt timeouts` 增加一次、`played` 只增加一次，继续按住不重复提交。
7. 错误恢复：用同名目录占住 `/data/velaguard/voice/clip.wav`，确认 `err_file`，删除目录后同次开机确认 `ok`。

本轮没有提交个人音频，没有提交 `connect_info` 或凭据，没有推送。
