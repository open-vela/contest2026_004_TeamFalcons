# Windows VS Code 编译、烧录与调试 STM32H750B-DK

> **写代码（补全 / 跳转）请先看**
> [windows_vscode_coding_setup.md](./windows_vscode_coding_setup.md)。
> 本文侧重编译、QSPI 烧录与 Cortex-Debug。日常窗口是 **Remote - WSL**。
>
> 本仓入口是 `contest2026_004_TeamFalcons/scripts/build.sh`，
> **不是** 父目录 openvela 的 `./build.sh`。

## 方案边界

STM32H750XBH6 只有 128 KiB 片内 Flash。超过该容量的 openvela 镜像采用：

```text
0x08000000  片内 Flash：QSPI boot stub
0x90000000  外部 QSPI：NuttX/openvela XIP 主镜像
```

本项目固定以下职责，避免再次把不存在的片内空间声明给 OpenOCD：

- WSL：用 Linux 工具链编译；参赛仓 `scripts/build.sh` 是唯一日常构建入口。
- STM32CubeProgrammer：使用
  `MT25TL01G_STM32H750B-DISCO.stldr` 写入外部 QSPI，再写入片内 boot
  stub。Remote - WSL 窗口通过 `scripts/flash.sh` 调 Windows Cube CLI。
- OpenOCD：只作为 Cortex-Debug 的 GDB server，不负责 QSPI 下载。
  GDB 是 WSL 内的 `gdb-multiarch`（`scripts/wsl_gdb_launcher.sh`）；
  OpenOCD 仍是 Windows 进程。

历史提交 `5eb5070` 已在本板验证：xPack OpenOCD 的 `stmqspi` 无法稳定
JEDEC probe/write 双 MT25TL01G，而上述 CubeProgrammer External Loader 可以
正常擦写和校验。

## 1. 环境

在 Windows 安装：

- VS Code / Cursor，以及 `ms-vscode-remote.remote-wsl`、
  `marus25.cortex-debug` 扩展（补全走 clangd，见编码文档）；
- WSL，完整 openvela 工作区位于 WSL 文件系统；
- STM32CubeCLT/STM32CubeProgrammer；
- Windows xPack OpenOCD。

开发板通过 ST-LINK USB 连接到 Windows。即使源码和编译环境位于 WSL，
CubeProgrammer、OpenOCD 和 ST-LINK 都由 Windows 进程访问，不需要把 USB
设备转发给 WSL。

源码编辑、自动补全、函数跳转、Build / Rebuild / Download / Debug 都在
**Remote - WSL** 窗口中进行。不要把下面的 UNC 路径当作普通 Windows
文件夹长期开发：

```text
\\wsl.localhost\Debian\home\<user>\openvela\contest2026_004_TeamFalcons
```

若当前已经打开 UNC 窗口，按 `Ctrl+Shift+P` 运行 `Tasks: Run Task`，选择：

```text
openvela: reopen workspace in WSL for IntelliSense (Cursor)
```

新窗口左下角必须显示 `WSL: Debian`。

PowerShell / 烧录脚本也支持环境变量：

| 变量 | 用途 |
|---|---|
| `OPENVELA_WSL_DISTRO` | WSL 发行版，默认 `Debian` |
| `OPENVELA_ROOT_WSL` | WSL 中的 openvela 根目录；默认从参赛仓父目录推导 |
| `OPENVELA_OUT_DIR` | Windows 产物目录，默认参赛仓 `.debug` |
| `STM32_PROGRAMMER_CLI` | `STM32_Programmer_CLI.exe` 完整路径 |
| `STM32_EXTERNAL_LOADER` | `MT25TL01G_STM32H750B-DISCO.stldr` 完整路径 |

Windows 工具路径位于 `.vscode/settings.json`；安装位置不同时只需修改该文件。

单窗口调试需要 WSL2 **mirrored** 网络（Linux GDB 才能连 Windows OpenOCD
的 `localhost:3333`）。一次性启用：

1. 确认 `C:\Users\<you>\.wslconfig` 含：
   ```ini
   [wsl2]
   networkingMode=mirrored
   ```
2. 在 Windows PowerShell 执行 `wsl --shutdown`，再打开 Remote - WSL 窗口。

## 2. 四个一等动作（对标 Keil）

全部在 Remote - WSL 窗口完成。Build 应对应 `Ctrl+Shift+B`（默认构建任务
`openvela: Build`），Debug = `F5`；Rebuild / Download 走任务面板。

Cursor 2.x 会把 `Ctrl+Shift+B` 绑到内置 Browser，盖掉 VS Code 的 Run Build Task。
若按下去打开的是 Browser 而不是编译：`Ctrl+Shift+P` → `Preferences: Open Keyboard
Shortcuts`，搜索 `ctrl+shift+b`，删掉 Browser 那一行，只保留
`Run Build Task` / `workbench.action.tasks.build`。

