# Windows VS Code 写代码环境（补全 / 跳转）

本文只覆盖 **编辑 + 自动补全 + 函数跳转**。编译 / 烧录 / 断点调试的完整硬件链路见
[windows_build_debug_setup.md](./windows_build_debug_setup.md)。

日常窗口是 **Remote - WSL**。固件入口是本仓 `scripts/build.sh`，
**不是** 父目录 openvela 的 `./build.sh`。

## 正确架构（必须遵守）

```text
┌──────────────────────────────────────────────────────────────┐
│ Cursor / VS Code Remote - WSL（唯一推荐的日常窗口）           │
│                                                              │
│  编辑 / 补全 / 跳转  →  WSL 内 clangd                         │
│                        + Linux arm-none-eabi-gcc             │
│                        + NuttX/openvela 真实头文件           │
│                                                              │
│  编译                →  参赛仓 scripts/build.sh（WSL）        │
│  烧录                →  scripts/flash.sh → Windows Cube CLI  │
│  调试                →  WSL GDB + Windows OpenOCD            │
│                        request: attach（不写 QSPI）          │
└──────────────────────────────────────────────────────────────┘
```

| 职责 | 跑在哪 | 为什么 |
|------|--------|--------|
| 源码编辑、补全、跳转 | **WSL** | NuttX 大量 Linux 绝对路径符号链接；Windows 本地 C/C++ 扩展会跟丢，并混用 Windows newlib 与 NuttX libc |
| 固件编译 | **WSL** | 参赛仓 `scripts/build.sh` 调 Linux 工具链；勿与上游 `./build.sh` 混淆 |
| QSPI 烧录 | **Windows Cube**（WSL 调用） | STM32H750 超 128 KiB 片内 Flash 时，用 CubeProgrammer External Loader 写外部 QSPI；OpenOCD 不负责下载 |
| 断点调试 | **WSL GDB + Windows OpenOCD** | ST-LINK 由 Windows 访问；GDB 只 attach 符号，不再 load flash |

**不要**把下面的 UNC 路径当普通 Windows 文件夹长期写代码：

```text
\\wsl.localhost\Debian\home\<user>\openvela\contest2026_004_TeamFalcons
```

## 一次性准备

### Windows 侧

1. 安装 **WSL**（本仓默认发行版名 `Debian`，可用环境变量 `OPENVELA_WSL_DISTRO` 覆盖）。
2. 安装 **VS Code / Cursor**，并启用 “Add to PATH”。
3. 安装扩展：
   - `ms-vscode-remote.remote-wsl`
   - （进入 WSL 窗口后再装）clangd（本仓推荐）或 `ms-vscode.cpptools`
   - `marus25.cortex-debug`
4. openvela 整树放在 **WSL 文件系统**里，例如：
   `/home/<user>/openvela/`，本仓是其子目录 `contest2026_004_TeamFalcons/`。

### 打开正确窗口

任选其一：

```powershell
# 在 Windows PowerShell 中（日常用 Cursor）
cd \\wsl.localhost\Debian\home\<user>\openvela\contest2026_004_TeamFalcons
.\scripts\windows_open_cursor_wsl.ps1
# 若用 VS Code：.\scripts\windows_open_vscode_wsl.ps1
```

或在已经误开的 UNC 窗口里：`Ctrl+Shift+P` → `Tasks: Run Task` →

```text
openvela: reopen workspace in WSL for IntelliSense (Cursor)
```

左下角必须显示 **`WSL: Debian`**（或你的发行版名）。

### 生成 IntelliSense 头文件

NuttX 的 `include/arch` 符号链接和 `include/nuttx/config.h` 在 configure 之后才存在。
先在 Remote - WSL 跑一次 `openvela: Build`（`Ctrl+Shift+B`），或：

```bash
./scripts/prepare_wsl_intellisense.sh
# 可选：BOARD_CONFIG=stm32h750b-dk:lvgl FORCE_CONFIGURE=1 ./scripts/prepare_wsl_intellisense.sh
```

然后：

1. 扩展面板确认 **clangd**（或 C/C++）已安装到 **WSL**
2. clangd: Restart language server（若用 C/C++：Reset IntelliSense Database）
3. `Developer: Reload Window`

## 验证补全与跳转

打开 `app/hello_app/hello_app_main.c`：

1. 把光标放在 `printf` 上，按 `F12`（Go to Definition）应进入 NuttX/`stdio` 相关声明。
2. 输入 `prin` 应出现补全。

若红色波浪很多且无法跳转，按顺序检查：

1. 是否 Remote - WSL（不是 UNC / 本地 Windows 文件夹）
2. 是否已跑 `prepare_wsl_intellisense.sh` 或一次 Build，且存在：
   - `../nuttx/include/nuttx/config.h`
   - `../nuttx/include/arch` → `../nuttx/arch/arm/include`
3. `../prebuilts/gcc/linux-x86_64/arm-none-eabi/bin/arm-none-eabi-gcc` 是否可执行
4. 是否重启过 clangd / Reload Window

## 与编译 / 烧录 / 调试的衔接

`Ctrl+Shift+B` 是 `openvela: Build`：增量编译并刷新 `.debug`，**不烧板**。
F5 是 `openvela: Debug`：只 attach 当前板上固件，**不编译、不烧录**。
完整语义见 [windows_build_debug_setup.md](./windows_build_debug_setup.md)。

| Task / Launch | 作用 |
|------|------|
| `openvela: Build` | `scripts/build.sh` 增量编译 |
| `openvela: Rebuild` | distclean 后按所选预设全量编译（会丢掉未保存的 `.config`） |
| `openvela: Download` | 只烧当前 `.debug` 产物 |
| `openvela: Build & Download` | 先 Build 再 Download |
| F5 `openvela: Debug` | 只 attach 当前固件，不烧录 |
| `openvela: Debug (Download first)` | 编调试固件 → Download → attach |

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
.vscode/c_cpp_properties.json   # 补全 / 跳转
.vscode/extensions.json         # 推荐扩展
.vscode/settings.json           # clangd + Cortex-Debug 工具路径
.vscode/tasks.json              # Build / Rebuild / Download
.vscode/launch.json             # Debug（默认只 attach）+ Debug (Download first)
scripts/prepare_wsl_intellisense.sh
scripts/windows_open_cursor_wsl.ps1
scripts/windows_open_vscode_wsl.ps1
scripts/build.sh                # 日常构建（不是上游 ./build.sh）
scripts/flash.sh                # WSL Download 入口
scripts/flash.ps1               # Cube CLI + HEX 校验
```
