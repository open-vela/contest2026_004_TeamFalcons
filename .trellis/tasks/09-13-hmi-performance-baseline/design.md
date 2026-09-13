# 基线工具设计

## 观察点

复用现有 LVGL 公共回调，不修改固件 LVGL checkout 或 NuttX 驱动。

| 指标 | 观察边界 | 解释 |
|---|---|---|
| loop | 一次 `lv_timer_handler()` 进入/返回 | 不含之后的 sleep |
| input_read | 包装 `lv_indev_get_read_cb()` 返回的原始回调 | 只测取样函数，不是实体触摸延迟 |
| model_tick | `vg_model_tick()` 进入/返回 | 包含当前版本原有调用链 |
| render | `LV_EVENT_RENDER_START` / `LV_EVENT_RENDER_READY` | 布局和绘制相关软件段，按实际事件验证配对 |
| flush_submit | `LV_EVENT_FLUSH_START` / `LV_EVENT_FLUSH_FINISH` | 驱动回调阶段，不能命名为物理显示完成 |
| nav_dispatch | 导航请求进入到页面构建返回 | 第一帧和可交互时刻仍由视频/无头额外确认 |

记录最后一次有内容提交的时间、提交计数、页面 ID、当前页面树对象数、模型监听数和本项目页面定时器数。统计不创建常驻屏幕浮层，不让数值标签自身触发重绘。

对象、监听和定时器计数只在页面变化或每秒诊断快照时由 HMI 线程采集，不每帧深度遍历。NSH 线程只读取已经发布的统计缓存，不直接调用 LVGL 或遍历对象树。

不存在的公共 getter 不得靠依赖私有结构布局绕过；观察所需的项目内计数在模型/Shell 增加只读调试查询。LVGL 全局定时器是否有可用公开枚举 API 按实际头文件核实，不可取得时明确只统计本项目拥有的定时器。

## 内部接口与存储

拟新增项目内 `vg_hmi_perf.c/.h`，采用固定指标枚举、记录函数和快照函数，不为这六项指标建立通用追踪框架。

- 每项保存累计次数、累计耗时、最大耗时和最近 256 个 `uint32_t` 耗时样本。
- 六项样本共 6144 字节；统计静态存储预算不超过 8 KiB，编译 map 核对，不计算共享 UI 原有对象。
- 时间使用单调时钟。输出单位可为微秒，但必须同时输出实际可观察时间粒度；不能把单位当作分辨率。
- 热路径不能 malloc、排序、printf、文件写入或等待消费者。读取方在短锁内复制，锁外计算分位数和格式化；写入方不因诊断读取而阻塞，冲突时记录 `stats_dropped`。
- 数据竞争使用项目平台支持的同步原语解决，不把 volatile 当作互斥。关闭构建中记录宏为空操作，不保留采样数组。
- P50/P95 只对标明的最近样本窗口计算。正式验收仍依据父任务完整原始样本口径。
- 单调时钟回绕以无符号差值处理；无配对结束事件计数为不完整样本，不填入零耗时假样本。

## NSH 与构建

增加只读子命令 `vghmi perf`，在 `lv_is_initialized()` 的重复启动检查之前分流。它不执行 `lv_init()`、不启动采集、不建立新页面。

输出采用单行或少量 `key=value`，至少包含：

- `state=disabled|not_running|running`、统计时间、时钟粒度。
- 每项的累计样本数、近窗样本数、P50/P95/最大耗时。
- 提交次数、页面 ID、对象/监听/项目定时器数、`stats_dropped`。
- 无法读取的 CPU、堆或栈指标用明确的不可用状态，不打印伪零值。

本期不加 reset/start/stop 命令，也不向设备端 Agent 注册这个入口。

Kconfig 增加 `CONFIG_VG_HMI_PERF`，依赖 HMI，默认关闭。在本仓构建脚本增加显式 `--hmi-perf`：

