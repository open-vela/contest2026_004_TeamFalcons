# Rebuild distclean 与 WSL Download

## distclean 丢什么、不丢什么

`scripts/build_minimal.sh --clean` 调用 `nuttx/tools/configure.sh -E -e stm32h750b-dk:<preset>`。
`-E` 在已有 `.config` 时执行 `make distclean`，再从板级 `defconfig` 生成新的 `nuttx/.config`。

会丢掉：

- 当前 `nuttx/.config` 以及只存在于这份文件里的 menuconfig / kconfig-tweak 改动
- NuttX 构建产物（`.o`、依赖文件等）

不会丢掉：

- 参赛仓 `scripts/openvela-velaguard-*-defconfig.patch` 以及已 apply 到 nuttx 树的
  板级 `defconfig` 源文件（distclean 不 checkout 源码）
- 参赛仓 `.debug/qspi_bootstub.hex`（在 contest 树，不在 nuttx distclean 范围）
- 应用源码

因此 Rebuild 的产品语义是：**复位到所选预设再全编**，不是“保留我刚在 menuconfig 里勾的开关”。
若改动要长期保留，应进对应 defconfig 补丁，而不是依赖 `nuttx/.config`。

## 删除 ps1 后的职责空洞

`windows_build_openvela.ps1` 每次构建会：

- `ensure-openvela-links.sh`
- 若干 `apply-openvela-*.sh`（QSPI / ETH / UI / display）
- 调用 `qspi_boot_stub/build_bootstub.sh`

`build_minimal.sh` 目前假设这些已经做过，缺 stub 就报错退出。
删除 ps1 后，`build.sh` 必须按目标幂等补齐，否则干净树无法一键 Build。

建议按目标调用（均为现有幂等脚本，不要新写补丁逻辑）：

| 目标 | configure 前 |
|------|----------------|
| net | links；velaguard-net defconfig；eth-mii；qspi |
| min | links；velaguard-min defconfig；pwm-tim15；qspi |
| lvgl | links；qspi；ui-performance；display-acceleration |

bootstub：缺失或 Rebuild 时调用 `build_bootstub.sh`，输出到 `.debug/`。

## WSL 窗口 Download

CubeProgrammer CLI 与 External Loader 是 Windows 程序，ST-LINK 由 Windows 访问。
Remote - WSL 任务应执行 `scripts/flash.sh`，内部用 `powershell.exe` 调短名 `flash.ps1`
（由 `windows_flash_cube.ps1` 改名并去掉构建参数），始终等价于今天的 `-NoBuild`。

保留现有 HEX 地址校验：主镜像 `0x90000000..0x97ffffff`，stub `0x08000000..0x0801ffff`。
不要把这段校验再写一份 bash 实现。

OpenOCD 与 Cube 不能同时占 ST-LINK。Debug 的 preLaunch 继续：先 `wsl_kill_openocd.sh`，
再 Download，再 attach。
