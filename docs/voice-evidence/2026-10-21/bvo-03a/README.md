# BVO-03a STM32H7 SAI 驱动移植记录

- 日期：2026-10-21
- 任务：BVO-03a，BVO-01 第 11 节新增项，BVO-03/04 的共同前置
- 状态：驱动移植完成，两套采集路线均编译通过，零告警。**未烧录、未板端验证**
- 归属：`../nuttx`，分支 `velaguard/ptt-user1-stm32h750b-dk`

## 1. 为什么单列这一项

执行方案假定 SAI 驱动已存在。实际 nuttx 树里 STM32H7 没有任何 SAI 驱动，只有 STM32F7 与 STM32L4 各一份。BVO-03 的录音与 BVO-04 的播放都要走 SAI，所以先补驱动基础。

本轮只交付驱动本身，不做板级注册、不做 PDM 抽取、不声称录音可用。

## 2. 改动

| 文件 | 内容 |
|---|---|
| `arch/arm/src/stm32h7/hardware/stm32_sai.h`（新增） | 寄存器偏移、位定义。含 H7 独有的 `PDMCR`/`PDMDLY` |
| `arch/arm/src/stm32h7/stm32_sai.h`（新增） | 六个逻辑接口号，`stm32_sai_initialize()` |
| `arch/arm/src/stm32h7/stm32_sai.c`（新增，约 1500 行） | I2S 设备实现：RX/TX、DMA、看门狗、worker 回调、PDM 配置 |
| `arch/arm/src/stm32h7/Kconfig` | `STM32H7_SAI` 从空壳改成可用；新增 `SAI1/2/4` 与各 block、`SAI_PDM_MODE`、`SAI Configuration` 菜单 |
| `arch/arm/src/stm32h7/Make.defs`、`CMakeLists.txt` | 按 `CONFIG_STM32H7_SAI` 编入 |
| `arch/arm/src/stm32h7/hardware/stm32h7x3xx_rcc.h` | **修既有缺陷**：`RCC_D2CCIP1R_SAI1SEL_MASK` 引用了自己的名字，预处理后为 `7 << 7`，选 SAI1 时钟源会写错位。改为 `<< SHIFT` |

## 3. 与 STM32F7 蓝本的差异

F7 的 `stm32_sai.c` 不能直接复制，以下六处按 H7 重写（与 BVO-01 第 11 节列的改造点逐条对应）：

1. **RCC**：H7 走 `D2CCIP1R.SAI1SEL`/`SAI23SEL` 与 `D3CCIPR.SAI4ASEL`/`SAI4BSEL`，使能位在 `APB2ENR`/`APB4ENR`。按实例选择寄存器与位，不再有 PLLI2S。
2. **DMA**：F7 是 `stm32_dmasetup(handle, paddr, maddr, n, flags)`；H7 是 `stm32_dmasetup(handle, stm32_dmacfg_t *)`，通道来自 `stm32_dmachannel(DMAMAP_*)`。
3. **cache**：F7 驱动完全没有 cache 维护。H7 在 RX 启动前 invalidate、完成回调里再 invalidate，TX 启动前 clean。传输单位随数据宽度变化，cache 范围按实际单位计算。
4. **主从语义**：F7 把 `SYNCEN` 当主从开关用，`CONFIG_..._SYNC_WITH_*` 同时决定同步与主从。H7 版把同步固定为异步，主从由收发方向决定（录音与放音都是 master）。
5. **PDM**：新增 `CR1.PDMEN` 等位。PDM 配置放在 `sai_portinitialize()` 末尾，避免被 PCM 的默认帧/槽设置覆盖。同时按参考配置设 `LSBFIRST`。
6. **DMA 宽度**：F7 有 8/16/32 三档。H7 版按数据宽度在 16 与 32 之间选择，PDM 走 16 位。

## 4. 参考来源

