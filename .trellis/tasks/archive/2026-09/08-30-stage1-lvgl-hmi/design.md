# 设计：stage1 LVGL HMI（Phase B）

> Phase A 见 [`08-30-stage1-gui-port-crop/design.md`](../08-30-stage1-gui-port-crop/design.md)

## 1. 源码与构建边界

```text
gui/main/ui/              # 唯一 UI 源码（Phase A 维护）
    ↓ 编译时 include + 同源 .c
app/velaguard/hmi/        # Phase B：NuttX 入口 + backend + lv_conf 适配
  vg_hmi_main.c           # lv_timer_handler 循环 / 注册 indev
  vg_ui_backend_board.c   # 实装 vg_ui_backend.h
  Kconfig / Makefile

nuttx/boards/.../velaguard-lvgl/defconfig   # 或 net+lvgl 合并 defconfig
```

**不同步复制** pages 到两处；CMake（PC）与 Makefile（NuttX）均编译 `gui/main/ui/**/*.c`（或 git submodule 路径等价）。

## 2. defconfig 策略（待 implement 二选一）

| 方案 | 优点 | 风险 |
|------|------|------|
| **B1** 新预设 `velaguard-lvgl-net` | 一次烧录演示全链路 | Kconfig 冲突多；SDRAM 帧缓冲 + 网络栈 |
| **B2** 保持 `net` 默认，HMI 可选 `CONFIG_VG_HMI` | 与现网测兼容 | 固件体积；LTDC 未开时需子选项 |

**倾向（已定案）**：演示固件走 **B1 形态但复用已有名 `velaguard-lvgl`**（一次烧录含 net+HMI+Agent）；日常 `velaguard-net` 保持无 LVGL（B2 长期入口）。

`velaguard_app_main` 为 INIT；`VG_HMI_AUTOSTART` 在 `/dev/fb0` 出现后 `task_create(vghmi)`。contest 仓 `scripts/configs/velaguard-lvgl.defconfig` 为预设 SoT，`ensure-upstream` 安装到 nuttx `configs/velaguard-lvgl/`。

参考：

```bash
tools/configure.sh -e stm32h750b-dk:lvgl      # 上游 UI 基座
tools/configure.sh -e stm32h750b-dk:velaguard-net
```

## 3. vg_ui_backend（板端）

```c
/* gui/main/ui/model/vg_ui_backend.h — PC 与板端共用接口 */

typedef struct {
  int (*discover_scan)(int amin, int amax);   /* vg_bus_scan */
  int (*discover_probe)(uint8_t addr);        /* vg_reg_probe_slave */
  int (*get_slaves_live)(vg_ui_slave_t *, int);
  int (*get_active_alarm)(...);
  int (*read_latest_report)(char *buf, size_t n, char *path, size_t pn);
} vg_ui_backend_t;
```

- PC：`vg_ui_backend.c` mock（Phase A 已有）
- 板：`vg_ui_backend_board.c` 链 `app/velaguard/vg_discover*.c`、`vg_stats`、agent 报告目录

discover 页 **不** 在板端直接 `system("vgdiscover ...")`；优先同进程 C API，避免阻塞 UI 线程 — 必要时 `work_queue` 或 NSH 线程封装。

## 4. 扫描开关与采集互斥

| 状态 | 行为 |
|------|------|
| 开关 OFF（默认） | 正常 `modbus_collector`；discover UI 只读展示缓存 |
| 开关 ON | 暂停或降频采集（Kconfig/运行时 flag）；允许 scan/probe |
| 开关 OFF（用户关闭） | 恢复采集；清空 in-progress scan UI |

与手册 §5.1 一致；实现细节在 `vg_hmi_discover.c` 协调 `vgdiscover` 状态机。

## 5. 页面与 Phase A 对齐

无新增页面；Phase B 只替换 **数据源** 与 **discover 后端**。Defer/OTA toast 逻辑保留在 `vg_shell.c`。

## 6. 验收

- 脚本：`scripts/stage1_lvgl_hmi_accept.ps1`（新建）或扩展 modbus discovery 脚本
- 手动：NSH 并行 `vgcfg dump`、`vgstats` 与屏上读数对照
