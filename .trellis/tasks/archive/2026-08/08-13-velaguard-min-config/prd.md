# VelaGuard minimal bring-up config (velaguard-min)

## Goal

为 VelaGuard bring-up 提供一个干净的最小固件基线：只保留 console/NSH、板载 LED、
RS485、ESP-01S、DO(PWM)，去掉 LVGL/LTDC/FB/触摸/网络/urandom 等预设。
同时保证后期可一键切回 LVGL（时机由用户决定），且 bring-up 工具
（vgpwm/vgrs485/vgesp/velaguard_app）不再被 LVGL demo 符号绑架。

## Background / confirmed facts

- 板子上游只有 `stm32h750b-dk:lvgl` 一个预设（`boards/arm/stm32h7/stm32h750b-dk/configs/lvgl/defconfig`），
  因此最小预设由减法生成；结果干净，板级平台代码（时钟树、QSPI 启动、console 初始化）保留。
- 当前 `.config`（nuttx 仓库，gitignored）启用了：`GRAPHICS_LVGL`、`LVX_USE_DEMO_*`、
  `EXAMPLES_FB`、`VIDEO_FB`、`STM32H7_LTDC`(+子项)、`INPUT/INPUT_FT5X06/FT5X06_*`、
  `DEV_URANDOM(+XORSHIFT128)`、`NET/NETINIT/NET_*`、`SYSTEM_PING`、`SYSTEM_DHCPC_RENEW`、
  `LV_USE_SYSMON`、`LV_USE_PERF_MONITOR`。
- `/dev` 来源：fb0←VIDEO_FB+LTDC；input0←INPUT_FT5X06；urandom←DEV_URANDOM；
  zero/null←NuttX 默认（`default !DEFAULT_SMALL`），必须保留。
- 工具被绑架：`app/velaguard/Makefile` 的 `MODULE = $(CONFIG_LVX_USE_DEMO_CONTEST2026_004_VELAGUARD_APP)`；
  Kconfig `select ARCH_HAVE_LEDS`；app 经软链映射为 `packages/demos/contest2026_004_hello_app`
  （见 `scripts/ensure-openvela-links.sh`）。
- 引脚合同（`docs/velaguard-expansion-board.md`）：console=USART3 PB10/PB11；
  RS485=UART7 PB4/PA8、DIR PK1（`/dev/rs485`→ttyS2）；ESP=USART2 PD5/PD6、EN PA3、RST PH10；
  DO=TIM15_CH2 PE6（`/dev/pwm0`，vgpwm 驱动 DO1 无源蜂鸣器）；板载 LED 走 `board_userled`（LED num: 3）。
- TIM15 笔误修复已在本地 `arch/arm/src/stm32h7/stm32_pwm.c` 生效
  （`#ifdef CONFIG_STM32H7_TIM15_CH2OUT`），/dev/pwm0 依赖它；须按仓库规矩固化为 patch。
- `scripts/windows_build_openvela.ps1` 默认 lvgl 预设 + UI tweak；本轮完全不动，
  在文档中记录"跑一次 ps1 会把本地配置切回 UI 形态"。
- nuttx 公共仓零直改：所有 nuttx 侧改动一律 `scripts/` 下 patch + apply 脚本
  （参照 `apply-openvela-qspi-patch.sh` 的幂等模式）。

## Requirements

- R1 新建 nuttx defconfig `stm32h750b-dk:velaguard-min`（减法生成），仅保留：
  console USART3+NSH、板载 userled（`ARCH_HAVE_LEDS`）、UART7 RS485（`/dev/rs485`）、
  USART2 ESP + EN/RST GPIO、TIM15 PWM（`/dev/pwm0`）、`DEV_NULL`/`DEV_ZERO`。
- R2 最小预设排除：`GRAPHICS_LVGL`+`LV_*`、`LVX_USE_DEMO_*`、`EXAMPLES_FB`、`VIDEO_FB`、
  `STM32H7_LTDC`(+子项)、`INPUT`/`INPUT_FT5X06`/`FT5X06_*`、`DEV_URANDOM`(+算法子项)、
  `NET`/`NETINIT`/`NET_*`、`SYSTEM_PING`/`SYSTEM_DHCPC_RENEW`、`LV_USE_SYSMON`/`LV_USE_PERF_MONITOR`。
