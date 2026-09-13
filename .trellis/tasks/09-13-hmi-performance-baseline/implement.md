# 基线子任务实施步骤

## 进入条件

创建本包时下列步骤全部未执行。用户另行授权后，先读父任务和本任务三份规划，检查真实 Git 状态，验证上下文，再按 Trellis 激活本子任务。

## B1：核对当前链路

- [x] 重新定位父任务 E01-E09、E23-E25，记录本仓和 LVGL/NuttX 提交。
- [x] 记录实际编译配置，区分源 defconfig、安装后的 `.config` 和 PC 配置。
- [x] 确认公开 LVGL 事件和 getter 可用；检查渲染事件配对和 flush 的实际含义。
- [x] 写入本任务研究记录，保留与初始证据不同之处。（research/b1-link-check.md）

## B2：最小性能观察

- [x] 实现固定六项统计、近窗、快照及关闭时的空操作；先跑纯逻辑测试。（host_tests/test_hmi_perf 全过）
- [x] 增加只读 `vghmi perf` 分流，覆盖未启动和关闭状态。（板端实测 running/disabled；not_running 为主机路径验证）
- [x] 增加最小观察点，不修改 sleep、输入周期或页面刷新逻辑。
- [x] 校验热路径分配/I/O/锁等待为零；记录静态内存和丢样统计。（map g_perf=6416B；dropped=0）
- [x] 增加 Kconfig 开关和构建 `--hmi-perf`，确认默认构建不受残留影响。

## B3：无头测试可重复化

- [x] 增加独立 CMake/CTest 入口和 16/32 位配置。（另有板容量 profile）
- [x] 复用现有 alarm/trend harness，修正测试自身对色深、stride、容量的硬编码。（harness_common 共享实现，测试逻辑未改）
- [x] 增加 deterministic fixture 和帧输出参数，测试不得读板端真实配置。（fixture_check）
- [x] 验证中文、数值和主要操作控件可见；保存截图。（PPM 于各 build 的 frames/ 目录）
- [x] 将已知页面生命周期问题记录为后续回归目标，不在此提前修复。（E10-E12 等保留于父任务 current-state，本任务未动页面行为）

## B4：采集基线

- [x] 使用父任务统一契约的 L0/L1/L2、D1/D2/D3，保存完整基线。（L0+D1 完成并落盘；L1/L2 与 D2/D3 阻塞于实体触摸/人工确认，见 baseline-results.md）
- [x] 对 L3 做同固件尝试；若 Agent 挂死，保存日志和时间并关联已有任务。（Agent lazy 模式且无 ask 操作，未触发挂死路径；风险归口 09-12-heartbeat-llm-round-system-wedge，已在结果中说明）
- [x] 采集开启/关闭统计的同条件对照，不在测量期间高频打印。（L0 CPU 对照；交互对照属未测范围）
- [ ] 保存实体触摸录像；缺少拍摄条件时明确未测，不用软件时间替代。（未测：无人手与拍摄条件）
- [x] 报告时钟分辨率、有效样本数、统计窗口和任何中途失败。（gran_us=10000 实测如实呈现；无中途失败）

## 验证命令

以下是本子任务实现后的命令，文档创建阶段不运行：

```bash
cmake -S gui/headless -B .debug/hmi-headless -DVG_LVGL_DIR="$PWD/../apps/graphics/lvgl/lvgl" -DVG_HEADLESS_COLOR_DEPTH=16
cmake --build .debug/hmi-headless
ctest --test-dir .debug/hmi-headless --output-on-failure
cmake -S gui/headless -B .debug/hmi-headless-32 -DVG_LVGL_DIR="$PWD/gui/lvgl" -DVG_HEADLESS_COLOR_DEPTH=32
cmake --build .debug/hmi-headless-32
ctest --test-dir .debug/hmi-headless-32 --output-on-failure
make -C app/velaguard/host_tests test
bash scripts/build.sh --hmi-perf
```

烧录和串口按 `velaguard-board-inner-loop` 执行。只允许本次成功构建的镜像；COM 被占或测试需要改点表时，遵守对应人工步骤。

测量后运行默认 `bash scripts/build.sh` 并核对统计关闭配置。记录这两种构建的身份，不能把一个构建的录像贴给另一个。

## 交给下一个 Agent

- [x] 生成实际 `research/baseline-results.md`，按 B-AC1 至 B-AC7 逐项说明。
- [x] 列出新增函数/配置/命令、测试 fixture 用法和统计输出例子。
- [x] 给出原始录像/数据路径以及当前页面生命周期和阻塞问题的复现步骤。（触摸录像未测；其余见结果文档）
- [x] 写明子任务 2 可以开始的证据及剩余独立 Agent 阻塞。（工具、无头测试矩阵与 L0 基线均已就绪；L0 基线证实静止状态每秒约 2 次提交刷新，直接为子任务 2 增量刷新提供了优化目标；物理触摸录像未测不阻塞纯代码生命周期与后台解耦实施）
- [x] 完成 Trellis 质量检查；提交和归档另外遵循用户授权，不自动启动子任务 2。

## 停止条件

若基线要求改公共驱动、申请新 Agent 权限或改变采集负载才能测量，停止扩大范围并报告。只有工具通过、真机条件缺失时继续保留未完成状态，不能捏造基线。
