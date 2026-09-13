# HMI UX 性能包进度追踪

更新日期：2026-09-13（继续推进：accept 修复 + 优化后 L0）。

本文件记录三个子任务的**核实后**状态。勾选清单与 `task.json` 的 notes 以本文件为准。

## 总览

| 顺序 | 任务 | Trellis 状态 | 核实结论 |
|---|---|---|---|
| 1 | `09-13-hmi-performance-baseline` | `done` | 工具、无头矩阵、L0 真机基线已交付；实体触摸与 L1+ 移交子任务 3 |
| 2 | `09-13-hmi-runtime-render` | `done` | 生命周期/后台文件/快照/增量刷新已落地；**优化后 L0 板端对照已落盘** |
| 3 | `09-13-hmi-touch-acceptance` | `in_progress` | T1–T4 自动部分完成并写出 `final-results.md`；**T5 实体触摸阻塞关闭** |

父任务保持 `planning`（集成验收入口）。自动侧已尽量收口；关闭依赖人手触摸与 L3。

## 本会话验收证据（2026-09-13 晚续）

| 命令 | 结果 |
|---|---|
| `ctest` headless 16/32（扩展 interaction_check） | 5/5 pass |
| 默认固件 `vghmi perf` | `state=disabled` |
| 触摸子任务结果文档 | `09-13-hmi-touch-acceptance/research/final-results.md`（明确未完成） |

## 关键对照摘要

静止首页（D1=14，无触摸）：提交速率从约 2/s 降到约 0.1/s；objects 仍 71/2/6。冷启动 render max 仍约 140 ms。L1+ 未测。

## 下一步（需人工）

- T5：实体触摸录像、L1/L2 P95/FPS、30 分钟资源、L3 Agent
- 用户授权后再 commit 本轮无头扩展与结果文档
