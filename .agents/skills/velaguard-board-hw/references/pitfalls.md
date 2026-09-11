# 已证伪的板级坑

编码前对照原理图。这里只列已经在树上修过、或必须用原理图才能下结论的项。全文：[docs/velaguard-bringup-known-issues.md](../../../../docs/velaguard-bringup-known-issues.md)。

## RS485 DIR

发送前把 DIR 拉到发送；`txempty` 必须等 **TXE|TC** 之后再切回接收。不要在 TC ISR 里切 DIR，也不要 `close` 过早拆 DIR。

树上现状（2026-08-29）：RS485 关 FIFO；`vgrs485` 不再靠 `usleep(50ms)` 糊时序。

逻辑分析仪证 DI/DIR 波形正确、但 A/B 仍乱码时，优先查 USB-RS485 收发器与共地，不要先改固件时序。

UART7：`GPIO_UART7_RX=PA8`，`GPIO_UART7_TX=PB4`。console 仍是 USART3（ST-LINK VCP）。NuttX 编号：`ttyS0`=USART3，`ttyS1`=USART2（ESP），`ttyS2`=UART7（`/dev/rs485`）。

## TIM15 CH2 守卫

`nuttx/arch/arm/src/stm32h7/stm32_pwm.c` 里 TIM15 通道 2 曾被错误写成 `CONFIG_STM32H7_TIM12_CH2OUT`，PE6 不配脚，蜂鸣器命令正常但无方波。正确守卫是 `CONFIG_STM32H7_TIM15_CH2OUT`。已在 nuttx 树直改，不要再 apply 旧 patch。

## FT5x06 INT

触摸卡顿 / 断触时，先查资料包原理图：INT 是否真正接到 MCU。接到了才能上中断；没接到就不要假装有下降沿，改用（或保留）轮询，并把轮询参数写进 defconfig。网上 UM 或 `unofficial` 笔记不能单独当证据。

## 不要再走 patch

历史文档若出现 `scripts/openvela-*.patch` 文件名，只作溯源。以当前 nuttx / apps 树上的源码为准。
