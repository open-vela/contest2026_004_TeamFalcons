# 优化编译烧录调试脚本，对标 Keil Build/Rebuild/Download/Debug

## Goal

在 Remote - WSL 的 Cursor 单窗口里，用四个一等动作完成日常固件循环：
Build（增量编译）、Rebuild（全量重编并复位到所选预设）、Download（只烧录）、
Debug（先 Download 再 attach）。脚本去掉 `minimal` 等历史后缀，只保留一套入口。

用户价值：点按钮的结果可预期，不再在两套构建管线、两个 IDE 窗口和过期文档之间猜测。

## Background

- 日常构建已是 `scripts/build_minimal.sh`（`net|min|lvgl`，`--clean` 走
  `configure.sh -E -e`）。见 `scripts/build_minimal.sh:4-20`、`:92-94`。
- `.vscode/tasks.json` 已有 Build / Rebuild All / Flash / Build & Flash；
  Flash 的 linux 分支直接 `exit 1`（`:112-117`），Remote - WSL 窗口无法 Download。
- `.vscode/launch.json` 两条 Cortex-Debug attach：`after Cube flash` 与 `attach only`；
  `loadFiles: []`，OpenOCD 不写 QSPI。
- `scripts/windows_build_openvela.ps1` 默认 `stm32h750b-dk:lvgl` + kconfig-tweak，
  跑一次会污染 `.config`；`windows_flash_cube.ps1` 非 `-NoBuild` 时仍调用它。
- `docs/windows_build_debug_setup.md` 任务名与“桥接脚本已删除”的表述已过期；
  `scripts/wsl_gdb_launcher.sh` 与 `.vscode/settings.json` linux Cortex-Debug 路径仍在。
- 硬件不变：主镜像 QSPI `0x90000000`，boot stub `0x08000000`；CubeProgrammer +
  `MT25TL01G_STM32H750B-DISCO.stldr` 写 QSPI。
- NuttX `configure.sh -E` 会 `make distclean` 再从板级 defconfig 生成新的
  `nuttx/.config`（`nuttx/tools/configure.sh:207-209`）。参赛仓里的
  `velaguard-net` / `velaguard-min` 补丁文件不会被 distclean 删掉。
- 删除 ps1 后，QSPI/ETH 等 apply 脚本与 bootstub 构建不再有自动调用方；
  `build.sh` 必须接过这些职责，否则干净树或 nuttx 复位后 Build 会缺补丁/缺 stub。

## Requirements

- R1 四个一等动作，语义对标 Keil：
  - Build：增量 `make`，不烧录、不启动调试。
  - Rebuild：`configure.sh -E -e` 复位到默认 `velaguard-net` 后全量编译，不烧录。
    未 `savedefconfig` 回预设的本地 `nuttx/.config` / menuconfig 改动会被丢掉；
    参赛仓 defconfig 补丁保留。这是有意行为，用来从污染的 `.config` 恢复。
  - Download：只烧当前 `.debug` 产物（QSPI 主镜像 + 片内 stub + reset），不编译、不调试。
  - Debug（F5 默认）：只杀残留 OpenOCD 再 attach 当前板上固件，不编译、不烧录。
    另留 `openvela: Debug (Download first)`：先 `build.sh --debug`（`-g3` + `-Og`）
    再 Download 再 attach。普通 Build / Download 不带调试符号。
    不通过 OpenOCD / `loadFiles` 写 QSPI。
- R2 四个动作必须在 Remote - WSL Cursor 窗口一键完成。失败时给出可行动错误
  （缺 hex、ST-LINK 占用、Cube CLI 找不到）。禁止静默烧旧固件。
- R3 日常构建入口为 `scripts/build.sh`（由 `build_minimal.sh` 改名）。
  删除 `scripts/windows_build_openvela.ps1`，不留包装。Download 不得再走
  旧 ps1 的 lvgl+kconfig-tweak 管线。`--debug` 只切换 `DEBUG_SYMBOLS`/`-Og`。
- R4 `build.sh` 在 configure 前幂等执行该目标所需的 apply / link 脚本；
  缺少 `.debug/qspi_bootstub.hex` 时自动调用 `scripts/qspi_boot_stub/build_bootstub.sh`。
