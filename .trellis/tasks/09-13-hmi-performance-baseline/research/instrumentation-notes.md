# 测量接入依据

## 可用入口

- `app/velaguard/vg_hmi.c`：NuttX HMI 主线程入口，适合观察循环和分流只读命令。
- `../apps/graphics/lvgl/lvgl/src/indev/lv_indev.h`：已核实有 `lv_indev_get_read_cb` 和 `lv_indev_get_read_timer`。
- `../apps/graphics/lvgl/lvgl/src/misc/lv_event.h`：已核实有 RENDER、FLUSH 相关事件；具体计时配对须按当前源码确认。
- `gui/headless/alarm_check_main.c`、`trend_check_main.c`：现有真实 UI、离屏 buffer、pointer indev 及模型断言示例。
- `gui/CMakeLists.txt`：目前顶层依赖 SDL；独立 headless 入口可避免为运行逻辑测试安装图形环境。

本文件只确认接入位置，不代表已实现性能工具。

## 不可混淆

| 看似相同的指标 | 实际差别 |
|---|---|
| 16 ms 周期 / 60 FPS | 周期只是调度设置，实际还受绘制、提交、系统 tick 和扫描影响 |
| flush 回调返回 / 屏幕可见 | 双缓冲 PAN 和垂直消隐是异步链路 |
| `lv_color_t` / 像素格式 | buffer 字节数要按 color format 和 stride 计算 |
| 1 Hz 业务 tick / 触摸读取 | 前者用于业务模型，不能为了触摸响应直接改快业务 tick |
| PC 测试 / 真机验收 | PC 只能证明行为和布局；不能证明 STM32 的 FPS 或触摸延迟 |
| 最近 256 个样本 / 整场景 P95 | 窗口覆盖后不能宣称保存了整场景分布 |

## 基线期间允许与禁止

允许给项目代码加最小观察点、调试查询和配置开关。禁止同时修复明显性能问题后再称结果为优化前基线；若测试自身存在像素解码等错误，只修测试并记录，不改产品行为。

未来基线结果应明确包含检测到的失败、未覆盖组及原因，不使用预填的成功样例。
