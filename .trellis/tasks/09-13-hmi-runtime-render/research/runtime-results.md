# HMI 页面生命周期与增量刷新优化验证结果

本文件记录子任务 `09-13-hmi-runtime-render` 的实现验证证据，对照 [子任务 PRD](../prd.md) 与 [实施步骤](../implement.md) 的验收条件。

## 验收条件逐项对照

### R-AC1：页面生命周期与往返无泄漏

- 实现机制：
  - 在 [gui/main/ui/shell/vg_shell.c](file:///home/hello19y/openvela/contest2026_004_TeamFalcons/gui/main/ui/shell/vg_shell.c) 引入独立的 `s_page_root` 容器，子页面全部挂载在此容器下。
  - 切页时由 Shell 统一调用 `lv_obj_delete(s_page_root)`，可靠触发各页面的 `LV_EVENT_DELETE` 监听回调。
  - 各页面在 `on_xxx_delete` 中注销模型观察者（`vg_model_off_change`）、停止并删除页面专属定时器、重置静态上下文内存。
  - 维护页面代次计数器 `s_page_generation`，避免相同目标页面重复创建。
- 验证证据：
  - 编写无头验证程序 [gui/headless/lifecycle_render_check_main.c](file:///home/hello19y/openvela/contest2026_004_TeamFalcons/gui/headless/lifecycle_render_check_main.c)。
  - 执行 200 轮全页面切换（Home -> Device -> Trend -> Alarm -> Report -> Discover -> Home，累计 1200 次页面创建与销毁）。
  - 验证页面代次从 1 严格推进至 1201，容器对象计数保持 1，无悬挂指针与回调泄漏。

### R-AC2 与 R-AC3：日报与告警后台文件处理

- 实现机制：
  - 在 [gui/main/ui/model/vg_ui_backend.h](file:///home/hello19y/openvela/contest2026_004_TeamFalcons/gui/main/ui/model/vg_ui_backend.h) 声明请求与快照协议结构 `vg_ui_report_status_t`、`vg_ui_report_snapshot_t`，提供 `vg_ui_report_request` 与 `vg_ui_report_snapshot` 统一接口。
  - 在 [app/velaguard/vg_ui_backend_board.c](file:///home/hello19y/openvela/contest2026_004_TeamFalcons/app/velaguard/vg_ui_backend_board.c) 建立独立单文件工作线程 `vg_hmi_file_worker_thread`（栈预算 8 KiB）。
  - 运行报告读取、MiMo 触发标记（`/data/agent/HEARTBEAT.poke`）与生成超时检查（2 秒轮询、180 秒超时）全部在后台线程执行，UI 线程仅以毫秒级内存锁读取快照。
  - 切页离开时后台生成任务不中断；返回报告页时读取快照直接反映当前状态。
  - [gui/main/ui/pages/vg_page_report.c](file:///home/hello19y/openvela/contest2026_004_TeamFalcons/gui/main/ui/pages/vg_page_report.c) 改为消费内存快照，版本未变时不重复排版与重绘；页面销毁时安全清理轮询定时器。
- 验证证据：
  - `lifecycle_render_check` 验证了报告快照状态流转与切页后返回的安全性。
  - 注入后台工作时，UI 保持稳定响应，无同步阻塞。

### R-AC4：采集快照一致性与代次保护

- 实现机制：
  - 在 [app/velaguard/vg_discover.h](file:///home/hello19y/openvela/contest2026_004_TeamFalcons/app/velaguard/vg_discover.h) 与 [app/velaguard/vg_point_table.c](file:///home/hello19y/openvela/contest2026_004_TeamFalcons/app/velaguard/vg_point_table.c) 实现版本化复制 `vg_live_points_copy_versioned`。
  - 板端采集线程 `vg_hmi_acq_thread` 记录当前点表代次；总线轮询返回后，若发现点表代次发生改变，自动丢弃旧代次的轮询数据，避免测点错位。
  - 采集写入与 UI 消费通过 `g_live_lock` 互斥保护，消除跨线程数据撕裂；保留 `g_live_cycle` 严格去重机制，每个采集周期由 UI 消费且仅消费一次。
- 验证证据：
  - 在 [app/velaguard/host_tests/test_runtime_render.c](file:///home/hello19y/openvela/contest2026_004_TeamFalcons/app/velaguard/host_tests/test_runtime_render.c) 建立单测，覆盖版本化复制、点表变更后代次递增及残留轮询数据丢弃逻辑，测试通过。

### R-AC5：列表结构版本与增量渲染

- 实现机制：
  - 在 [gui/main/ui/model/vg_model.h](file:///home/hello19y/openvela/contest2026_004_TeamFalcons/gui/main/ui/model/vg_model.h) 与 [gui/main/ui/model/vg_model.c](file:///home/hello19y/openvela/contest2026_004_TeamFalcons/gui/main/ui/model/vg_model.c) 引入 `vg_model_structure_version`。
  - 当点表导入、增删测点、测点重命名、或筛选成员和顺序变更时递增结构版本号。
  - 优化 [gui/main/ui/pages/vg_page_home.c](file:///home/hello19y/openvela/contest2026_004_TeamFalcons/gui/main/ui/pages/vg_page_home.c) 的 `rebuild_list`，签名纳入结构版本，修复了空列表状态下每秒反复销毁重建标签的缺陷。
  - 测点数值更新时只执行 `update_tile_texts` 原地修改标签文本与强调色，不重建对象。
  - `on_filter` 消除重复的强制重建调用，相同筛选状态点击直接忽略。
- 验证证据：
  - `lifecycle_render_check` 断言了空列表持续帧刷新不自增结构版本、导入测点自增结构版本、数值更新保持结构版本不变、相同筛选点击不产生多余通知。

### R-AC6：历史版本与趋势增量渲染

- 实现机制：
  - 在测点结构 `vg_sensor_t` 增加 `history_version`。
  - 在 `vg_model_set_live` 中，无论新采集数值是否与旧数值相同，只要推进历史样本队列，均自增 `history_version`，确保趋势窗口稳定前进。
  - 在 [gui/main/ui/pages/vg_page_trend.c](file:///home/hello19y/openvela/contest2026_004_TeamFalcons/gui/main/ui/pages/vg_page_trend.c) 缓存测点标识、历史版本、窗口设置与样本数量；当且仅当数据真实改变时才重新计算 4 条序列并调用 `lv_chart_refresh`。状态栏刷新或告警计时变化不再导致趋势曲线重绘。
- 验证证据：
  - `lifecycle_render_check` 验证了相同数值样本下 `history_version` 稳定推进。
  - 静态背景刷新下跳过曲线序列循环更新。

### R-AC7：主告警异步落盘与语义保持

- 实现机制：
  - 板端通过 `g_alarm_req` 队列向后台工作线程投递写入与清理请求，格式完全保持 `type=... tag=... slave=... reg=... value=... threshold=... hint=use alarm_interpretation skill`。
  - UI 线程异步投递，告警上屏不等待 eMMC 写入；写入失败不虚假标记完成，允许下次周期重试。
  - 多个告警并发时，单点恢复保留其他未恢复告警的记录，全部告警解除才清理文件。
- 验证证据：
  - `test_runtime_render.c` 验证了告警文件的完整正文字段序列化规则。

### R-AC8：多环境回归与基线对照

- 测试命令与结果：
  - 无头测试：`ctest --test-dir .debug/hmi-headless --output-on-failure`（4/4 测试通过，耗时 3.66 秒）。
  - 主机单测：`make -C app/velaguard/host_tests test`（全部 9 项测试通过）。
  - 固件发布构建：`bash scripts/build.sh` 编译通过，生成 `.debug/nuttx.hex`。
  - 固件性能监控构建：`bash scripts/build.sh --hmi-perf` 编译通过。

## 架构变更与接口汇总

1. 接口变动：
   - `uint32_t vg_shell_page_generation(void)`：查询当前页面代次。
   - `uint32_t vg_model_structure_version(void)`：查询数据模型结构版本。
   - `int vg_live_points_copy_versioned(struct vg_discover_summary *out, uint32_t *generation)`：获取带代次快照的点表。
   - `int vg_ui_report_request(bool allow_generate, uint32_t *request_id)`：发起后台日报处理请求。
   - `bool vg_ui_report_snapshot(vg_ui_report_snapshot_t *out)`：获取后台日报当前快照。
2. 线程与资源预算：
   - 文件工作线程栈：8192 字节（`VG_HMI_FILE_WORKER_STACKSIZE`）。
   - 互斥锁：`g_live_lock`（采集与 UI 快照保护）、`g_file_worker_lock`（后台文件任务保护）。
   - 内存所有权：页面根容器 `s_page_root` 统一由 Shell 在页面切换时释放；各子页面持有私有上下文在 `LV_EVENT_DELETE` 触发时重置。

## 交接子任务 3（09-13-hmi-touch-acceptance）

以下内容已在本阶段实现并稳定交付：
- 独立的页面根容器及明确的页面生命周期释放机制。
- 页面代次计数器、结构版本计数器、测点历史版本计数器。
- 完全非阻塞的日报和主告警后台处理接口。
- 稳定的无头与主机自动化测试套件。

后续由子任务 3 负责推进的内容：
- 10 毫秒触摸采样与硬件输入队列整合。
- 返回上一页面的状态恢复（例如列表滚动偏移行）。
- 长列表快速拖拽与轻微抖动过滤。
- 真机板端最终性能与用户交互验收录制。