- R5 脚本命名：
  - `build_minimal.sh` → `build.sh`（不留旧名包装）
  - 新增 `flash.sh` 作为 WSL Download 入口（调 Windows CubeProgrammer）
  - `windows_flash_cube.ps1` 改名为短名并去掉“先构建”参数，只负责烧录/校验
- R6 文档与任务名对齐：操作文档只描述这一套入口。`docs/learn/` 历史博客可保留旧文件名，
  操作说明加一句现用名即可。
- R7 不改 contest 公共仓；改动限制在参赛仓 `scripts/`、`.vscode/`、`docs/`。
- R8 快捷键：不新增仓库级 `keybindings.json`。Build = Ctrl+Shift+B；Debug = F5；
  Rebuild / Download 走任务面板。Build / Rebuild **不弹** `net|min|lvgl` 选择器，
  固定默认 `velaguard-net`。

## Acceptance Criteria

- [ ] AC1 Remote - WSL 执行 Build：`scripts/build.sh` 增量编译成功，刷新
      `.debug/nuttx.{hex,bin,elf}`，不访问 ST-LINK。仓库日常入口不再指向
      `build_minimal.sh`。
- [ ] AC2 同一窗口执行 Rebuild：distclean 后按 **velaguard-net** 重新 configure 并全量编译；
      HEX 地址范围仍为 QSPI 主镜像 + 片内 stub。本地未保存的 `.config` 改动被预设覆盖。
      任务不弹出预设选择器。
- [ ] AC3 同一窗口执行 Download：只烧当前 `.debug` 产物并复位；缺文件则失败且不烧旧固件。
- [ ] AC4 F5 默认只 attach 当前固件，不 Download；可选配置
      `openvela: Debug (Download first)` 仍可先烧再 attach。
      不通过 OpenOCD/`loadFiles` 写 QSPI。
- [ ] AC5 仓库中不存在 `scripts/windows_build_openvela.ps1`；任务/Flash/操作文档
      日常入口不再引用它。
- [ ] AC6 `docs/windows_build_debug_setup.md` 与 `.vscode/tasks.json` / `launch.json`
      的任务名、窗口约定、脚本路径一致；写明 Rebuild 会复位到预设、F5 不自动编译。
- [ ] AC7 干净树或缺少 bootstub 时，Build 能自动补齐 stub 与该目标所需补丁，不必先跑已删 ps1。

## Out of Scope

- 改 Keil / STM32CubeIDE 工程本身。
- 用 OpenOCD 替代 CubeProgrammer 写双 MT25TL01G QSPI。
- 把 ST-LINK USB 转发进 WSL。
- 产品固件功能（Modbus / MQTT / UI）。
- 自动安装 CubeProgrammer / OpenOCD / gdb-multiarch。
- 为 `build_minimal.sh` 或 `windows_build_openvela.ps1` 保留兼容包装。
- F5 自动 Build；Debug halt-at-main（烧录后目标处于运行，attach 后手动暂停）。
- 仓库级按键绑定。
- 改写 `docs/learn/` 历史博客正文为新文件名。

## Key Decisions

1. 日常窗口 = Remote - WSL 单窗口。Download 经 WSL interop 调 Windows CubeProgrammer；
   Debug = WSL GDB + Windows OpenOCD。不转发 ST-LINK USB。
2. `build_minimal.sh` → `build.sh`，不留 `minimal` 品牌与旧名包装。
3. 删除 `windows_build_openvela.ps1`。
4. F5 默认 = 只 attach 当前固件（不编译不烧录）。需要符号固件与板上一致时，
   先 `build.sh --debug` + Download，或选 `openvela: Debug (Download first)`。
   普通 Build / Download 保持 FULLOPT、无 `-g`。调试开关不写进 defconfig。
5. Rebuild = distclean + 按所选预设重新 configure。丢掉的是未写回 defconfig 的本地
   `.config`；预设补丁是配置源，不会丢。
6. 不新增 `keybindings.json`。
7. Build / Rebuild / Download / F5 **不弹预设选择器**，一律 `velaguard-net`。
   `min` / `lvgl` 只留 `scripts/build.sh` 命令行。
