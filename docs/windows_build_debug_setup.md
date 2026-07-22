# Windows VS Code 编译、烧录与调试 STM32H750B-DK

## 方案边界

STM32H750XBH6 只有 128 KiB 片内 Flash。超过该容量的 openvela 镜像采用：

```text
0x08000000  片内 Flash：QSPI boot stub
0x90000000  外部 QSPI：NuttX/openvela XIP 主镜像
```

本项目固定以下职责，避免再次把不存在的片内空间声明给 OpenOCD：

- WSL：编译 Linux 工具链下的 openvela。
- STM32CubeProgrammer：使用
  `MT25TL01G_STM32H750B-DISCO.stldr` 写入外部 QSPI，再写入片内 boot
  stub。
- OpenOCD：只作为 Cortex-Debug 的 GDB server，不负责 QSPI 下载。

历史提交 `5eb5070` 已在本板验证：xPack OpenOCD 的 `stmqspi` 无法稳定
JEDEC probe/write 双 MT25TL01G，而上述 CubeProgrammer External Loader 可以
正常擦写和校验。

## 1. 环境

在 Windows 安装：

- VS Code，以及 `ms-vscode-remote.remote-wsl`、`ms-vscode.cpptools`、
  `marus25.cortex-debug` 扩展；
- WSL，完整 openvela 工作区位于 WSL 文件系统；
- STM32CubeCLT/STM32CubeProgrammer；
- Windows xPack OpenOCD。

开发板通过 ST-LINK USB 连接到 Windows。即使源码和编译环境位于 WSL，
CubeProgrammer、OpenOCD 和 ST-LINK 都由 Windows 进程访问，不需要把 USB
设备转发给 WSL。

源码编辑、自动补全和函数跳转必须在 **Remote - WSL** 窗口中进行。不要把下面
的 UNC 路径当作普通 Windows 文件夹长期开发：

```text
\\wsl.localhost\Debian\home\<user>\openvela\contest2026_004_TeamFalcons
```

Windows 本地 C/C++ 扩展无法正确跟随 NuttX 的 Linux 绝对符号链接，还会把
Windows newlib 与 NuttX libc 混用，表现为 `velaguard_main.c` 没有可靠补全、
不能跳转或出现大量错误类型提示。若当前已经打开 UNC 窗口，按
`Ctrl+Shift+P` 运行 `Tasks: Run Task`，选择：

```text
openvela: reopen workspace in WSL for IntelliSense
```

新窗口左下角必须显示 `WSL: Debian`。首次进入时按扩展面板提示，把
`ms-vscode.cpptools` 安装到 WSL；然后运行 `C/C++: Reset IntelliSense
Database` 和 `Developer: Reload Window`。项目配置会使用 WSL 内的真实 ARM
GCC、NuttX 头文件与 Linux 符号链接。

当前硬件任务仍由 Windows 进程访问 CubeProgrammer、OpenOCD 与 ST-LINK。
因此编辑/跳转使用 Remote - WSL 窗口；执行现有编译、烧录和 Cortex-Debug
任务时保留原 Windows UNC 窗口。不要把 ST-LINK 转发到 WSL，也不要在尚未
配置 Windows 工具桥接的 Remote 窗口直接启动 Cortex-Debug。Windows 工具路径
位于 `.vscode/settings.json`；安装位置不同时只需修改该文件。

PowerShell 脚本也支持环境变量：

| 变量 | 用途 |
|---|---|
| `OPENVELA_WSL_DISTRO` | WSL 发行版，默认 `Debian` |
| `OPENVELA_ROOT_WSL` | WSL 中的 openvela 根目录；默认从参赛仓父目录推导 |
| `OPENVELA_OUT_DIR` | Windows 产物目录，默认参赛仓 `.debug` |
| `STM32_PROGRAMMER_CLI` | `STM32_Programmer_CLI.exe` 完整路径 |
| `STM32_EXTERNAL_LOADER` | `MT25TL01G_STM32H750B-DISCO.stldr` 完整路径 |

## 2. 编译

按 `Ctrl+Shift+B`，运行默认测试版本：

```text
openvela: build VelaGuard test QSPI firmware
```

构建脚本会：

1. 将团队仓内的 QSPI 补丁幂等应用到当前 NuttX checkout；
2. 维护 manifest 声明的 VelaGuard 生成态 linkfile；
3. 以 `stm32h750b-dk:lvgl` 作为板级驱动基线，但禁用官方
   `lvgldemo` 和开发期 VS Code Lab；
4. 启用 `CONFIG_STM32H750B_DK_QSPI_BOOT`；
5. 启用项目自有 VelaGuard 并将 `velaguard_main` 设置为启动入口；
6. 构建主镜像和内部 boot stub；
7. 复制以下文件到参赛仓 `.debug`：

```text
nuttx.elf  nuttx.hex  nuttx.bin
qspi_bootstub.elf  qspi_bootstub.hex  qspi_bootstub.bin
nuttx.config  build-info.txt
```

调试构建使用任务 `openvela: build VelaGuard test debug QSPI firmware`，它额外
启用 `-g3` 和 `-Og` 调试优化构建。生产身份验证使用
`openvela: build VelaGuard production QSPI firmware`。产品模式
test/production 与编译模式 debug/release 相互独立。

### 增量 vs 全量

默认 **incremental**（日常改 app 后 flash 应走这条）：

- 已有 `nuttx/.config` 时 **跳过** `configure.sh`；
- 仅在 kconfig-tweak 后 `.config` **真的变化** 时才 `make clean`；
- 否则直接 `make -j`，只重编改动的目标。

需要全量时用 `-Rebuild full` 或任务
`openvela: FULL clean+build production QSPI firmware` /
`openvela: FULL clean+flash production (CubeProgrammer)`（会重新
`configure.sh` 并强制 `make clean`）。

