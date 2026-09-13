# B1 链路核对（实施会话实测）

核查时间：2026-09-13，实施会话（09-13-hmi-performance-baseline 已激活）。
对照父任务 `research/current-state.md` E01-E09、E23-E25 重新定位。

## 证据身份

- 本仓：`7b7bc4b607b83394651b8c94ce35d7f0cfd7ce79`，分支 `integrate-learn-vela-0912`。
  脏文件仅为既存 AI 日志（logs/Foleaf/manifest.json、zcode sess jsonl）和本任务包四个未跟踪目录；无产品源码改动。
- NuttX：`18bec160782263e7e2ed0f8b9ca06bbda69a5c9f`，分支 `velaguard/eth-mii-stm32h750b-dk`，工作区干净。
  （父任务记录了分支，本次补记实际提交。）
- 固件 LVGL checkout `../apps/graphics/lvgl/lvgl`：`0f2a49f588505a00e8b46e25a34581c87291a62a`，detached HEAD，干净。与父任务记录一致。
- PC LVGL checkout `gui/lvgl`：同为 9.1 版本（lvgl.h 版本宏 MINOR=1）。

## E01-E09、E23-E25 复核结果

全部与父任务一致，仅补注实施时行号：

- E01 `app/velaguard/vg_hmi.c:180`（while 主循环）：`idle = lv_timer_handler(); idle = idle ? idle : 1; usleep(idle*1000)`。零返回改 1 ms 休眠仍在。
- E02 `scripts/configs/velaguard-lvgl.defconfig`：`CONFIG_LV_DEF_REFR_PERIOD=16`、`CONFIG_LV_NUTTX_VSYNC_TIMER_PERIOD=16`。
- E06 `../nuttx/.config:684`：`CONFIG_USEC_PER_TICK=10000`。
- E07 FT5x06：非轮询、单点、SWAPXY、阈值 2（defconfig 原位）。
- E09 `gui/main/ui/shell/vg_shell.c:225-226`：`s_clock_tmr`、`s_model_tmr` 均为 1000 ms。
- E23 `gui/main/inc/vg_display.h`：480x272、`VG_MIN_TOUCH_H 36`。
- E24 容量：`vg_discover.h:20` 32 点上限；`vg_model.h` 板端 64 点/128 历史（`#else` 分支 PC 256 点/300 历史）。
- E25 `gui/lv_conf.h:30` PC `LV_COLOR_DEPTH 32`、`:404` Montserrat 14。

## 实施新增核实（此前未记录）

| 事实 | 位置 | 含义 |
|---|---|---|
| `lv_timer_get_next(NULL)` 公开枚举存在 | `lv_timer.h:237`，头文件注明 NULL 起始迭代 | 定时器计数可用公开 API，由 HMI 线程在快照时枚举；输出 timers_total 并单列 shell 自有定时器 |
| `lv_indev_get_next` 公开迭代存在 | `lv_indev.h:104` | 板端 init 后可包装全部 indev 的 read cb（配合 `lv_indev_get_read_cb`） |
| RENDER 事件配对语义 | `lv_refr.c:744`（RENDER_START，进入绘制前）、`lv_refr.c:407`（RENDER_READY，本轮失效区域绘制完） | 一轮有效刷新一对一配对；layout 更新发生在 REFR_START 与 RENDER_START 之间，不计入 render 段 |
| FLUSH 事件语义 | `lv_refr.c:1345/1347`：START/FINISH 紧包单次 `flush_cb` 调用 | 每次 flush 一对、顺序调用不嵌套；FINISH 是回调返回，不代表面板扫描完成 |
| REFR_START/READY 事件也存在 | `lv_refr.c:381/436` | 设计的六项指标不含 REFR 段，维持不加，避免指标扩张 |
| `vg_model_tick` 调用点唯一 | `vg_shell.c:77`（model_tick_cb） | 在 shell 包装处计时即覆盖全部 UI 驱动 tick |
| 页面自有定时器 | `vg_page_report.c:159` 报告轮询、`vg_page_discover.c:285/313` 扫描/应用轮询 | timers_total 中除 LVGL 核心与 shell 外即页面定时器；快照输出 totals + shell 数并说明 |
| 模型监听器存储 | `vg_model.c:44-45` `s_listeners[VG_MAX_LISTENERS]`、`s_listener_n` | 增加只读计数查询即可 |
| 板端 cpuload | `../nuttx/.config:749` `CONFIG_SCHED_CPULOAD_SYSCLK=y`，`CONFIG_FS_PROCFS=y` | `/proc/cpuload`、`/proc/meminfo` 可尝试读取；未挂载时输出 unavailable，不打印伪零值 |
| host_tests 模式 | `app/velaguard/host_tests/Makefile`：每测试一条 gcc 编译行，`-Wall -Werror -I..` | 新增 test_hmi_perf 按同一模式接入 `make test` |
| headless 硬编码 | `gui/headless/trend_check_main.c:281` 断言"当前 300 点"；两个 harness 的 `lvbuf` 用 `sizeof(lv_color_t)` 分配 | 按设计改为随 profile 的容量断言和按 color format/stride 分配 |

## 构建链路

- `scripts/build.sh`：`expect_dev_config` 按 .config 关键符号校验形态，不匹配则 distclean 重装 defconfig。
  `--hmi-perf` 方案：在配置就绪后 `kconfig-tweak --enable CONFIG_VG_HMI_PERF` + olddefconfig，并清 `build/velaguard` 前缀对象后 make；
  默认构建的 `expect_dev_config` 增加 `! CONFIG_VG_HMI_PERF=y` 校验，保证统计开启残留会被复位为关闭。
- `app/velaguard/Makefile`：`CONFIG_VG_HMI` 段内追加 `vg_hmi_perf.c`，include 路径已含 `$(CURDIR)`，gui 源可直接 include 头文件。
- `gui/CMakeLists.txt`（SDL 模拟器）编译 gui/main/ui；vg_shell.c 引入 perf 头后需为该构建补 app/velaguard include 路径（无行为变化）。

## 与初始证据的差异

1. NuttX 提交补记为 `18bec16`（父任务只记了分支名）。
2. LVGL 事件配对语义从源码确认，替代设计文档中的"按实际事件验证配对"占位。
3. 定时器枚举、indev 迭代两个公开 API 确认可用，无需在固件 LVGL checkout 增加任何改动。