- R3 单一形态（用户最终拍板）：`INIT_ENTRYPOINT="velaguard_app_main"`，
  Guard 作为系统入口常驻（主循环 LED 演示，后续挂 LVGL），同时拉起 NSH 线程
  （`nsh_initialize()` + `nsh_consolemain()`）提供 shell 调试工具；
  `velaguard_app` 命令带 `g_app_running` 防重护栏；板级初始化由 NSH 线程内
  NSH_ARCHINIT 触发一次，无需 board 层幂等标志。
- R4 app 解绑：新增 bool `VG_BRINGUP_TOOLS`（`default LVX_USE_DEMO_CONTEST2026_004_VELAGUARD_APP`，
  `select ARCH_HAVE_LEDS`）；Makefile `MODULE` 与 CMakeLists 条件改挂 `CONFIG_VG_BRINGUP_TOOLS`；
  LVX demo 符号保留给未来 UI。
- R5 nuttx 侧改动仅以补丁形式交付：`openvela-velaguard-min-defconfig.patch`（新增
  `boards/arm/stm32h7/stm32h750b-dk/configs/velaguard-min/defconfig`）、
  `openvela-pwm-tim15-fix.patch`（TIM15 笔误），各配幂等 apply 脚本。
- R6 文档更新：bring-up 已知事项文档写明两预设切换命令、ps1 未动警告、
  排除项理由、`VG_BRINGUP_TOOLS` 说明。

## Acceptance Criteria

- [ ] AC1 `tools/configure.sh -e stm32h750b-dk:velaguard-min` + `make olddefconfig` + 全量 `make` 通过
  （WSL，PATH 含 arm-none-eabi）。
- [ ] AC2 defconfig 中无 fb/input/urandom/network/LVGL 相关符号；`INIT_ENTRYPOINT="velaguard_app_main"`；
  `VG_BRINGUP_TOOLS=y`；keep 清单（USART2/USART3/UART7/TIM15_PWM/NSH）齐全。
- [ ] AC3（硬件，用户执行）上电进 NSH；`ls /dev` 恰为
  `console null pwm0 rs485 ttyS0 ttyS1 ttyS2 zero`；NSH 内 vgpwm/vgrs485/vgesp/velaguard_app 可用；
  `vgrs485 tx` 在 COM23 收到完整 26 字节；`vgesp at` 返回 OK。
- [ ] AC4 上游 `lvgl` 预设文件未改动；切回命令 `configure.sh -e stm32h750b-dk:lvgl` 可正常配置并编译
  （构建冒烟即可）。
- [ ] AC5 两个 apply 脚本幂等：二次执行输出 "already applied"。
- [ ] AC6 回归保护：LVX demo=y 且 VG_BRINGUP_TOOLS 未显式设置时，`olddefconfig` 后工具仍编译
  （ps1 路径不被破坏）。

## Out of Scope

- ps1 脚本改动（用户明确暂缓）
- 新增 `vgled` 等 LED 控制命令、删除 `velaguard_app`
- 修复 `ARCH_LEDS`/`board_userled` 链接错误根因（维持现状 ARCH_LEDS 关闭）
- 最小预设保留以太网（按决策排除）
- 本次实际启用 LVGL（属后续任务，文档给出切换路径）

## Key Decisions（grilling 结论）

1. 基座 = 减法生成的新预设 `velaguard-min`，结果干净、平台代码保留。
2. 保留清单：console/NSH、板载 LED、RS485、ESP-01S、DO(PWM)。
3. 排除清单：LVGL/LTDC/FB/触摸/urandom/网络栈。
4. 单一形态：velaguard_app_main 入口常驻（Guard 主循环）+ NSH 线程托管，
   产品/开发统一；shell 里再敲 velaguard_app 由防重护栏拦截。
5. 工具独立开关 `VG_BRINGUP_TOOLS`，minimal 与未来 LVGL 模式默认都带工具。
6. 后期 LVGL 用预设/开关一键开启，时机由用户决定。
7. ps1 本轮不动。

## Risks / Deferred

- kconfiglib 对 `default LVX_USE_DEMO_...` 符号引用的支持需验证；不支持则退化为
  `default y if LVX_USE_DEMO_...`。
- `make savedefconfig` 输出到 nuttx 根 `defconfig`，需手动拷入 `configs/velaguard-min/defconfig`。
- ps1 仍默认 lvgl：后期运行 ps1 会把本地 `.config` 切回 UI 形态（文档记录，非 bug）。
- 生成 TIM15 patch 时需只取 `stm32_pwm.c` 的笔误改动，避免混入其它本地编辑。
- AC3 依赖用户硬件（COM7/COM23），由用户执行验证。
