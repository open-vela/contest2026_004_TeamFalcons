# 实施计划：stage1-lvgl-hmi（Phase B）

> **前置 Gate**：子任务 `08-30-stage1-gui-port-crop` Phase A 完成（`gui/` 裁剪 UI + PC 可构建）。

## Phase 0 — 规划（本步）

- [x] P0 更新 prd / design / implement；明确 A/B 分工
- [x] P1 `task.py start 08-30-stage1-lvgl-hmi`

## Phase B 执行顺序

### B1 构建 spike（点亮屏）

- [x] B1.1 基于 `velaguard-lvgl` defconfig 编过（QSPI XIP + 无 mqtt 链接）
- [x] B1.2 `vg_hmi.c`：LVGL NuttX init + tick 循环
- [x] B1.3 板端见占位屏（`vghmi &` → 480×272 + touch；LCD 目视）

### B2 UI 同源编译

- [x] B2.1 Makefile 纳入 `gui/main/ui/**/*.c`（`VG_UI_DIR` + `VG_HMI_BOARD`）
- [x] B2.2 字体 fallback 板端置 NULL（CJK 子集已内嵌）；480×272 与 `vg_display.h` 一致
- [x] B2.3 `vg_app.c` 去除 SDL；`vg_hmi.c` 调用 `vg_app_init()` + touch indev

### B3 backend 实装

- [x] B3.1 `vg_ui_backend_board.c`：discover work_queue + get_slaves；板端首页空态直至扫描
- [x] B3.2 报告页读 `/data/agent/reports/daily-*.md`（无 FS 时 empty 态）
- [x] B3.3 discover 页接 C API（async scan + poll）；开关默认关

### B4 net 合并与验收

- [x] B4.1 defconfig 合并 net + HMI（`velaguard-lvgl` = 演示固件；`net` 仍无 LVGL）
- [x] B4.2 `scripts/stage1_lvgl_hmi_accept.ps1` + 笔记（autostart + vgdiscover）
- [x] B4.3 NSH 验收 9/9 PASS（2026-09-01）；**2026-09-02 续推 11/11 PASS**（复位后抓 boot；480×272 + touch + vghmi autostart）

### C — 剩余板端（父任务剩余闭环）

- [x] C1 目视裁剪首页；扫描开关默认关（代码默认关；**2026-09-09** NSH：冷启动无 `vg_bus_scan` / `vghmi scan`）
- [x] C2 探查 ON → scan @9600 → ≥1 从站（用户板测：扫描能完成并出列表）
- [x] C3 confirm → `vgcfg dump` / `points.json`（二次确认 high；落盘仅在确认后）
- [x] C4 首页真实从站（冷启动读 `points.json`；**2026-09-09** `vghmi: home fleet n=32` + `hmi_fleet.txt`）
- [x] C5 报告页 empty 态（NSH reports 缺失=empty；UI 有 empty 文案）
- [x] C6 告警页「AI 推测」（首页「告警」入口 + 顶栏常驻；CJK「推」已补）
- [x] C7 验收脚本扩展 NSH 对照（11/11 PASS）
- [x] D1（尽力）HMI 存活时 `ai_agent &` → **仍 assert**（2026-09-01；Q1=B 不阻塞）

## 2026-09-01 续推笔记

- CJK 全量扫 UI 字符串补字；`regen_cjk_font.sh`
- `net`：`ai_agent` 自启/手动均曾 assert（64K 栈仍 panic）；暂关 `VG_AGENT_AUTOSTART` 保 NSH
- E1 CLI 查数：**SKIP** — 无 `MIMO_API_KEY` / `secrets/agent_llm.key`，且 agent 当前不稳定


## 验证命令（草案）

```bash
# 构建（目标名 implement 阶段定案）
bash scripts/build.sh velaguard-lvgl --clean
# flash ~849KB QSPI；SRAM ~92.6%（板端 VG_SENSOR_MAX=32）

# 烧录 + 板测（需板子上电、ST-LINK Voltage≠0）
powershell.exe -File scripts/flash.ps1
powershell.exe -File scripts/stage1_lvgl_hmi_accept.ps1
```

## Rollback

- HMI 独立 Kconfig `CONFIG_VG_HMI=n` 回退 net-only 固件
- UI 源码仍在 `gui/`，不影响 NSH 工具链