| Keil | 任务 | 脚本 | 做什么 |
|------|------|------|--------|
| Build | `openvela: Build`（默认） | `scripts/build.sh` | 增量 `make`（velaguard-net），刷新 `.debug/nuttx.{hex,bin,elf}`，不烧录 |
| Rebuild | `openvela: Rebuild` | `scripts/build.sh --clean` | distclean 后复位到 velaguard-net 并全量编译，不烧录 |
| Download | `openvela: Download` | `scripts/flash.sh` | 只烧当前 `.debug` 产物并复位，不编译 |
| Debug | F5 `openvela: Debug` | 只杀 OpenOCD 再 attach | 基于当前板上固件，不编译、不烧录 |
| （可选） | `openvela: Debug (Download first)` | `build.sh --debug` → Download → attach | 需要先刷带 `-g3/-Og` 的符号固件时用 |
| （可选） | `openvela: Build & Download` | Build 成功后再 Download | 改代码后一键编译+烧录 |

Build / Rebuild / Download / F5 **不再弹预设选择**，一律 `velaguard-net`。
Download 固定烧 `.debug/nuttx.hex` + `.debug/qspi_bootstub.hex`。
若偶尔需要 `min` / `lvgl`，只走命令行，不要改任务按钮。

命令行等价：

```bash
# 在参赛仓根目录（contest2026_004_TeamFalcons/）
bash scripts/build.sh               # 增量 velaguard-net 发布构建（按钮等同这条）
bash scripts/build.sh --debug       # 带 -g3/-Og 符号；F5 不自动跑这条
bash scripts/build.sh --clean       # Rebuild 复位到 velaguard-net 发布配置
bash scripts/flash.sh               # Download
bash scripts/flash.sh -ValidateOnly # 只校验 HEX 地址，不碰硬件
# 非日常：bash scripts/build.sh min|lvgl [--clean]
```

### Rebuild 会丢掉什么

`configure.sh -E` 会 `make distclean`，再从板级 defconfig 生成新的
`nuttx/.config`。未 `savedefconfig` 回预设的本地 menuconfig / kconfig
改动会被丢掉。参赛仓里的 `velaguard-net` / `velaguard-min` 补丁文件
不会被 distclean 删除。这是有意行为：用来从被污染的 `.config` 恢复。
若改动要长期保留，应写进对应 defconfig 补丁。

### 构建脚本会做什么

`scripts/build.sh` 在 configure 前幂等补齐 velaguard-net 所需内容（干净树不必先跑
其它脚本）：

1. `ensure-openvela-links.sh`，并重生 `packages/demos/Kconfig`；
2. apply：QSPI + ETH-MII + velaguard-net defconfig；
3. 缺少 `.debug/qspi_bootstub.hex` 或 `--clean` 时构建 boot stub；
4. 清空 `.debug` 里的旧主镜像，避免失败后 Download 烧到旧固件；
5. 形态不匹配或 `--clean` 时 `configure.sh -E -e stm32h750b-dk:<preset>`；
6. `make -j`，把 `nuttx.{hex,bin,elf}` 拷到 `.debug/`。

判断编译正确不要只看任务退出码，还应确认 `.debug/nuttx.hex` 时间戳已更新，
且 Download 日志显示主镜像位于 `0x9000xxxx`、boot stub 位于 `0x0800xxxx`。

## 3. 下载

运行任务 `openvela: Download`，或 `bash scripts/flash.sh`。

脚本在连接硬件前检查：

- `.debug/nuttx.hex` 与 `.debug/qspi_bootstub.hex` 存在（缺文件则失败，
  **不会**静默烧旧固件）；
- 主 HEX 全部位于 `0x90000000..0x97ffffff`；
- boot stub 全部位于 `0x08000000..0x0801ffff`；
- CubeProgrammer CLI 与 External Loader 存在。

随后依次执行：

```text
Cube + External Loader → 写入并校验 QSPI 主镜像
Cube                   → 写入并校验片内 boot stub
Cube                   → 复位
```

没有连接开发板时，可只验证工具和镜像布局：

```bash
bash scripts/flash.sh -ValidateOnly
```

失败时应能直接行动：缺 hex 先 Build；找不到 `powershell.exe` 检查 WSL
interop；找不到 Cube CLI 安装 STM32CubeProgrammer 或设置
`STM32_PROGRAMMER_CLI`；ST-LINK 被占用则关掉 Cube GUI / 其它 OpenOCD。

## 4. 断点调试

F5 默认配置是 `openvela: Debug`（原先的 Attach only）：

1. 杀掉占用 ST-LINK 的 OpenOCD；
2. Cortex-Debug attach 当前板上固件。配置使用 `request: attach`（不是 launch），
   GDB 加载 `${workspaceFolder}/.debug/nuttx.elf` 的符号，**不会**编译或写 Flash。
   不要设置 `loadFiles: []`：Cortex-Debug 会因此跳过加载 ELF，Debug Console
   出现 `No symbol table is loaded`，断点一直空心。

板上代码必须与当前 `.debug/nuttx.elf` 一致，否则断点会偏。改过代码后应先
`openvela: Build Debug` / `scripts/build.sh --debug` 再 Download，或从调试下拉框
选 `openvela: Debug (Download first)`。

`Ctrl+Shift+B` / Download 不带调试符号。

