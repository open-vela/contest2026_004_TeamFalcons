# VelaGuard HMI（PC 模拟器）

自 [FoLeaf/velaguard_gui](https://github.com/FoLeaf/velaguard_gui) `release/v9.1` 迁入 contest 仓，并按 `VelaGuard_项目手册.md` §6 **裁剪**：

- 保留：首页、从站详情、告警（含规则摘要）、**运行报告**、**总线探查**（扫描开关默认关）
- 占位 toast：OTA →「阶段 3 提供」；趋势/诊断/日志/系统/添加 →「阶段 2 提供」

## WSL / Linux

```bash
bash gui/scripts/setup_gui.sh
cmake -S gui -B gui/build -DCMAKE_BUILD_TYPE=Debug
cmake --build gui/build -j$(nproc)
./gui/bin/main
```

## 板端（NuttX）

与 PC 共用 `gui/main/ui/` 源码；由 `app/velaguard/Makefile` 在 `CONFIG_VG_HMI=y` 时编入 `vghmi`：

```bash
bash scripts/build.sh velaguard-lvgl
powershell.exe -File scripts/flash.ps1
# 冷启动自动进 HMI（VG_HMI_AUTOSTART）；NSH 仍可用 vgdiscover / vgcfg
```

## Windows

见 `gui/README_CN.md`（llvm-mingw + 本地 SDL2）。

## 上游同步

功能开发可继续在 `FoLeaf/velaguard_gui`；集成时同步 `main/ui/` 到本目录并重新应用裁剪（见 `.trellis/tasks/08-30-stage1-gui-port-crop/`）。
