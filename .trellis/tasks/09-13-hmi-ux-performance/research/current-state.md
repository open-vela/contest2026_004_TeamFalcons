# 源码、配置与已有成果核查

## 证据身份

- 核查日期：2026-09-13。
- 本仓分支：`integrate-learn-vela-0912`。
- 本仓提交：`7b7bc4b607b83394651b8c94ce35d7f0cfd7ce79`。
- 固件 LVGL checkout：`../apps/graphics/lvgl/lvgl`，提交 `0f2a49f588505a00e8b46e25a34581c87291a62a`，核查时为 detached HEAD。
- NuttX checkout：`../nuttx`，核查时分支为 `velaguard/eth-mii-stm32h750b-dk`。实施时重新记录提交。
- 创建本包前本仓仅有既存 AI 日志改动，本包不编辑这些日志。

以下路径以本仓根目录为基准，行号为核查时定位信息。接手者必须重新搜索符号，不能盲用旧行号打补丁。

## 确认事实

| 编号 | 文件定位 | 已确认内容 | 规划含义 |
|---|---|---|---|
| E01 | `app/velaguard/vg_hmi.c:171` | 主循环调用 `lv_timer_handler()` 后直接按返回值休眠，零值改为 1 ms | 输入调度需单独观察，不能假设固定 16 ms 循环 |
| E02 | `scripts/configs/velaguard-lvgl.defconfig:71` | LVGL 刷新周期 16 ms | 这是配置值，不是 FPS 实测 |
| E03 | `../nuttx/.config:521` | 帧缓冲基址 `0xd0000000`，522240 字节，双缓冲、RGB565 | 不重复规划新增双缓冲 |
| E04 | `../apps/graphics/lvgl/lvgl/src/drivers/nuttx/lv_nuttx_fbdev.c` | mmap 帧缓冲、DIRECT 绘制、最后一次 flush 提交 `FBIOPAN_DISPLAY` | 提交完成不能直接代表屏幕像素已变化 |
| E05 | `../nuttx/arch/arm/src/stm32h7/stm32_ltdc.c:2574` | 垂直消隐重载，完成中断释放 pending flip | 本轮不改驱动，用真实屏幕核对显示时序 |
| E06 | `../nuttx/.config:684` | `CONFIG_USEC_PER_TICK=10000` | 软件时间精度和休眠取整须实测，不能宣传微秒精度 |
| E07 | `scripts/configs/velaguard-lvgl.defconfig:59` | FT5x06 非轮询模式、单点、SWAPXY、X/Y 阈值 2 | 保留已用输入模式、方向和阈值 |
| E08 | `../nuttx/boards/arm/stm32h7/stm32h750b-dk/src/stm32_ft5x06.c:45` | 触摸 I2C 使用 100 kHz；事件由 HPWORK 读取 | 不直接改成更高总线速率 |
| E09 | `gui/main/ui/shell/vg_shell.c:224` | 模型业务 tick 为 1000 ms | 帧率优化不能让离线计数和持续时间被更快 UI 定时器放大 |
| E10 | `gui/main/ui/shell/vg_shell.c:346` | `lv_obj_clean(s_content)` 后复用同一容器创建页面 | 独立页面根是最小修复位置 |
| E11 | `gui/main/ui/pages/vg_page_report.c:184`、`gui/main/ui/pages/vg_page_home.c:381` | 页面把 DELETE 清理回调注册到传入 parent | parent 未删除时，清理回调不因 clean 子对象而执行 |
| E12 | `../apps/graphics/lvgl/lvgl/src/core/lv_obj_tree.c:89` | `lv_obj_clean()` 删除子对象并发 CHILD 事件，不删除传入对象 | E10/E11 由源码支撑；实际故障频率未测 |
| E13 | `gui/main/ui/pages/vg_page_home.c:285` | 筛选 setter 已通知刷新，回调又强制 `rebuild_list(true)` | 同一操作存在重复结构更新入口 |
| E14 | `gui/main/ui/pages/vg_page_home.c:88` | 首页以 160 字节拼接签名判断可见点集合 | 长 ID/多点时可能无法覆盖尾部结构变化，需精确版本或完整 ID 比较 |
| E15 | `gui/main/ui/pages/vg_page_trend.c:145` | 每次刷新重写窗口内四条序列、范围并刷新图表 | 用历史/配置/窗口版本控制更新，批量提交 |
| E16 | `gui/main/ui/pages/vg_page_report.c:27` | 页面回调调用同步 `read_latest_report` | 文件读取必须与 UI 主线程分离 |
| E17 | `app/velaguard/vg_ui_backend_board.c:448` | 后台函数本身扫描目录并同步 read，当前由 UI 直接调用 | 改调用路径和接口，不能只把函数改名为异步 |
| E18 | `app/velaguard/vg_ui_backend_board.c:814` | UI apply_live 路径 clear/write 主告警文件 | 需保留原有主告警正文和状态语义 |
| E19 | `app/velaguard/vg_ui_backend_board.c:554` | `g_live_cycle` 用于完成轮次去重，数据数组以 volatile 跨线程共享 | volatile 不能保证整批快照一致 |
| E20 | `app/velaguard/vg_point_table.c:621` | 表代次查询和点表 copy 是独立接口，copy 直接复制全局表 | 快照代次与点表需要同一次受保护读取 |
| E21 | `gui/main/ui/model/vg_model.c:1821` | set_live 在同值有效样本时仍追加历史，而 changed 可能为 false | 新样本版本不能仅由当前值变化判断 |
| E22 | `gui/main/ui/pages/vg_page_alarm.c:14` | `ALARM_LIST_MAX=8`，行事件携带数组下标 | 保留展示容量，但点击期间必须锁定真实 ID |
| E23 | `gui/main/inc/vg_display.h` | 480x272，状态栏 28，内容 244，主要按钮触高 36 | 热区优化不能破坏现有布局预算 |
| E24 | `app/velaguard/vg_discover.h:20`、`gui/main/ui/model/vg_model.h:24` | 有效点表上限 32；板端模型容量 64、历史 128；PC 容量 256、历史 300 | 32 点是真机容量压力；不要拿 PC 容量代替板端 |
| E25 | `gui/lv_conf.h:30`、`:860` | PC 色深 32，启用 SDL；板端色深 16，仅 Montserrat 14 启用 | 无头回归要有板端相近配置 |
| E26 | `app/velaguard/vg_agent_alarm.c:80` | pending 写入遇到已有文件直接返回成功；当前调用方先 clear 再 write | 异步化需检查失败返回，不能只投递裸 write |

