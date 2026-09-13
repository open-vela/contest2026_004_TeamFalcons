# 父任务集成执行计划

## 本文件用途

这是将来接手者的集成检查单，当前没有执行任何产品优化。文档创建完成后仍保留 `planning`，不调用 `task.py start`，不生成代码提交或实际验收成绩。

## 阶段 0：实施授权与基点

- [x] 用户明确指定要实施的子任务，并审阅该任务的最新规划。（2026-09-13 继续推进）
- [x] 记录本仓、NuttX、固件 LVGL checkout 的提交及脏文件；保留其他任务改动。
- [x] 读取父任务 README、根规则、所接子任务三份规划和上下文清单。
- [x] 检查当前任务和前置证据，只激活实际承担工作的子任务。
- [x] 将源码漂移记录到所接任务研究文件，必要时更新设计并重新确认重大范围变化。（见 progress-tracker.md）

## 阶段 1：性能基线

- [x] `09-13-hmi-performance-baseline` 的观察工具、无头入口及测量口径通过验证。
- [x] 优化前矩阵结果包含版本、配置、数据量、网络状态、Agent 状态及失败项。（L0 完整；L1+ 交互未测已标明）
- [x] 给后续任务交接完整原始证据路径和测量命令，不只交平均 FPS。
- [x] 不把仅完成测量工具、缺少真机数据的任务标记为基线验收完成。（交互样本仍移交子任务 3）

## 阶段 2：运行与刷新

- [x] 确认阶段 1 结果可复现后实施 `09-13-hmi-runtime-render`。
- [x] 检查页面根和所有页面退出清理，尤其 report/discover 的定时器。
- [x] 检查 UI/后台之间没有 LVGL 指针跨线程，结果按请求和页面代次校验。
- [x] 验证相同采集轮次不重复推进失败窗口或历史；新历史样本即使值相同仍可见。
- [x] 检查稳定 ID、结构更新和统一刷新契约已交接给阶段 3。
- [ ] 保存阶段 2 与基线的对照，明确仍待交互阶段解决的项目。（优化后板端 L0 未再测，归子任务 3）

## 阶段 3：交互与最终证据

- [ ] 在阶段 2 通过后实施 `09-13-hmi-touch-acceptance`。
- [ ] 验证输入周期、按下反馈、手势取消、动态列表和返回状态。
- [ ] 完成统一验收契约要求的全部场景及样本数。
- [ ] 对 Agent 负载失败核对既有系统挂死任务，不通过停用产品能力制造通过结果。
- [ ] 检查所有新增界面状态使用真实数据，未解锁延期功能或新增权限。

## 最终质量门

- [ ] 三个子任务的需求和验收映射完整，各自验证记录均可追溯至实际提交/构建。
- [ ] 所有 HMI-AC1 至 HMI-AC9 有通过证据或明确的未完成原因。
- [ ] 相同场景的最终结果不劣于基线，未靠减少负载或降低业务频率达标。
- [ ] 确认产物属于单一 `velaguard-lvgl` 主线；测试配置变更有记录且产品配置恢复可复现。
- [ ] 需要更新的规范由各实施任务按 Trellis 处理；不把未验证推测写入规范。
- [ ] 提交和归档由后续用户批准后执行，禁止因本包存在就自动提交。

## 本轮任务包校验

本轮只允许以下文档检查，不执行上面的产品实施步骤：

```bash
python3 .trellis/scripts/task.py validate .trellis/tasks/09-13-hmi-ux-performance
python3 .trellis/scripts/task.py validate .trellis/tasks/09-13-hmi-performance-baseline
python3 .trellis/scripts/task.py validate .trellis/tasks/09-13-hmi-runtime-render
python3 .trellis/scripts/task.py validate .trellis/tasks/09-13-hmi-touch-acceptance
python3 .trellis/scripts/task.py current --source
git diff --check
git status --short
```

还须结构化检查四份 `task.json`、八份 JSONL 的格式、真实引用和双向父子关系。`task.py validate` 只覆盖上下文清单，不能代替规划完整性检查。

文档检查结果写入 `research/task-package-validation.md`；其中不得勾选或声称产品验收成功。
