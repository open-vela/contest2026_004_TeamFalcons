# 阶段 2 优化后 L0 对照（板端）

日期：2026-09-13。场景：D1=14 点，静止首页，采集运行，无触摸。测量固件 `bash scripts/build.sh --hmi-perf`。

| | 基线（优化前） | 优化后 |
|---|---|---|
| 产物路径 | `.debug/hmi-baseline/perf_transcript.txt` | `.debug/hmi-post-opt/perf_transcript.txt` |
| hex md5 | bc60dbe805ba667a32bb0e3abcc44d72 | 7cf65e457c7bac035a2a5be091cefaa9 |
| 版本串 | stm32h750b-dk:velaguard-lvgl-dirty | NuttX `18bec160782` Sep 13 2026 18:17:19 dirty |
| 本仓锚点 | 当时未提交实施 | `6afd140` + 未提交 T3/T4/调度改动 |

## 同口径快照（约 80 s uptime）

| 指标 | 基线 t≈81 s | 优化后 t≈80 s |
|---|---|---|
| flush / commits | 158 | **9** |
| render n / max | 81 / 140 ms | 9 / 140 ms |
| render p95 | 20 ms | 140 ms（样本极少，冷启动主导） |
| page objects / listeners / timers | 71 / 2 / 6 | 71 / 2 / 6 |
| heap 稳定 | 是 | 是（objects 无增长） |
| cpu（/proc/cpuload） | 44.1% | 59.0%（单读数方差；见下） |
| dropped | 0 | 0 |

约 45 s 窗口：基线 commits 88 → 优化后 **5**。

## 结论

1. **静止无变化重绘显著下降**：约 80 s 内提交从 158 降到 9（约 2 commits/s → ~0.1 commits/s）。与子任务 2 增量刷新目标一致：时钟标签不再每秒强制整页提交。
2. **冷启动段未改变性质**：首次 render/nav max 仍约 130–200 ms、loop max 约 350 ms，属首帧布局，不作为稳态回归判定。
3. **资源稳定**：objects/listeners/timers 与基线同为 71/2/6，无单向增长。
4. **CPU 读数**：优化后略高，与更密的空闲 loop（loop n 3892 vs 2690）同向；不能据此宣称 CPU 恶化或改善。交互场景仍须 L1+ 实体触摸复核。
5. **仍未覆盖**：L1/L2 触摸 P95/FPS、D2/D3、30 分钟资源、L3 Agent——归 `09-13-hmi-touch-acceptance` T5。

## 复现命令

```bash
bash scripts/build.sh --hmi-perf
powershell.exe -ExecutionPolicy Bypass -File scripts/flash.ps1
powershell.exe -ExecutionPolicy Bypass -File .debug/hmi-post-opt/collect_perf.ps1
bash scripts/build.sh   # 恢复默认产品固件（清 CONFIG_VG_HMI_PERF）
powershell.exe -ExecutionPolicy Bypass -File scripts/flash.ps1
```
