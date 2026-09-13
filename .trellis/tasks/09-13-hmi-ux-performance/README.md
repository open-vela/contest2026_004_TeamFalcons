# HMI 优化任务交接入口

## 交付边界

本目录及三个子任务是供其他 Agent 实施的完整规划包。创建本包的会话只负责文档，不授权自动启动任务、改代码、接管串口、烧录或提交。

父任务状态为 `planning`（集成验收入口）。子任务进度见 [progress-tracker.md](research/progress-tracker.md)：基线与 runtime 已复核为 `done`；下一实施入口是触摸验收。先读根目录 `AGENTS.md`，不能用历史任务或本包覆盖安全要求。

## 接手顺序

| 顺序 | 任务 | 交付 | 复核状态 |
|---|---|---|---|
| 1 | [HMI 性能基线与测量工具](../09-13-hmi-performance-baseline/prd.md) | 有界统计、可重复测试入口、真机基线 | `done`（L1+ 交互未测） |
| 2 | [页面生命周期与增量刷新优化](../09-13-hmi-runtime-render/prd.md) | 退出清理、非阻塞后台接口、快照一致性、增量更新 | `done`（主机/无头已复跑） |
| 3 | [触摸交互与整体验收](../09-13-hmi-touch-acceptance/prd.md) | 输入调度、状态恢复、防误触、对照实测与安全回归 | 下一实施目标 |

三个子任务可能修改同一组文件，不得并行实施。后一个 Agent 先读取前一个任务的实际 diff 和验证记录，不能只读取本包最初的源码行号。

## 每个 Agent 的阅读顺序

1. 根规则、当前 Git 状态、当前任务和相关 Trellis 规范。
2. 所接子任务的 `prd.md`、`design.md`、`implement.md`。
3. 该任务的 `implement.jsonl` 中的规范和研究材料。
4. 本父任务的 [设计](design.md)、[现状证据](research/current-state.md)、[统一验收契约](research/acceptance-contract.md)。
5. 前置任务完成时写入的研究结果和验证记录；重新定位当前源码，核对是否漂移。

## 启动约束

- 先得到用户对具体子任务的实施指令；本轮创建文档的授权不能代替它。
- 只激活实际实施的子任务。父任务管理集成验收，不是批量改代码的入口。
- 使用 Trellis 的 `task.py validate`、上下文加载和启动流程；未满足 `Blocked by` 时不要执行后续子任务。
- 本包不派发 Agent 或创建 Codex 应用中的任务。以下交接文本仅供后续用户复用。

当前任务的交接文本：

```text
Active task: .trellis/tasks/09-13-hmi-touch-acceptance
请先阅读本任务 prd.md、design.md、implement.md、implement.jsonl，
以及父任务 README.md、progress-tracker.md 和统一验收契约。
前两项子任务代码已落地并复核；读取其 research/*-results.md 与当前 diff，不要沿用创建包时的行号。
按 T1→T5 实施；真机触摸与 L3 证据不足时如实未测，不得捏造达标。
```

## 统一查阅

- [产品需求与最终验收](prd.md)
- [跨任务设计与责任分界](design.md)
- [父任务集成检查单](implement.md)
- [源码与配置证据](research/current-state.md)
- [计时、样本与性能门槛](research/acceptance-contract.md)
- [文档包校验记录](research/task-package-validation.md)
- [进度追踪（复核）](research/progress-tracker.md)

子任务归档后，按 Trellis 返回的真实路径更新必要的交接引用，不手工猜测归档目录，也不批量改写其他历史任务。
