# P1 TTS 流式播放证据目录（2026-10-24）

任务：把 TTS 从收齐全部 PCM→写 WAV→nxplayer 文件播放，改为增量解码→有界环→
直接 ioctl 驱动 /dev/audio/pcm0p 的队列播放器；P2 一期为 ASR/TTS 加语音专用
keep-alive 连接槽与录音期预连接。实施计划见会话 plan（goal 模式）。

## 对比基线（只读引用，不复制）

- 板端 P0 两轮（全标记、相位连贯）：
  `../p0-timing/logs/p0-cleanbuild-2rounds.log`
  - 轮 1（99840 字节答案）：tts_begin t=10970 → tts_end t=24020 → play_posted
    t=24030 → play_done t=27890；first_playable（投递近似）24030，整轮 27900。
- 主机参考（keepalive reused=true、TTS ttfb ~470ms）：
  `../p0-timing/host/mimo-probe-1430.json`
- 固件来源：packages/ai_agent `velaguard/voice-streaming`（自 86ff02c 切出），
  本仓 `velaguard/maintenance-stability`（含 978748e 健康探测栈修复）。
- 测量固件 `bash scripts/build.sh --no-llm-health`（与 P0 基线可比）。

## 板端验收结论（logs/p1-pilot2.log 为主证据）

1. **收完前出声（P1 核心判据）**：每个成功轮 play_start 均早于 tts_end——
   point01 早 4.66s（20340 vs 25000）、point02 早 6.22s、t100a 早 6.66s、
   t400a 早 8.5s+。first_playable 现为播放线程在 START 时刻记录的真实
   驱动消费点，不再是文件投递近似。
2. **首声提前**：point01 首声 20340 vs P0 基线 24030（同尺寸答案），提前
   3.7s；整轮 25470 vs 27900，提前 2.4s。更强的例子：tts body 13.6s 的
   慢轮（pilot2 CASE_prebuf100）首声 19280，且在 30s 预算耗尽前已播出
   大部分答案（partial=1）——旧路径该轮将完全没有音频。
3. **欠载恢复机制板端裁决（streamtest）**：本地 WAV 以 1/4 实时速率喂入，
   38 次欠载 8.57s 累计停顿全部经 再入队+再 START 自行恢复（restarts=37），
   无一次升级到 STOP→重灌路径，complete=1。简单恢复即足够。
4. **取消/截断/超时**：cancelmid 取消干净（cancel_settled 990ms、无代际
   泄漏）；timeout 与 cancelled 轮 partial=1 语义正确（已出声的失败不报
   成功）。VG_VC_TRUNCATED 分类由主机测试覆盖。
5. **真实轮块间饥饿**：每答案 4-6 次欠载、累计停顿 3.4-6.2s——与 P0 块
   节奏预测一致；停顿由服务端块间隔（0.5-2.3s）决定，预缓冲档位无法
   消除，只影响起播。

## 预缓冲档位（方向性数据，样本 1-3/档）

| 档位 | play_start 实测 (ms) | 欠载/停顿 |
|---|---|---|
| 100 | 15870 / 17900 / 19280 | 6 / 5030-6170ms |
| 200 | 20340 | 4 / 3410ms |
| 400 | 15000 / 19630 | 4 / 5090ms |

档位间差异被轮间服务波动（ASR/LLM/TTS 各自 ±3-5s）完全淹没；默认 200ms
维持。结论：进一步压首声需靠 P2（去握手）而非调预缓冲。

## 环境故障记录（均非本任务代码问题）

- 试点 1（p1-pilot.log）：pcm_decode WAV 头契约未适配（已修，f0c0bd7）+
  ICS 中途丢失（ASR DNS 29.6s 超时）；用户 17:05 手动恢复 ICS。
- 试点 2 前段：ICS 恢复后约 1 分钟内 net 失败 ×3（ttss×2、anomaly01）。
- tiers 会话：eMMC wedge 复发（asr rc=io req=0 ×4，SWD 复位恢复，已知
  未闭环问题）+ 服务慢（多轮 30s 预算超时）。
- harness 两次在长轮后的 round end 步骤误判提示符超时（日志涌出），
  NSH 实际存活；后续计划应给 round end 加 optional_prompt。

## 目录

- plans/：板端驱动计划 JSON（boot-verify/pilot/tiers/netcheck）
- handoff-tools/run_p1_plan.py：驱动器（P0 运行器同构）
- logs/：pilot（首轮，含 WAV 头 bug 证据）、pilot2（修复后主证据）、
  tiers/tiers-rest/tiers-rest2（档位对照，含环境故障）

## 状态

P1 板端验收完成；P2 一期代码完成+主机验证闭环，板端机制实证但完整验收被
服务端 400 与板端内存不稳定阻断（详见 `../p2-reuse/README.md`）。