主镜像在 QSPI XIP（`0x90000000`），不能写软件断点。launch 会强制硬件断点
（Cortex-M7 最多 8 个）。断点应变成实心红点；若仍是灰色空心，先点暂停再下断点，
并确认悬停文案不是 “unverified”。推荐打在 `velaguard.c` 主循环 `usleep` /
`board_userled` 行，不要打在已被优化掉的空语句上。

推荐的日常操作顺序：

1. 需要新固件时：`Ctrl+Shift+B` 或 `openvela: Build Debug`，再任务 `openvela: Download`。
2. 只要跑起来：任务 `openvela: Download`（或 `openvela: Build & Download`）。
3. 要源码断点：F5 `openvela: Debug`（只 attach 当前固件）。
4. 改过代码且需要连编译带烧录再调试：选 `openvela: Debug (Download first)`。

不要用 VS Code 或 GDB 的 Download/Load 命令。当前配置是 `request: attach`，
OpenOCD 只 halt，不会写 QSPI。OpenOCD 与 Cube 不能同时占 ST-LINK，所以
F5 会先杀残留 OpenOCD 再 attach。

## 5. VelaGuard 运行验证

ST-LINK 虚拟串口使用 `115200 8N1`。烧录后，VelaGuard 工业首页会自动显示，
不需要运行 openvela 的 `lvgldemo`。界面显示 Device ID、test/production
模式、固件版本、存储启动状态，以及 Acquisition、Alarm、Network、Audio、
Time 五类状态占位。

在 [velaguard.c](../app/velaguard/velaguard.c)（当前为模板应用，VelaGuard
UI 代码后续将放在本目录）中设置断点，按 `F5` attach。应用主循环每秒都会
运行，断点会稳定命中，可检查模块级状态变量。

NSH 仍在实际枚举的 ST-LINK COM 口（本机当前为 `COM7`）以 `115200 8N1`
可用。触摸初始化成功时串口会出现 `/dev/input0 open success, maxpoint 1`，
应用启动成功后还会输出 `[velaguard] UI ready`。ISSUE1 首页没有虚构的交互
按钮，触摸仅作为平台连续性验证。

## 6. 故障恢复

- `Main QSPI image address range is invalid`：构建未启用 QSPI linker，禁止下载。
- 找不到 External Loader：检查 STM32CubeProgrammer 安装，或设置
  `STM32_EXTERNAL_LOADER`。
- 找不到 Cube CLI：安装 STM32CubeProgrammer，或设置 `STM32_PROGRAMMER_CLI`。
- `powershell.exe` 未找到：Download 必须从 WSL 调 Windows；检查 WSL interop。
- 缺 `.debug/nuttx.hex`：先 Build；Download 不会烧旧文件。
- OpenOCD: GDB Server Quit Unexpectedly / `couldn't bind tcl to socket on port 50001`：
  Windows Hyper-V 排除了 TCP 50000–50059，而 Cortex-Debug 默认从 50000 起分配端口。
  仓库用 `scripts/wsl_openocd_launcher.sh` 把 GDB 端口中继到 3333。确认
  `cortex-debug.openocdPath.linux` 指向该脚本，且 WSL2 `networkingMode=mirrored`。
- OpenOCD 无法 attach：关闭 CubeProgrammer GUI 和其他 OpenOCD 进程，确认
  ST-LINK 未被占用。
- 断点一直是灰色空心圆圈：先看 Debug Console 有没有 `No symbol table is loaded`。
  若有，确认 `launch.json` **没有** `loadFiles: []`，且 `executable` 指向
  `.debug/nuttx.elf`。停调试再 F5。源码断点走硬件比较器，不要超过 8 个。
- QSPI 下载成功但不启动：先检查 boot stub 是否写入片内 Flash，再检查复位后
  PC 是否进入 `0x9000xxxx`。若停在 `0x080002aa`，检查 boot stub 是否错误要求
  NuttX 初始 MSP 8 字节对齐；当前实现只要求合法 SRAM 范围和 4 字节对齐。
- 屏幕只有背光：查看串口是否出现 `[velaguard] UI ready`；若没有，检查更早的
  板级/LVGL 初始化错误，确认烧录的是刚生成的 `.debug` 产物。
- LVGL 报 `get touch maxpoints failed (errno=25)`：当前 NuttX checkout 未应用
  团队补丁，重新执行 `openvela: Build` / Rebuild，不要只烧录旧 `.debug` 产物。
- 能显示但触摸无效：先看串口是否出现 `/dev/input0 open success`，再检查触摸
  读事件；若点击位置方向错误，再核对 `CONFIG_FT5X06_SWAPXY` 和屏幕旋转配置。
- GDB 中 `lv_nuttx_init()` 显示 `disp=NULL` 但 `indev!=NULL`：不要硬编码
  `/dev/lcd0`；当前 framebuffer 配置应保留 `lv_nuttx_dsc_init()` 选择的
  `/dev/fb0`。
- 芯片保护或连接异常：在 CubeProgrammer GUI 中检查 Option Bytes；记录原值后
  再处理。正常流程不会自动修改 Option Bytes 或执行 mass erase。
