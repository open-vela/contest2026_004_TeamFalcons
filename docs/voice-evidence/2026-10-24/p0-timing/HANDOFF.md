# P0 计时任务状态（2026-10-24 下午）

固件来源：packages/ai_agent `velaguard/voice-timing`（86ff02c，基于 c3e500c），
contest 仓 `velaguard/maintenance-stability`（34b15f3）。构建命令
`bash scripts/build.sh --no-llm-health`（关 30s 健康探测，保持与 2026-10-23
基线可比；探测互斥不覆盖语音轮 ASR/TTS 阶段，测量期必须关闭）。

## 已完成

- 代码与主机测试：传输相位拆分（send=头+体、ttfb 首字节、body 自首字节、
  http_total 全退出路径）、TTS 块级观测、LLM 首个 SSE data 行、play_done/
  clip_prep 指标。`make -C app/velaguard/host_tests test` 全绿。
- 板端仪表验证（p0-cleanbuild-2rounds.log）：多轮 run_id 一致，first_data/
  tts_blocks/clip_prep/play_done 全部就位，相位数值连贯。
- 主机参考（host/mimo-probe-1430.json）：**keepalive reused=true**——服务端
  允许同一 TLS 连接顺序复用（LLM 形状 + TTS 形状各一次，均 200），P2 连接
  复用的设计前提成立。主机 TLS 握手约 75ms，板端实测 1000-2500ms。
- 空 buffer tts_blocks 样例（cleanbuild 第 1 轮）：first_pcm=720ms，6 块，
  末块 7360ms，块间隔 600-1750ms——服务端确实分块下发，P1 流式播放有料可依。

## 板端基础设施问题（均已知未闭环，今日复发）

1. **eMMC wedge / /data 打开失败**：pilot 第二问 ASR `rc=io req=0`（点句
   WAV 打不开），`[skills] Cannot open skills directory` 持续出现。SWD 复位
   后恢复。与 bvo14 emmc-wedge、/data 瞬时失败同源，根因未闭。
2. **控制台挂死第三次复发，本次无健康探测**：pilot 运行器中途死（日志截断
   在 advice 轮 TLS 握手输出中间），NSH 输入失效而 syslog 仍在流。cleanbuild
   两轮短会话未复现。推翻 MIMO 会话对探测并发的归因——无探测也挂。
3. **烧录后软复位不生效**：flash.ps1 完成后板子可能仍跑旧镜像（本日两次
   实测）；必须跟一次 SWD 硬复位并核对固件标记再采信结果。
4. **平铺构建不跟踪 ai_agent 头文件依赖**：改 agent_timing.h 等头文件后，
   引用它的 .o（如 agent_loop.o）不会重编，新旂数据结构布局混链导致
   agent 线程栈越界写（trace run_id 被 attempt=1 覆写为 0x01）。本次
   排查近两小时。改 ai_agent 头文件后必须
   `find ../packages/ai_agent -name '*.o' -delete` 再构建。

## 未完成（P0 剩余）

- 全量矩阵：4 组（冷/热 × 空闲/告警）各 ≥30 轮。被上述 1/2 阻塞——长会话
  中途 wedge/挂死会污染数据并中断运行器。建议先决策：等 eMMC wedge 根因，
  或按会话间 SWD 复位的降级方案跑（运行器已支持续跑）。降级方案必须执行
  数据可疑标记：任一会话内出现 wedge 症状（/data 打开失败、ASR rc=io
  req=0、skills 枚举失败、控制台无响应）时，该会话自症状出现起的全部轮次
  在 results.csv 标 `suspect=1`，只进剔除明细不进中位数（MIMO 会话建议，
  已采纳）；SWD 复位后从新会话重新计数。
- p0-report.md：待矩阵数据；主机参考与板端两轮数据已可预填归因表。

## 密钥纪律

connect_info 仅由 mimo_phase_probe.py 运行时解析，密钥只进 Authorization
头；探针输出与全部日志不含密钥。
