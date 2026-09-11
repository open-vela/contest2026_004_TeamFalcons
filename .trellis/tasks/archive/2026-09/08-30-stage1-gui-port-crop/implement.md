# 实施计划：gui-port-crop

## Checklist

- [x] I1 rsync `velaguard_gui` → `gui/`，clone lvgl v9.1.0
- [x] I2 `vg_shell.c` 导航策略 + enum 增 REPORT/DISCOVER
- [x] I3 `vg_page_report.c` / `vg_page_discover.c`（扫描开关默认关）
- [x] I4 `vg_page_home.c` 底栏裁剪；alarm 增「AI 推测」行
- [x] I5 `scripts/setup_gui.sh` + `gui/README.md`
- [x] I6 WSL 构建验证（2026-09-02：`cmake --build gui/build` → `gui/bin/main` OK；无 sudo 时用 `$HOME/.local/opt/sdl2-src` 源码静态 SDL2 + 提取 X11 `.so`）

## 验证

```bash
bash scripts/setup_gui.sh
cmake -S gui -B gui/build -DCMAKE_BUILD_TYPE=Debug
cmake --build gui/build -j$(nproc)
./gui/bin/main
```

Windows：见 `gui/README_CN.md`。