## 不能从源码推出的结论

- 真实 FPS、P95 触摸延迟、实际 I2C 工作线程延迟及 LTDC 可见完成时间。
- 30 分钟混合操作的内存变化和后台网络压力影响。
- 既有 Agent 系统挂死是否仍能在当前固件重现。
- 当前屏幕自动告警跳页是否完整成立。本次没有找到足以声明运行成功的证据。

本文件未执行构建、串口命令、触摸采集或板测。历史截图只用于理解布局，不作为本任务性能结果。

## 已有成果与依赖

- `.trellis/tasks/archive/2026-09/09-13-alarm-page-multi-rows/`：逐点告警、确认/静音、主告警派生。
- `.trellis/tasks/archive/2026-09/09-13-field-test-fixes-0913/`：轮次去重、筛选刷新、报告按钮、日期。
- `.trellis/tasks/archive/2026-09/09-13-trend-page-live/`：历史深度 128、点位下拉、样本窗口和量程。
- `.trellis/tasks/09-12-heartbeat-llm-round-system-wedge/`：同固件 Agent 压力测试的已知风险来源。
- `.trellis/tasks/09-12-statusbar-live-sync/`、`.trellis/tasks/09-12-time-sync-beijing/`：状态含义、时间同步的相关任务；本任务只减少无意义刷新。

## 规范和历史描述差异

1. 现有前端规范以 LVGL 9.3 描述部分 API，而两个 checkout 的 `lvgl.h` 版本宏显示 9.1.0，固件树又有较新的 API。按实际 checkout/头文件验证，不借此升级依赖。
2. 告警技能里的 200 ms 一轮和约 1 s 离线是历史描述。当前 32 点、9600 波特率与重试可能显著延长整轮时间。
3. 后台接口中的 `mqtt_online`/`aud_ok` 注释包含旧 AI Bridge/声音语义。不得据此恢复已废止产品功能。
4. 项目手册包含较大完整功能清单；本任务只覆盖当前已开放页面，延期功能以根规则为准。

## 硬件证据边界

已只读查看官方 `mb1381-h750xb-b01-schematic.pdf` 的 MCU I/O 和 LCD4.3 页面，以及资料包中的 ST LCD/触摸 BSP。官方原理图位置：

- Windows：`F:\Project\Embeded\H750B-DK\BOARD INFO\H750B-DK`。
- WSL：`/mnt/f/Project/Embeded/H750B-DK/BOARD INFO/H750B-DK`。

本任务不提出任何引脚、时钟、I2C 速率或 LTDC 时序变更。若后续测量要求修改硬件相关驱动，必须按 `velaguard-board-hw` 重新核查原理图并取得扩展范围批准，不能把本次只读浏览当作全部硬件审核完成。