ST 官方 `STM32Cube_FW_H7_V1.13.0`（本机 `C:\Users\19y\STM32Cube\Repository\`）：

| 用途 | 文件 |
|---|---|
| 寄存器位定义交叉核对 | `Drivers/CMSIS/Device/ST/STM32H7xx/Include/stm32h750xx.h` |
| PDM 边界条件 | `Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_sai.c`（PDM 仅 SAI1_Block_A / SAI4_Block_A，且必须 master RX + free protocol） |
| 本板 PDM 实测配置 | `Projects/STM32H750B-DK/Examples/BSP/Src/audio_record.c` |
| BSP 的 SAI4 PDM 初始化 | `bsp/stm32h750b_discovery_audio.c` 的 `MX_SAI4_Block_A_Init` |

从官方参考得到的四条事实，已写进驱动：

- **PDM 只能在 SAI1 block A 或 SAI4 block A**，且必须是 master RX、free protocol。HAL 对该组合有显式断言。
- **本板 PDM 帧参数**：帧长 32、活动帧长 1、槽数 1、16 位数据、时钟沿下降沿、PDM 时钟 2、单麦克风对。
- **MCLK 仍是 128×FS**。ST 参考里 `AudioFrequency` 传的是 `FS*8`，配合帧长 32，等效于 `MCKDIV = CK/(FS*256)`。这与 PCM 用的 256×FS 公式一致，所以分频器不需要 PDM 专用分支。用参考参数复核：`49.14 MHz / (16000×256) = 12.0`，与 HAL 结果一致，MCLK = 2.0475 MHz，比特时钟约 2.048 MHz。
- **PDM 数据按 LSB 优先**交给内存。后续解码必须假定同一顺序。

## 5. 采集路线选择

BVO-01 推荐 SAI4 + BDMA，理由是 ST BSP 参考充分。本轮据官方资料补充两点：

- SAI1 block A 同样支持 PDM（HAL 断言明确允许），且 PE4/PE5 的 AF2 复用给出同样的 `CK2`/`D2` 角色，所以两条路线的引脚语义一致，切换只改 GPIO 复用与 DMA 请求。
- SAI4 在 D3 域，其数据寄存器只能由 BDMA 服务，而 `CONFIG_STM32H7_BDMA` 依赖 `CONFIG_EXPERIMENTAL`。Kconfig 因此把 `SAI4_A`/`SAI4_B` 写成同时依赖 `STM32H7_SAI4 && STM32H7_BDMA`，不隐式打开实验性选项。

驱动两条路线都支持，编译均通过。**默认走 SAI4 + BDMA**（BVO-01 的结论），但需要先确认打开 `CONFIG_EXPERIMENTAL` 后本板仍稳定。若不成立，改选 SAI1，只改 defconfig 与 GPIO 复用，驱动无需改动。

## 6. 构建

两套配置都做了完整构建（`make -j8`，链接到 `nuttx.hex`）：

| 配置 | 结果 |
|---|---|
| `SAI1 + SAI1_A + PDM_MODE + DMA` | 编译通过，`stm32_sai.c` 零告警，链接通过 |
| `SAI4 + SAI4_A + BDMA + EXPERIMENTAL + PDM_MODE + DMA` | 编译通过，`stm32_sai.c` 零告警，链接通过 |

对象文件中可见 `stm32_sai_initialize`。链接后该符号被丢弃，因为还没有板级调用者，这是预期行为，不是缺陷。BVO-03 接入板级注册后才会进入镜像。

内存占用无变化（SRAM4 仍为 0），因为 BDMA 未被任何实际缓冲使用。

## 7. 未做与边界

- 不注册板级音频设备，不写 `/dev/audio/*`。
- 不做 PDM 到 PCM 的抽取滤波。ST 的 PDM2PCM 库是 MCD-ST Image SW License V2，不是 Apache-2.0，不能直接进 nuttx 树。BVO-03 需要基于参考实现自行实现抽取。
- 未改 `board.h` 的 PLL2 配置。当前 PLL2P = 400 MHz，`MCKDIV` 需要约 98，超过 6 位字段上限（63）。**BVO-03 若要录音，必须先把 PLL2 重配成能产生约 49 MHz 的参数**，或确认可用的其它时钟源。这一点本轮只记录，不改板级时钟。
- 未烧录、板端运行留到 BVO-03b。

**BVO-03b 更正（2026-10-21）**：

- 上面"PDM 需要约 49 MHz、`MCKDIV=12`"的算术本身没错，但这次的 MCLK 语义读错了。
  ST 的 `NoDivider = SAI_MASTERDIVIDER_DISABLE` 里，`SAI_MASTERDIVIDER_DISABLE`
  的定义就是 `SAI_xCR1_NODIV`，也就是 **PDM 要求 NODIV=1**，分频器直接驱动位时钟。
  本文第 4 节把 `SAI_MASTERDIVIDER_DISABLE` 当成 NODIV=0 来解释，是错的。
- PLL2 重配时 `PLL2RGE` 必须同步从 `4_8_MHZ` 改为 `1_2_MHZ`：分频比 25 让 PLL
  输入落到 1 MHz，本文没有涉及这一条约束。
- 本文第 7 节关于 SW2 的前提不成立。B-01 原理图与 BOM 里没有 SW2，板上只有
  B1/B2 两个按键，麦克风直连 SAI，无需任何开关。ST 示例里那句提示是从带音频
  编解码器的板子继承下来的。
- 第 3 节第 6 条写的"DMA 宽度按数据宽度在 16 与 32 之间选择"不完整：PDM 接收
  必须用 **8 位**，按 16 位读会让相邻字节成对重复。

## 8. 待 BVO-03 处理

1. PLL2 重配（阻塞项：不解决则 PDM 比特时钟无法落到 2.048 MHz）。
2. SW2 位置与麦克风选通关系。
3. PDM 抽取滤波实现（替代不可用的 ST 库）。
4. 板级注册与 `/dev/audio/pcm0c`。

**BVO-03b 完成情况（2026-10-21）**：第 1 项已做（PLL2 改为 M=25/N=344/P=7，
`PLL2RGE` 同步改 `1_2_MHZ`）；第 2 项结论是不存在这个开关；第 3、4 项未做。
另外第 1 项里"2.048 MHz 比特时钟"这个目标应理解为每帧 1 字节、字节率约
128 KB/s；实际位时钟由 NODIV=1 直接取自 MCKDIV，见 BVO-03b 记录。
