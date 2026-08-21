# Design: Keil-style Build / Rebuild / Download / Debug

## Boundaries

- 改动只在参赛仓：`scripts/`、`.vscode/`、操作文档（`docs/windows_build_debug_setup.md`、
  `docs/windows_vscode_coding_setup.md`、`docs/velaguard-bringup-known-issues.md` 中的入口段落）。
- 不改 `nuttx/`、`apps/`、`packages/`、`vendor/` 公共树正文；继续只通过现有 apply 脚本碰 nuttx。
- 父目录 openvela 的 `./build.sh` 保持不动。本仓入口是 `contest.../scripts/build.sh`，
  文档里写全路径，避免和上游 `build.sh` 混淆。

## Daily operator surface

Remote - WSL Cursor：

| Keil | 本仓 | 实现 |
|------|------|------|
| Build | 任务 `openvela: Build`（默认 Ctrl+Shift+B） | `scripts/build.sh`（固定 velaguard-net） |
| Rebuild | 任务 `openvela: Rebuild` | `scripts/build.sh --clean` |
| Download | 任务 `openvela: Download` | `scripts/flash.sh` |
| Debug | F5 默认 launch `openvela: Debug` | 只杀 OpenOCD 再 attach，不编译、不烧录 |
| （可选） | `openvela: Debug (Download first)` | `build.sh --debug` → 杀 OpenOCD → Download → attach |
| （可选） | `openvela: Build & Download` | Build 成功后再 Download |

按钮不弹预设：一律 `velaguard-net`。`min` / `lvgl` 只留 CLI。
Download 固定烧 `.debug/nuttx.hex` + `.debug/qspi_bootstub.hex`。

## Scripts

```text
scripts/build.sh          # 唯一构建入口（改名自 build_minimal.sh）
scripts/flash.sh          # WSL Download 入口
scripts/flash.ps1         # Cube CLI + HEX 校验（改名并收缩自 windows_flash_cube.ps1）
scripts/wsl_kill_openocd.sh
scripts/wsl_gdb_launcher.sh
scripts/qspi_boot_stub/build_bootstub.sh
scripts/apply-openvela-*.sh   # 已有，build.sh 按目标调用
```

删除：`scripts/windows_build_openvela.ps1`。
删除：`scripts/build_minimal.sh`（git mv 到 `build.sh`，不留 stub）。
删除：`scripts/windows_flash_cube.ps1`（git mv 到 `flash.ps1`）。

### build.sh

保留现有：目标解析、`expect_dev_config`、构建前清空 `.debug` 主镜像、`--clean` 或形态不匹配时
`configure.sh -E -e`。

新增（接过已删 ps1 的职责，调用现有脚本，不复制 kconfig-tweak）：

1. `ensure-openvela-links.sh`
2. 按目标幂等 apply（见 `research/distclean-and-wsl-flash.md` 表）
3. 若缺少 `.debug/qspi_bootstub.hex`，或 `--clean`，则跑 `build_bootstub.sh` 并拷到 `.debug/`

不把 UI/ETH kconfig 开关写进 `build.sh`。预设内容只来自 defconfig 补丁。

### flash.sh / flash.ps1

`flash.ps1` 只保留：解析 Cube CLI / External Loader、HEX 范围断言、三步烧录（QSPI → stub → reset）、
`-ValidateOnly`。去掉所有构建参数（`-NoBuild`、`-DebugBuild`、`-VelaGuardMode`、`-Rebuild` 等）
以及对本已删除 ps1 的调用。

`flash.sh`：把脚本目录转成 Windows 路径，调用

```text
powershell.exe -NoProfile -ExecutionPolicy Bypass -File <flash.ps1>
```

缺 `powershell.exe` / Cube CLI 时以非零退出并打印安装提示。不要在 linux 分支写死 `exit 1`。

## Debug

`.vscode/launch.json` 默认配置改名为 `openvela: Debug`：

- `request: attach`。不要写 `loadFiles: []`：Cortex-Debug 把非空
  `loadFiles`（空数组在 JS 里也是真值）当成“不要 `file-exec-and-symbols`”，
  GDB 会以无符号表启动，源码断点一直空心并报 `No symbol table is loaded`。
  attach 默认只 `monitor halt`，不会用 OpenOCD 写 QSPI。
- `executable`: `.debug/nuttx.elf`；`preAttachCommands` 再执行一次 `file` 兜底。
- F5 默认 `openvela: Debug`：`preLaunchTask` 只杀残留 OpenOCD，基于当前板上固件
  attach。不编译、不 Download。
- 可选 `openvela: Debug (Download first)`：`preLaunchTask` 为 `openvela: Prepare Debug`
  （`build.sh --debug` → 杀 OpenOCD → Download）。
- QSPI XIP（`0x90000000`）强制硬件断点：`gdb breakpoint_override hard`，
  并把该窗口标成 ROM。OpenOCD 只探测片内 128 KiB Flash，软件断点会一直空心。
- 不使用 `sourceFileMap`：DWARF 已是参赛仓绝对路径，`${workspaceFolder}/..`
  会让 GDB 对不上编辑器里打开的文件。

VS Code 的 `preLaunchTask` 目前是字符串或数组；现有数组写法保留。
若数组在 Cortex-Debug 下不可靠，改为一个复合 task `openvela: Prepare Debug`
（kill → Download），launch 只依赖这一条。

## Compatibility / migration

- 操作文档所有 `build_minimal.sh` / `windows_build_openvela.ps1` / `windows_flash_cube.ps1`
  日常入口改为新名。
- 历史博客（`docs/learn/`）不改写过程叙述；可在构建脚本表加“现用 `scripts/build.sh`”。
- 旧 VS Code 任务名消失；用户已固定的任务快捷方式需要重新选一次（可接受）。

## Trade-offs

- F5 默认不烧录、不编译：调试快，改代码后必须先 Build Debug + Download，
  或选 `openvela: Debug (Download first)`。写进文档。
- Rebuild distclean：能从污染 `.config` 恢复，会丢掉未保存的 menuconfig。写进文档。
- Download 继续用 PowerShell 调 Cube CLI，而不是把 HEX 解析重写成 bash：少一份校验实现。

## Rollback

单 commit 可逆：恢复三个脚本文件名、还原 `tasks.json` / `launch.json`、还原 ps1。
不涉及 nuttx 公共仓回滚。