- 仅适用于作品主线 `velaguard-lvgl`，不改变默认目标。
- 在生成/安装配置阶段启用统计，不把源 `scripts/configs/velaguard-lvgl.defconfig` 永久改为开启。
- 默认重新构建必须恢复源配置对应的关闭状态，不受上次测试配置残留影响。
- 不应用公共仓补丁，不新增第二套参赛固件主线。

未来命令：

```bash
bash scripts/build.sh --hmi-perf
bash scripts/build.sh
```

当前 `--hmi-perf` 尚不存在，须由本子任务实现并测试，不能在本轮文档创建中执行。

## 独立无头入口

在 `gui/headless/` 增加独立 `CMakeLists.txt` 和最小测试配置，避免现有根 CMake 对 SDL 的硬依赖。构建参数：

- `VG_LVGL_DIR`：默认解析为固件实际 checkout；显式传入时使用该源码，不下载或升级。
- `VG_HEADLESS_COLOR_DEPTH`：只接受 16 或 32；默认 16。
- 复用 `gui/main/ui`、中文字体、现有 alarm/trend harness；不复制产品页面实现。
- 排除依赖 SDL 的 app 启动和键盘轮询代码；按现有 harness 初始化 theme/model/shell。
- 板端容量 profile 使用真实 32 点输入上限和 128 样本长度。通过测试配置/后端 stub 提供数据，不连接 NSH、RS485 或网络。
- 测试代码必须按实际颜色格式和 stride 分配、解码缓冲，不能用 `sizeof(lv_color_t)` 推断显示每像素字节数。
- 固件配置和 PC 配置分别跑，字体的布局差异单独说明，不用 PC 大字体的截图代替板端布局。

新增入口在本任务完成后应支持：

```bash
cmake -S gui/headless -B .debug/hmi-headless -DVG_LVGL_DIR="$PWD/../apps/graphics/lvgl/lvgl" -DVG_HEADLESS_COLOR_DEPTH=16
cmake --build .debug/hmi-headless
ctest --test-dir .debug/hmi-headless --output-on-failure
```

现有测试中写死的 PC 300 点断言应改为随测试 profile 检查对应容量，不能为了通过测试改变产品历史深度。

## Fixture 与证据

- 确定性 fixture 覆盖空表、当前典型规模、32 点、重复名称、23 字符合法 ID、1/3/8 告警、128 点历史。
- 无头测试驱动真实 pointer indev 的按下/移动/释放，不只直接调用事件 handler。
- 基线阶段只要求已有功能冒烟通过；生命周期、过期请求等已知缺陷先记录可复现条件，正式修复测试由子任务 2 接管。
- 原始文件放本次任务独有 `.debug/hmi-baseline/`，结果说明放本任务 `research/baseline-results.md`，后者只有实际运行后才创建或填入结果。
- 摄像原始素材按统一验收契约保存，不把无头帧图算作板端性能。

## 预期改动边界

| 所属 | 预期文件或模块 | 必要性 |
|---|---|---|
| 本仓应用 | `app/velaguard/vg_hmi.c`、拟新增 `vg_hmi_perf.c/.h` | 观察循环、诊断分流 |
| 本仓构建 | `app/velaguard/Kconfig`、`Makefile`、`scripts/build.sh` | 可关闭统计及显式测量构建 |
| 本仓 UI | Shell/model 的最小只读观察点 | 获取阶段和项目资源计数，不改变业务 |
| 本仓测试 | `gui/headless/` | 独立入口、像素格式和 profile |

不改 `gui/lv_conf.h` 的正常模拟器设置，不改公共仓，不改变输入或显示周期。

## 验证与回退

- 编译开关关闭、开启、再次关闭三次配置核对，确认配置无残留。
- 统计空窗、256 样本覆盖、计数增长、不配对事件、并发读取均有主机测试。
- 对照重复操作的结果，检查统计开启是否影响业务状态、资源或调度。
- 如观察工具本身明显影响帧率，先缩减观察开销，不能带着失真的基线进入后续优化。
- 关闭统计是回退观察开销的方式，不删除已经采集的真实失败证据。
