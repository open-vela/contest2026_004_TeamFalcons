# Windows VS Code 写代码环境（补全 / 跳转）

本文只覆盖 **编辑 + 自动补全 + 函数跳转**。编译 / 烧录 / 断点调试的完整硬件链路见
[windows_build_debug_setup.md](./windows_build_debug_setup.md)；任务入口已在
`.vscode/tasks.json` / `launch.json` 预留为 scaffold。

## 正确架构（必须遵守）

```text
┌──────────────────────────────────────────────────────────────┐
│ VS Code Remote - WSL（唯一推荐的写代码窗口）                  │
│                                                              │
│  编辑 / 补全 / 跳转  →  WSL 内 ms-vscode.cpptools            │
│                        + Linux arm-none-eabi-gcc             │
│                        + NuttX/openvela 真实头文件           │
│                                                              │
│  编译（后续）        →  仍在 WSL 跑 openvela 工具链          │
│  烧录（后续）        →  Windows CubeProgrammer + ST-LINK     │
│  调试（后续）        →  Windows OpenOCD + Cortex-Debug       │
│                        request: attach, loadFiles: []        │
└──────────────────────────────────────────────────────────────┘
```

| 职责 | 跑在哪 | 为什么 |
|------|--------|--------|
| 源码编辑、补全、跳转 | **WSL** | NuttX 大量 Linux 绝对路径符号链接；Windows 本地 C/C++ 扩展会跟丢，并混用 Windows newlib 与 NuttX libc |
| 固件编译 | **WSL** | openvela 官方工具链与 `build.sh` 是 Linux 路径 |
| QSPI 烧录 | **Windows** | STM32H750 超 128 KiB 片内 Flash 时，用 CubeProgrammer External Loader 写外部 QSPI；OpenOCD 不负责下载 |
| 断点调试 | **Windows OpenOCD** | ST-LINK 由 Windows 访问；GDB 只 attach 符号，不再 load flash |

**不要**把下面的 UNC 路径当普通 Windows 文件夹长期写代码：

```text
\\wsl.localhost\Debian\home\<user>\openvela\contest2026_004_TeamFalcons
```

## 一次性准备

### Windows 侧

1. 安装 **WSL**（本仓默认发行版名 `Debian`，可用环境变量 `OPENVELA_WSL_DISTRO` 覆盖）。
2. 安装 **VS Code**，并启用 “Add to PATH”。
3. 安装扩展：
   - `ms-vscode-remote.remote-wsl`
   - （进入 WSL 窗口后再装）`ms-vscode.cpptools`
   - （后续调试再装）`marus25.cortex-debug`
4. openvela 整树放在 **WSL 文件系统**里，例如：
   `/home/<user>/openvela/`，本仓是其子目录 `contest2026_004_TeamFalcons/`。

### 打开正确窗口

任选其一：

```powershell
# 在 Windows PowerShell 中
cd \\wsl.localhost\Debian\home\<user>\openvela\contest2026_004_TeamFalcons
.\scripts\windows_open_vscode_wsl.ps1
```

或在已经误开的 UNC 窗口里：`Ctrl+Shift+P` → `Tasks: Run Task` →

```text
openvela: reopen workspace in WSL for IntelliSense
```

左下角必须显示 **`WSL: Debian`**（或你的发行版名）。

### 生成 IntelliSense 头文件

NuttX 的 `include/arch` 符号链接和 `include/nuttx/config.h` 在 configure 之后才存在。
在 Remote - WSL 窗口执行默认构建任务（`Ctrl+Shift+B`）：

```text
openvela: prepare IntelliSense headers (WSL)
```

等价命令：

```bash
./scripts/prepare_wsl_intellisense.sh
# 可选：BOARD_CONFIG=stm32h750b-dk:lvgl FORCE_CONFIGURE=1 ./scripts/prepare_wsl_intellisense.sh
```

然后：

1. 扩展面板确认 **C/C++** 已安装到 **WSL**
2. `C/C++: Reset IntelliSense Database`
3. `Developer: Reload Window`

## 验证补全与跳转

打开 `app/hello_app/hello_app_main.c`：

1. 把光标放在 `printf` 上，按 `F12`（Go to Definition）应进入 NuttX/`stdio` 相关声明。
2. 输入 `prin` 应出现补全。
3. 状态栏 C/C++ 配置名应为 **`openvela STM32H750 (WSL)`**。

若红色波浪很多且无法跳转，按顺序检查：

1. 是否 Remote - WSL（不是 UNC / 本地 Windows 文件夹）
2. 是否已跑 `prepare_wsl_intellisense.sh`，且存在：
   - `../nuttx/include/nuttx/config.h`
   - `../nuttx/include/arch` → `../nuttx/arch/arm/include`
3. `../prebuilts/gcc/linux-x86_64/arm-none-eabi/bin/arm-none-eabi-gcc` 是否可执行
4. 是否 Reset 过 IntelliSense Database

## 与后续编译 / 烧录的衔接

当前默认 `Ctrl+Shift+B` **只准备 IntelliSense**，不烧板。

已预留的 scaffold 任务（脚本在、产品 app 选择后续再对齐）：

| Task | 作用 |
|------|------|
| `openvela: build firmware via Windows host script (scaffold)` | 经 PowerShell 调 WSL 编译，产物进 `.debug/` |
| `openvela: flash firmware via CubeProgrammer (scaffold)` | Windows CubeProgrammer 写 QSPI + boot stub |
| `openvela: flash current artifacts only (scaffold)` | 只烧已有 `.debug` 产物 |
| Debug: `openvela: attach only (scaffold)` | Cortex-Debug attach，`loadFiles: []` |

硬件工具路径在 `.vscode/settings.json` 的 `cortex-debug.*` 项；本机安装位置不同时只改该文件，或设：

| 环境变量 | 用途 |
|----------|------|
| `OPENVELA_WSL_DISTRO` | WSL 发行版，默认 `Debian` |
| `OPENVELA_ROOT_WSL` | openvela 根目录 |
| `OPENVELA_OUT_DIR` | 产物目录，默认仓内 `.debug` |
| `STM32_PROGRAMMER_CLI` | CubeProgrammer CLI |
| `STM32_EXTERNAL_LOADER` | `MT25TL01G_STM32H750B-DISCO.stldr` |

## 仓库内相关文件

```text
.vscode/c_cpp_properties.json   # 补全 / 跳转（本环境核心）
.vscode/extensions.json         # 推荐扩展
.vscode/settings.json           # C/C++ + 后续调试工具路径
.vscode/tasks.json              # prepare + 后续 build/flash scaffold
.vscode/launch.json             # 后续 attach scaffold
scripts/prepare_wsl_intellisense.sh
scripts/windows_open_vscode_wsl.ps1
scripts/windows_build_openvela.ps1      # 后续
scripts/windows_flash_cube.ps1          # 后续
```
