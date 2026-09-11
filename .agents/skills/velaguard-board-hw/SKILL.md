---
name: velaguard-board-hw
description: "VelaGuard H750B-DK 真电路：原理图优先再改 pinmux/驱动。Use when: 引脚、pinmux、原理图 schematic、SchDoc、RS485 DIR、FT5x06、LTDC、扩展板、USART、ETH MII、BOARD INFO、H750B-DK 板级资料。"
---

# VelaGuard 真电路

凡是依赖开发板真实走线的工作（驱动、pinmux、SDMMC/UART/SPI/I2C/ETH bring-up、扩展板、断言某 MCU 脚接某外设），必须先查本地官方资料包，不能只靠网上 UM 或第三方 pinmux 笔记编码。

应用逻辑、MQTT、Agent 提示词、纯文档任务不要加载本 Skill。

## 资料包

- WSL：`/mnt/f/Project/Embeded/H750B-DK/BOARD INFO/H750B-DK`
- Windows：`F:\Project\Embeded\H750B-DK\BOARD INFO\H750B-DK`

查找顺序（必须按这次序，前一级能定论就停）：

1. 原理图 PDF / SchDoc
2. 资料包里 ST BSP（`bsp/`）
3. ST UM / data brief

文件名含 `unofficial` 的只能当检索线索。编码前必须对照原理图或 BSP。

扩展板方案（Arduino + STMod+，无 KiCad）：[docs/velaguard-expansion-board.md](../../../docs/velaguard-expansion-board.md)。已知坑：[references/pitfalls.md](references/pitfalls.md)，细节溯源 [docs/velaguard-bringup-known-issues.md](../../../docs/velaguard-bringup-known-issues.md)。

## 改哪里

| 树 | 典型分支 | 内容 |
|----|----------|------|
| `../nuttx` | `velaguard/*` | `board.h`、bring-up、pinmux、LTDC、FT5x06、RS485 DIR、defconfig |
| `../apps` | `velaguard/netinit-esp8266` | netinit / ESP8266 兼容 |
| 选手仓 | 本仓 | `app/`、`gui/`、`scripts/configs/*.defconfig` 复制到板级配置（安装树文件，不是 patch） |

**禁止**新增或 apply `scripts/openvela-*.patch`。在对应 git 树上直改，再 PR 到 `dev-ai-contest-2026`。

选手仓 `scripts/build.sh` 只校验这些树已经含 VelaGuard 改动。

## 编码前核对

1. 打开资料包，用原理图确认网名 / 连接器 / 是否接到 MCU。
2. 再打开 `../nuttx/boards/.../stm32h750b-dk/include/board.h` 对现有宏。
3. 与 [docs/velaguard-expansion-board.md](../../../docs/velaguard-expansion-board.md) 决策表冲突时，以原理图 + 该文档的已终审决策为准，不要另发明脚。
4. 读 [references/pitfalls.md](references/pitfalls.md)，避免重开已修好的 DIR / TIM15 / 触摸中断争论。
