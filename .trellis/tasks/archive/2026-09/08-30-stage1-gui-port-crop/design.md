# 设计：velaguard_gui 移植与手册裁剪

## 1. 目录布局

```text
contest2026_004_TeamFalcons/gui/          # 自 FoLeaf/velaguard_gui release/v9.1 迁入
├── main/ui/                              # HMI 源码（裁剪在此层）
├── main/src/ main/inc/
├── lvgl/                                 # LVGL 9.1.0（git clone，见 scripts/setup_gui.sh）
├── lv_conf.h  CMakeLists.txt
├── README_CN.md                          # Windows 构建说明
└── scripts/setup_gui.sh                  # WSL：SDL2 + lvgl 初始化
```

**同步策略**：contest 仓为集成副本；上游功能开发仍在 `FoLeaf/velaguard_gui`，定期 cherry-pick / rsync `main/ui/`。

## 2. 导航裁剪（stage1）

在 `vg_nav_goto()` 入口做 **策略拦截**（`vg_shell.c`）：

| 页面 ID | 行为 |
|---------|------|
| HOME, DEVICE, ALARM, REPORT, DISCOVER | 正常进入 |
| TREND, DIAGNOSIS, LOGS, SYSTEM, ADD_SENSOR | toast「阶段 2 提供」，不导航 |
| OTA | toast「阶段 3 提供」，不导航 |

首页底栏：**详情 · 报告 · 探查 · 静音**（手册 §6.3）。

## 3. 新增页面

| 文件 | 职责 |
|------|------|
| `vg_page_report.c` | 日报/周报预览（mock → Phase B 读 `/data/agent/reports/`） |
| `vg_page_discover.c` | 总线探查：扫描开关默认 **OFF**；ON 后「开始扫描」+ mock 从站列表 |

## 4. vg_ui_backend（Phase B 预留）

```c
/* model/vg_ui_backend.h — PC 阶段 vg_model 实现，上板换真实源 */
typedef struct {
  int (*get_slaves)(vg_ui_slave_t *out, int max);
  int (*discover_scan)(int addr_min, int addr_max);  /* 接 vgdiscover C API */
} vg_ui_backend_t;
```

本任务仅声明/占位，不阻塞模拟器 Gate。

## 5. 与 modbus-discovery 对接

- Phase A：discover 页 mock 扫描结果（与 MThings 32 从站文档一致即可）
- Phase B：`vg_ui_backend` 板端实装 → **父任务 `08-30-stage1-lvgl-hmi`**

## 6. 构建 Gate

- WSL：`scripts/setup_gui.sh` + `cmake -B build && cmake --build build -j`
- Windows：沿用 `README_CN.md` llvm-mingw 路径
