# 设计：点表数据趋势页恢复与增强

## 现状与数据流

- 采集：`app/velaguard/vg_ui_backend_board.c` 的 `vg_hmi_acq_thread` 每 200 ms 醒一次，每轮同步轮询全部点位（FC03/FC04 单寄存器类读取 + 一次重试），一轮完成后 `g_live_cycle` 递增；1 Hz 的 `vg_model_tick` 经 `vg_ui_backend_apply_live` 按 cycle 去重后逐点调用 `vg_model_set_live`。
- 历史：`vg_sensor_t.history[VG_HISTORY_LEN]` 为移位数组（`vg_model_set_live` 在线分支追加，满则 memmove 左移）。板上 16、模拟器 300（1 Hz 合成正弦）。
- 页面：`vg_page_trend.c` 读 `vg_model_get_selected_sensor()` 的 history 绘图，4 series（数据 / 预警线 / 严重线 / 超限红点），LVGL 9 逐系列线宽依赖 `chart_draw_task_cb` + `LV_OBJ_FLAG_SEND_DRAW_TASK_EVENTS`（异常系列必须是第 4 个添加的系列）。
- 门禁：`vg_shell.c` `nav_allowed()` 将 TREND 归入阶段 2，首页/设备页入口被 toast 拦截。

## 内存预算（128 点历史依据）

- 增量 = 4 字节/样本 × 64 点位（VG_SENSOR_MAX 板上值）× 112 样本 ≈ 28.7 KB BSS。
- 构建产物实测（nuttx.map，2026-09-13）：.data 0x3588 + .bss 0x2a1a0 ≈ 186 KB，落在 512 KB AXI sram 区（0x24000000 + 0x80000）；LTDC 帧缓冲位于 0xd0000000 外部存储器（CONFIG_STM32H7_LTDC_FB_BASE，522240 字节），不占内部 SRAM；mbedTLS 与网络栈已链入当前固件，其运行期堆开销已在当前基线内。
- 结论：+28 KB 安全；留板端构建后 `arm-none-eabi-size` 前后对比与上电自验收作为最终门禁，异常时回退 64（+12 KB）。

## 窗口语义

板上样本间隔 = 一轮轮询真实耗时（14 点 9600 波特下为秒级），`period_ms` 是名义值 1000 不反映实况，按时间标注会失真。改为按样本数：最近60点 / 全部。模拟器 1 Hz 下 60 点即 1 分钟，300 点即 5 分钟，语义无损。meta 行显示当前点数。

## 改动设计

### 块 1：`gui/main/ui/model/vg_model.h`

- `VG_HMI_BOARD` 分支 `VG_HISTORY_LEN` 16 → 128；`VG_SENSOR_MAX` 维持 64。
- 重写注释：删除 stage-2 延期旧话术，写明 128 的 BSS 增量（≈28 KB）、基线余量来源（nuttx.map 实测）、回退路径（64）。

### 块 2：`gui/main/ui/pages/vg_page_trend.c`

- 头部第一行改为：下拉选择器（lv_dropdown，宽约 130 px，显示当前点位 name）+ 当前值 + 单位；窗口按钮与 meta 保持在头部右侧。
- 下拉数据源：`vg_model_get_sensors(&count)`，构建 `name (id)` 或纯 name 文本表（name 重复时附带 id 尾缀消歧），私有映射数组保存 index→id 指针；选中回调按 id 调 `vg_model_set_selected_sensor(id)` 后调 `refresh_trend(NULL)`。
- 创建时：用 `vg_model_get_selected_sensor_id()` 定位初始 index，无选中时落到 index 0。
- 新增点位名 label：显示当前点位 name，与当前值同行（当前值左侧或下拉框内）；下拉框文本即点位名，可兼作名称显示，省一个 label，头部更宽松 —— 采用此方案，另在 meta 行保留阈值信息。
- 窗口按钮文案：1分钟/5分钟 → 最近60点 / 全部；`window_count()` 语义不变（60 / VG_HISTORY_LEN），`window_offset` 不变；meta 行的 最近 %d 分钟 改为 当前 %u 点。
- Y 轴量程：`has_warn||has_crit` 任一为真时沿用 `(thr_low-5, thr_crit+10)`；否则扫描窗口内 history 求 min/max，上下各加 10% 跨度边距（跨度为 0 时 ±1），保证曲线可见。
- 离线：现状已显示 `--` 且图表保留最后样本，保持不变。
- 不改动：4 series 添加顺序、chart_draw_task_cb、删除回调注销监听。

### 块 3：`gui/main/ui/shell/vg_shell.c`

- `nav_allowed()` 中将 `case VG_PAGE_TREND:` 从阶段 2 分支移除（该分支保留 DIAGNOSIS/LOGS/SYSTEM/ADD_SENSOR）。
- OTA 阶段 3 门禁不动。

## 兼容与回滚

- 三块相互独立，均可单独 revert：块 1 回退历史深度、块 2 回退页面增强（趋势页回到只读跟随全局选中）、块 3 恢复门禁。
- 不改vg_model.c / vg_ui_backend*（避开在途任务文件），模拟器与板端共用页面代码无需分叉。
