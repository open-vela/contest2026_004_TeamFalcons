# HMI UX 性能包进度追踪

更新日期：2026-09-13（继续推进：accept 修复 + 优化后 L0）。

本文件记录三个子任务的**核实后**状态。勾选清单与 `task.json` 的 notes 以本文件为准。

## 总览

| 顺序 | 任务 | Trellis 状态 | 核实结论 |
|---|---|---|---|
| 1 | `09-13-hmi-performance-baseline` | `done` | 工具、无头矩阵、L0 真机基线已交付；实体触摸与 L1+ 移交子任务 3 |
| 2 | `09-13-hmi-runtime-render` | `done` | 生命周期/后台文件/快照/增量刷新已落地；**优化后 L0 板端对照已落盘** |
| 3 | `09-13-hmi-touch-acceptance` | `in_progress` | T1–T4 自动部分完成；T5 实体触摸最终验收未完 |

父任务保持 `planning`（集成验收入口），阶段 2 对照已写入 `research/final-results.md`。

## 本会话验收证据（2026-09-13 晚）

| 命令 | 结果 |
|---|---|
| `make -C app/velaguard/host_tests test` | 此前会话 pass |
| `ctest` headless 16 位 / 32 位 | 5/5 pass（含 `interaction_check`） |
| `stage1_lvgl_hmi_accept.ps1`（修正 fleet n 硬编码后） | **pass=16 fail=0**；无从站时 live Modbus SKIP |
| `--hmi-perf` L0 对照 | 约 80 s commits **158 → 9**；见 `final-results.md` |
| 默认固件复位烧录 | hex `3cd82ff4…`，`# CONFIG_VG_HMI_PERF is not set` |

## 关键对照摘要

静止首页（D1=14，无触摸）：提交速率从约 2/s 降到约 0.1/s；objects 仍 71/2/6。冷启动 render max 仍约 140 ms。L1+ 未测。

## 下一步（阻塞人工）

- T5：实体触摸录像、L1/L2 P95/FPS、30 分钟资源、L3 Agent
- 用户授权后再 commit 未提交的 T3/T4、accept 脚本与任务文档
