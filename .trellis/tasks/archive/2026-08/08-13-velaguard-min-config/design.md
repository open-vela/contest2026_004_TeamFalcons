# Design: VelaGuard minimal bring-up config (velaguard-min)

## Architecture

两层改动：

1. **app 层（队伍仓，直接改）**：解绑工具与 LVGL demo 符号。
2. **配置层（nuttx 公共仓，patch 交付）**：新 defconfig + TIM15 笔误修复，
   均以 `scripts/` 下 patch + 幂等 apply 脚本落地。

## App 层解绑

`app/velaguard/Kconfig`：

```kconfig
config VG_BRINGUP_TOOLS
	bool "VelaGuard bring-up tools (vgpwm/vgrs485/vgesp + entrypoint app)"
	default LVX_USE_DEMO_CONTEST2026_004_VELAGUARD_APP
	select ARCH_HAVE_LEDS
```

- `default LVX_USE_DEMO_...`：ps1 未动时（LVX demo=y），工具默认仍编译 → AC6。
- `select ARCH_HAVE_LEDS`：`velaguard.c` 使用 `board_userled_*`，与现行为一致。
- `Makefile`：`MODULE = $(CONFIG_VG_BRINGUP_TOOLS)`；`PROGNAME` 只含
  velaguard_app/vgpwm/vgrs485/vgesp，与 MAINSRC 按位置配对；
  `velaguard.c` 经 `-Dmain=velaguard_app_main` 重命名。
- `CMakeLists.txt`：`if(CONFIG_VG_BRINGUP_TOOLS)`。
- LVX demo 符号保留，供未来 UI 应用使用。

## 入口与 NSH 托管（单一形态）

- `INIT_ENTRYPOINT="velaguard_app_main"`：Guard 作为系统入口常驻运行
  （主循环 LED 演示，后续挂 LVGL 等业务），同时拉一个 NSH 线程
  （`nsh_initialize()` + `nsh_consolemain()`）提供 shell，供调试
  vgpwm/vgrs485/vgesp 等小工具。两个任务并行，互不阻塞。
- 板级初始化由 NSH 线程内的 NSH_ARCHINIT 触发一次（BOARDIOC_INIT），
  入口线程不重复初始化，不需要 board 层幂等标志。
- velaguard.c 的 main 经 Makefile `-Dmain=` 重命名为 `velaguard_app_main`
  并注册为 NSH 命令 `velaguard_app`；shell 里再敲该命令时，防重护栏
  （`g_app_running`）直接返回，不会拉起第二个实例/嵌套 NSH。
- `VG_APP_HOSTS_NSH` 开关已删除（曾用于双形态切换，现统一为 app 入口形态）。

## 配置生成路径

```text
当前 .config（lvgl 基底 + 本队改动）
  → kconfig-tweak 关闭 R2 排除清单
  → --set-str CONFIG_INIT_ENTRYPOINT "velaguard_app_main"
    （并同步 CONFIG_INIT_ENTRYNAME，避免残留 "nsh_main"）
  → --enable CONFIG_VG_BRINGUP_TOOLS
  → make olddefconfig（校验依赖）
  → make savedefconfig（nuttx 根 defconfig）
  → cp 到 boards/.../configs/velaguard-min/defconfig
  → git add -N + git diff 生成 patch
```

保留符号（keep 清单）：`STM32H7_USART2/3`、`STM32H7_UART7`、`STM32H7_TIM15_PWM`、
`NSH_LIBRARY`、`DEV_NULL`、`DEV_ZERO`、`PWM`、`ARCH_HAVE_LEDS`（经 VG_BRINGUP_TOOLS）、
board.h 已有的 `GPIO_ESP_EN/RST`、`GPIO_TIM15_CH2OUT`、`GPIO_UART7_*` 等板级定义不动。

## Patch 与幂等契约

参照 `scripts/apply-openvela-qspi-patch.sh`：

- 先 `git apply --reverse --check` 通过 → "already applied" 退出。
- 再按 marker grep（如 defconfig 文件存在且含 `CONFIG_ARCH_BOARD_STM32H750B_DK`、
  `CONFIG_INIT_ENTRYPOINT="velaguard_app_main"`；stm32_pwm.c 含
  `CONFIG_STM32H7_TIM15_CH2OUT`）判断是否已应用。
- `git apply --check` 失败则报错退出，不自动冲突解决。

## /dev 契约

最小固件启动后 `/dev` 恰为：

```text
console null pwm0 rs485 ttyS0 ttyS1 ttyS2 zero
```

（ttyS0=USART3 console、ttyS1=USART2 ESP、ttyS2=UART7 RS485、rs485 为稳定软链。）

## 兼容与迁移

- 上游 `lvgl` defconfig 保持原样；切换命令：
  `tools/configure.sh -e stm32h750b-dk:velaguard-min|lvgl`，切换后需全量重编。
- ps1 不动：其 `--enable CONFIG_LVX_USE_DEMO...` 仍有效，经 Kconfig 默认链保住工具编译。
- 文档记录：运行 ps1 会把本地 `.config` 带回 UI 形态。

## 回滚

- `.config` 手术前备份（`.config.bak.velaguard-min-<date>`）。
- app 层改动为队伍仓普通 diff，`git checkout` 可回滚。
- nuttx 侧：`git apply -R <patch>` 即逆操作；apply 脚本本身幂等。