```powershell
# 默认增量
scripts\windows_flash_cube.ps1 -VelaGuardMode production

# 强制全量
scripts\windows_flash_cube.ps1 -VelaGuardMode production -Rebuild full
# 或
scripts\windows_build_openvela.ps1 -VelaGuardMode production -FullClean
```

从 test 切到 production（或改 debug 符号）时，`.config` 会变，**仍会 clean 一次**，属正常。

判断编译正确不要只看任务退出码，还应确认 `.debug` 中六个产物都存在，且
日志显示主镜像位于 `0x9000xxxx`、boot stub 位于 `0x0800xxxx`。

## 3. 下载

运行：

```text
openvela: flash VelaGuard test QSPI firmware (CubeProgrammer)
```

脚本在连接硬件前检查：

- 主 HEX 全部位于 `0x90000000..0x97ffffff`；
- boot stub 全部位于 `0x08000000..0x0801ffff`；
- CubeProgrammer CLI 与 External Loader 存在。

随后依次执行：

```text
Cube + External Loader → 写入并校验 QSPI 主镜像
Cube                   → 写入并校验片内 boot stub
Cube                   → 复位
```

已有产物时可使用 `openvela: flash current artifacts only
(CubeProgrammer)`。生产版本使用 `openvela: flash VelaGuard production
firmware (CubeProgrammer)`。

没有连接开发板时，可在 Windows PowerShell 只验证工具和镜像布局：

```powershell
scripts\windows_flash_cube.ps1 -NoBuild -ValidateOnly
```

## 4. 断点调试

选择：

```text
openvela: VelaGuard test debug after Cube flash
```

按 `F5` 后会先构建并用 CubeProgrammer 下载 debug 镜像，再由 OpenOCD
启动 GDB server。配置使用 `request: attach` 和空 `loadFiles`，因此 GDB 只
加载 `${workspaceFolder}\.debug\nuttx.elf` 的符号，不会再次写 Flash。

若固件已经下载，使用 `openvela: VelaGuard attach only`。

推荐的日常操作顺序是：

1. 普通运行验证：执行 `openvela: flash VelaGuard test QSPI firmware
   (CubeProgrammer)`，它会自动编译、烧录并复位。
2. 调试代码：在“运行和调试”中选择 `openvela: VelaGuard test debug
   after Cube flash` 后按 `F5`；该配置会先生成并烧录 debug 固件，再 attach。
3. 只增加或移动断点、不改固件：选择 `attach only`，避免重复擦写 QSPI。
4. 改过代码后不要直接使用 `attach only`，否则板上代码与 ELF 符号可能不一致。

连接后若程序正在运行，可先暂停，再在应用源码中下断点并继续。不要用 VS Code
或 GDB 的 Download/Load 命令；本项目的 `loadFiles: []` 正是为了阻止这条错误
路径。

## 5. VelaGuard 运行验证

ST-LINK 虚拟串口使用 `115200 8N1`。烧录后，VelaGuard 工业首页会自动显示，
不需要运行 openvela 的 `lvgldemo`。界面显示 Device ID、test/production
模式、固件版本、存储启动状态，以及 Acquisition、Alarm、Network、Audio、
Time 五类状态占位。

在 [vg_ui_home.c](../app/velaguard_app/src/vg_ui_home.c) 的
`vg_ui_home_uptime_checkpoint()` 中设置断点，按 `F5` attach。每秒 uptime
刷新会命中该稳定断点，可检查 `g_velaguard_uptime_seconds`。

NSH 仍在实际枚举的 ST-LINK COM 口（本机当前为 `COM7`）以 `115200 8N1`
可用。触摸初始化成功时串口会出现 `/dev/input0 open success, maxpoint 1`，
应用启动成功后还会输出 `[velaguard] UI ready`。ISSUE1 首页没有虚构的交互
按钮，触摸仅作为平台连续性验证。

## 6. 故障恢复

- `Main QSPI image address range is invalid`：构建未启用 QSPI linker，禁止下载。
- 找不到 External Loader：检查 STM32CubeProgrammer 安装，或设置
  `STM32_EXTERNAL_LOADER`。
- OpenOCD 无法 attach：关闭 CubeProgrammer GUI 和其他 OpenOCD 进程，确认
  ST-LINK 未被占用。
- QSPI 下载成功但不启动：先检查 boot stub 是否写入片内 Flash，再检查复位后
  PC 是否进入 `0x9000xxxx`。若停在 `0x080002aa`，检查 boot stub 是否错误要求
  NuttX 初始 MSP 8 字节对齐；当前实现只要求合法 SRAM 范围和 4 字节对齐。
- 屏幕只有背光：查看串口是否出现 `[velaguard] UI ready`；若没有，检查更早的
  板级/LVGL 初始化错误，确认烧录的是刚生成的 `.debug` 产物。
- LVGL 报 `get touch maxpoints failed (errno=25)`：当前 NuttX checkout 未应用
  团队补丁，重新执行项目构建任务，不要只烧录旧 `.debug` 产物。
- 能显示但触摸无效：先看串口是否出现 `/dev/input0 open success`，再检查触摸
  读事件；若点击位置方向错误，再核对 `CONFIG_FT5X06_SWAPXY` 和屏幕旋转配置。
- GDB 中 `lv_nuttx_init()` 显示 `disp=NULL` 但 `indev!=NULL`：不要硬编码
  `/dev/lcd0`；当前 framebuffer 配置应保留 `lv_nuttx_dsc_init()` 选择的
  `/dev/fb0`。
- 芯片保护或连接异常：在 CubeProgrammer GUI 中检查 Option Bytes；记录原值后
  再处理。正常流程不会自动修改 Option Bytes 或执行 mass erase。
