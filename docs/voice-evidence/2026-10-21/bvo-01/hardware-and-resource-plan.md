# BVO-01 板端语音硬件与资源方案

- 日期：2026-10-21
- 任务：BVO-01，依据 `board-voice-execution-plan.md` 第 5 节与 ADR 0009
- 状态：**硬件项已全部实测回填，方案定稿**。本文不含任何编译、烧录或板端运行结论；所有板端数字引自既有证据，未在本轮执行。
- 归属：参赛仓文档；未修改 `AGENTS.md`、ADR 0009 与执行方案，二者仍在 `.vg-maintenance-closeout`

## 1. 范围与结论摘要

BVO-01 的任务是用原理图和 ST BSP 交叉核对 USER1 按键、数字麦克风与播放输出三条线路，给出总线、时钟、DMA、引脚占用结论，列出 RAM 与线程预算，并固化 BVO-02 至 BVO-05 要用的接口参数。

本轮结论：

| 项目 | 结论 | 强度 |
|---|---|---|
| USER1 按键 | PC13，按下为高，板上无其它占用 | 已实测 |
| 数字麦克风 | U34 MP34DT01TR，CLK 接 PE5、DOUT 接 PE4，走 SAI4 PDM 直连 | 已实测 |
| 采集路线 | 路线 A（SAI4 PDM 直连），麦克风不经过 WM8994 | 已实测确认 |
| 采集 DMA | 可用 BDMA（SAI4 所在 D3 域），也可改走 SAI1 + DMA1/DMA2 以避开 BDMA | 已核实 |
| 播放输出 | 绿色 3.5mm 插座由 HPOUT1 驱动，板上没有独立的 Line Out 接口 | 已实测 |
| 扬声器跳线 | SPKOUTLN-LP、SPKOUTRN-RP 两处短接，与 HPOUT1 并存于同一插座 | 已实测 |
| 蓝色插座输入 | IN1LN / IN1RN，与 MICBIAS1 无关 | 已实测 |
| MICBIAS1 | 不需要软件使能 | 已实测 |
| SAI 驱动 | NuttX 无 STM32H7 SAI 驱动，需新增 BVO-03a | 已核实 |
| 时钟 | PLL2 已启用，但 `board.h` 中 PLL2 的 Q/R 定义不完整 | 已核实 |
| 内存 | 板端 maxused 428 KB，总 7.0 MB，余量足够 480 KB 原始录音 | 依据 2026-10-18 基线 |

原计划的 7 条待实测项已由用户于 2026-10-21 逐条实测回填，结果见 `pre-measurement-checklist.md`。M-01 的结论推翻了本文初稿的路线 B 推荐，第 5 节已按实测重写。M-03 暴露出一处初稿未预料到的问题，见第 6 节。

## 2. 证据来源与判读边界

| 来源 | 路径 | 可用性 | 判定强度 |
|---|---|---|---|
| 原理图 PDF | `mb1381-h750xb-b01-schematic.pdf`（22 页） | 文字层无法机器抽取，只有引脚编号等短文本 | 需人工视图 |
| Altium 原理图源 | `mb1381-bdp/MB1381B/Altium_Designer/Audio.SchDoc` 等 | 可提取元件、网络标号、坐标、焊桥类型 | 元件与网络可信；**连接关系需人工判读** |
| ST BSP | `bsp/`（v3.3.4，Board ID MB1381B） | 完整 `.c`/`.h` | 强，代表 ST 实际配置 |
| 引脚分配表 | `STM32H750B-DK_pinmux_unofficial.csv` / `.md` | 文件名含 unofficial | 仅辅助 |
| NuttX 源码 | `../nuttx` | 完整 | 强 |
| 板端基线 | `docs/maintenance-evidence/2026-10-18/board-baseline.md` | 实际运行记录 | 强，但为另一份固件 |

**判读边界**：本文中凡标注为网络标号或元件的结论，来自 SchDoc 文本提取，可信到元件与网络名一级；凡涉及哪个网络连到哪个引脚、焊桥实际短接与否、插座由哪一路驱动，SchDoc 文本不含连线拓扑，只能靠实测确认。这些项目一律标为待实测，不写成已确认。

## 3. 信号级交叉核对表

坐标列为 Audio.SchDoc 中的文本记录坐标，供人工在原理图视图里定位。

| 信号 | 原理图（网络标号/引脚/坐标） | ST BSP 宏 | NuttX H7 pinmap | 结论 | 矛盾 |
|---|---|---|---|---|---|
| SAI2_MCLKA | 网络 `SAI2_MCLKA`，PI4（410,480 / 840,550） | `AUDIO_OUT_SAIx_MCLK_GPIO_PORT GPIOI` `GPIO_PIN_4` `GPIO_AF10_SAI2` | `GPIO_SAI2_MCK_A_2`（PE 无关，PORTI PIN4 AF10） | 一致 | 无 |
| SAI2_SCKA | 网络 `SAI2_SCKA`，PI5（410,530 / 470,530） | `SCK_GPIO_PORT GPIOI` `GPIO_PIN_5` | `GPIO_SAI2_SCK_A_2`（PORTI PIN5 AF10） | 一致 | 无 |
| SAI2_SDA | 网络 `SAI2_SDA`，PI6（410,510 / 470,510） | `SD_GPIO_PORT GPIOI` `GPIO_PIN_6` | `GPIO_SAI2_SD_A_2`（PORTI PIN6 AF10） | 一致 | 无 |
| SAI2_FSA | 网络 `SAI2_FSA`，PI7（410,520 / 470,520） | `FS_GPIO_PORT GPIOI` `GPIO_PIN_7` | `GPIO_SAI2_FS_A_2`（PORTI PIN7 AF10） | 一致 | 无 |
| SAI2_SDB | 网络 `SAI2_SDB`，PG10（470,500） | `AUDIO_IN_SAIx_SD_GPIO_PORT GPIOG` `GPIO_PIN_10` `GPIO_AF10_SAI2` | `GPIO_SAI2_SD_B_4`（PORTG PIN10 AF10） | 一致 | 无 |
| SAI4_CK2 | 网络 `SAI4_CK2`，PE5（130,460 / 180,460） | `AUDIO_IN_SAI_PDMx_CLK_IN_PORT GPIOE` `GPIO_PIN_5` `GPIO_AF10_SAI4` | `GPIO_SAI4_CK2_0`（PORTE PIN5 AF10） | 一致 | 无 |
| SAI4_D2 | 网络 `SAI4_D2`，PE4（130,470 / 180,470） | `AUDIO_IN_SAI_PDMx_DATA_IN_PORT GPIOE` `GPIO_PIN_4` | `GPIO_SAI4_D2_0`（PORTE PIN4 AF10） | 一致 | 无 |
| Audio_Int | 网络 `Audio_Int`，PG7（410,490），原理图标注 `NLAudio0Int` | 无（BSP 不处理 codec 中断） | `GPIO_SAI1_MCLK_A_2` 复用同一引脚，但未使用 | 引脚存在，VelaGuard 未使用 | BSP 未覆盖 |
| I2C4_SCL | 网络 `IIC4_SCL`，PD12（850,590） | `BUS_I2C4_SCL` / `AUDIO_I2C_ADDRESS 0x34` | `stm32_i2cbus_initialize(4)` | 一致 | 触摸、Arduino、STMod 共用 |
| I2C4_SDA | 网络 `IIC4_SDA`，PD13（850,600） | `BUS_I2C4_SDA` | 同上 | 一致 | 同上 |
| MIC_CLK | 网络 `MIC_CLK`（240,320），实测接 PE5 | 无（BSP 由 SAI4 输出 CK2 作 PDM 时钟） | `GPIO_SAI4_CK2_0`（PORTE PIN5 AF10）；同一引脚另有 `GPIO_SAI1_CK2_0`（AF2） | 麦克风时钟由 SAI4 CK2 驱动 | 已实测 |
| MIC_DATA | 网络 `MIC_DATA`（280,330），实测接 PE4 | 无 | `GPIO_SAI4_D2_0`（PORTE PIN4 AF10）；同一引脚另有 `GPIO_SAI1_D2_0`（AF2） | 麦克风数据进 SAI4 D2 | 已实测 |
| MICBIAS1 | 网络 `MICBIAS1`（150,150） | WM8994 提供 | 无 | 与数字麦克风无关，不需要软件使能 | 已实测 |
| PC13 / WAKEUP | 引脚 `PC13`，网络 `WAKEUP`，文本 `Wakeup & Key Button`、`B1` | `BUTTON_USER_PIN GPIO_PIN_13`，`GPIOC`，`GPIO_PULLDOWN`，`GPIO_MODE_IT_RISING` | `arch/arm/include/stm32h7/stm32h7x3xx_irq.h` 有 EXTI15_10 | PC13 是 USER1，按下为高 | 已实测；`board.h:527` 注释仍误写 PI11 |

关于最后一行：`boards/arm/stm32h7/stm32h750b-dk/include/board.h:527` 的注释写的是 `connected to GPIO PI11`，与 BSP、板 README、以及参赛仓 `docs/velaguard-expansion-board.md:274` 的记录都不一致。后三处一致记为 PC13。本文按 PC13 采用，注释错误留给 BVO-02 在改板级文件时一并修正。

## 4. 外设占用冲突表

| 资源 | 用途 | 当前是否被 VelaGuard 占用 | 音频要用的部分 | 冲突判断 |
|---|---|---|---|---|
| SAI2 block A | WM8994 播放，master TX | 否（`CONFIG_STM32H7_SAI` 未启用） | PI4/5/6/7 | 无冲突，独占 |
| SAI2 block B | WM8994 采集，slave RX | 否 | PG10 | 采集已定为 SAI4，本 block 不进首版 |
| SAI4 block A | PDM 麦克风，master RX | 否 | PE4/PE5 | **采集主路径** |
| SAI1 block B | PE4/PE5 的备选映射（AF2） | 否 | 仅在选择 2 时使用 | 只作 BDMA 不可用时的退路，见第 5 节 |
| DMA1/DMA2 | SAI2 播放搬运；选择 2 时也搬 SAI4 | 已启用（`CONFIG_STM32H7_DMA1/DMA2=y`），已有使用者为 SDMMC 等 | TX 一个通道；选择 2 再加 RX 一个 | 需 `stm32_dmachannel` 按 DMAMAP 申请，不复用固定 stream |
| BDMA | SAI4 采集搬运（选择 1） | 否（`CONFIG_STM32H7_BDMA` 未开，且依赖 `EXPERIMENTAL`） | DMAMUX2 通道 | 要额外打开一个实验性选项，可用 SRAM4 上限 64 KB 减去 `.sram4` 段占用 |
| I2C4 | WM8994 控制（地址 0x34） | 是，FT5x06 触摸已初始化（`stm32_ft5x06.c:281`） | WM8994 寄存器读写 | 无冲突，`stm32_i2cbus_initialize` 带引用计数（`stm32_i2c.c:2767`） |
| PLL2 | SAI 核时钟潜在来源 | PLL2 已启用（M=5、N=160、P=2）；当前无实际消费者 | SAI2/SAI4 核时钟 | 可用，但 `board.h` 的 PLL2 Q/R 定义不完整，见第 8 节 |
| PLL3 | LCD | 是（LTDC） | 不使用 | 不改动 |
| PC13 | USER1 按键 | 否 | 按键输入 | 无冲突；需打开 `ARCH_HAVE_BUTTONS` |
| PG7 | WM8994 中断 | 否 | 不使用 | 首版不需要 codec 中断 |
| PE4/PE5 | 麦克风 PDM | 否 | SAI4 主路径；可改配 SAI1 | 单一麦克风，同一时刻只能有一组 AF 生效 |
| PI4-PI7 | SAI2 | 否 | 播放 | 无冲突 |
| PG10 | SAI2_SDB | 否 | 不使用 | 麦克风不经 WM8994，本信号首版闲置 |
| SRAM4 | BDMA 可达区 | 否 | 选择 1 的 DMA 缓冲 | 只限制 DMA 直接读写的缓冲，不限制整段录音累积位置 |
| 绿色 3.5mm 插座 | 模拟输出 | 否 | HPOUT1 与 SPKOUT 经跳线并存 | **需软件二选一，见第 6 节** |
| DTCM/ITCM | — | 已进堆 | 不可用于 DMA 缓冲 | 硬约束，见第 7 节 |

关于 PE4/PE5：这两个引脚在本板上同时具备 SAI4（AF10）与 SAI1（AF2）两种复用。同一时刻只能启用其中一组。选择 1 与选择 2 之间切换需要改 GPIO 配置，不是运行时可切换项。

## 5. 采集路线（已按实测确定）

### 实测结论

用户于 2026-10-21 实测：麦克风 `MP34DT01TR` 的 `CLK` 接 `PE5`，`DOUT` 接 `PE4`。这两个引脚在原理图上分别对应网络 `SAI4_CK2` 与 `SAI4_D2`，即麦克风直连 SAI4 的 PDM 接口，**不经过 WM8994**。

本文初稿曾推荐路线 B（经 WM8994 片内抽取），依据是 ST BSP 同时提供了这条路径的配置。实测推翻了该推荐：BSP 有配置代码不等于本板这样装配。初稿的判断已作废，不再作为实施依据。

### 路线 A：麦克风直连 SAI4，主核做 PDM 抽取

链路：U34 → `MIC_CLK`/`MIC_DATA` → PE5/PE4 → SAI4 block A（master RX，PDM 模式）→ DMA → 缓冲 → 64 倍抽取 → PCM。

要新写的东西：

- SAI4 的 PDM 模式。`arch/arm/src/stm32f7/hardware/stm32_sai.h` 与 H7 都没有 PDM 相关位定义（全树 `PDMEN`、`MICPAIRNBR` 零命中），要按参考手册新增 `CR1.PDMEN`、`CR4.MICPAIRNBR`、PDM 时钟使能等。
- PDM 到 PCM 的抽取滤波。本地资料包里没有 ST 的 PDM2PCM 库（`bsp/` 只有 `.c`/`.h`，无 `Components/`）。可用 `apps/mlearning/cmsis/CMSIS_5/CMSIS/DSP` 的 `arm_fir_decimate_*`（Apache-2.0，`arm_fir_decimate_f32/q15/q31` 与对应 init 均在树内），但 64 倍抽取的滤波器系数、位序与增益标定要自行设计，并与 PDM 位流对齐。

### 采集 DMA 的两个选择

SAI4 的基址是 `0x58005400`，属 D3 域。`stm32_dma.c:2048-2160` 的 `stm32_bdma_capable` 限制很明确：BDMA 只能访问 D3 域外设与 SRAM4（`0x38000000`，64 KB），其余内存一律拒绝。因此若用 SAI4，只有两条路：

选择 1，用 BDMA：

- 要打开 `CONFIG_STM32H7_BDMA`，该项 `depends on EXPERIMENTAL`。
- DMA 缓冲必须落在 SRAM4。注意这只限制 DMA 直接读写的那一小块缓冲，不限制整段录音：DMA 按块把数据搬进 SRAM4 后，主核可以把已抽取的 PCM 搬到 AXI SRAM 或 SDRAM 累积 15 秒。初稿把这一条写成整段录音必须驻留 SRAM4，是过严的判断，此处更正。
- SRAM4 的剩余空间是 64 KB 减去 `.sram4` 段已占用部分（`qspi_flash.ld:91-96` 的 `.sram4_reserve`，`_sram4_heap_start` 起才进堆）。具体余量由 BVO-03 实测。

选择 2，改用 SAI1 的同一组引脚：

- PE4/PE5 除 SAI4（AF10）外还能映射到 SAI1 的 `D2_0`/`CK2_0`（AF2，`stm32h7x3xx_pinmap.h:978,983`）。
- SAI1 基址 `0x40015800` 属 D2 域，走 DMA1/DMA2（`DMAMAP_DMA12_SAI1A_0`/`SAI1B_0`，`stm32h7x3xx_dmamux.h:412-415`），不需要 BDMA，也就不引入 EXPERIMENTAL 依赖，且缓冲可任意落在 AXI SRAM 或 SDRAM。
- 代价是 PDM 模式仍要自己加，抽取滤波仍要自己做；另外 SAI1 是否支持 PDM 模式要查参考手册确认，不同型号的 SAI 实例对 PDM 的支持不完全一致。这一点是选择 2 的**首要待核实项**。

两条选择都要 BVO-03a 的 SAI 基础驱动。选择 2 在依赖上更干净，但存在 SAI1 PDM 能力未核实的风险；选择 1 是 ST BSP 实际使用的路径，参考价值最高。

### 推荐

先按选择 1（SAI4 + BDMA）推进，因为 ST BSP 的 PDM 配置全部基于 SAI4，参考最充分，且 BSP 的 `MX_SAI4_ClockConfig` 给出了可直接复用的 PLL2 参数。

在选择 1 实施前，BVO-03a 要先核实两件事：`CONFIG_STM32H7_BDMA` 在本板打开是否稳定（它是 EXPERIMENTAL），以及 SRAM4 的实际可用余量。若其中任一项不成立，改按选择 2 走 SAI1，并把 SAI1 PDM 能力作为先决检查项。

### 不做的事

不建议再考虑经 WM8994 的路线。实测显示麦克风不经 WM8994，走那条路要么需要改板飞线，要么需要软件把 PE4/PE5 配成 SAI4 却指望 WM8994 收到数据，两者都不成立。

## 6. 播放输出

链路：PCM/WAV → `pcm_decode` → WM8994（SAI2 block A，master TX）→ HPOUT1 → 绿色 3.5mm 插座。

已确认：

- SAI2 block A 为 master TX，MCLK/SCK/FS/SD_A 在 PI4/5/6/7，BSP 与 NuttX pinmap 一致。
- `drivers/audio/wm8994.c` 当前只启用 HPOUT1（耳机）与 SPKOUT（扬声器）两条模拟输出，代码里 `hp_out=1`、`spk_out=0` 写死（`wm8994.c:1879-1882`），注释明确写 `Currently only headphones are supported!`。
- BSP `BSP_AUDIO_OUT_SetDevice` 的注释写 `there is only one device (AUDIO_OUT_HEADPHONE)`。
- `wm8994.h` 已有 LINEOUT1/2 的使能位与输出混音位定义（如 `WM8994_LINEOUT1P_ENA`、`WM8994_LINEOUT2P_ENA`、`WM8994_DAC1L_TO_MIXOUTL`），但驱动里没有使用。

### 实测结论

用户于 2026-10-21 实测：

- M-02：绿色插座由 **HPOUT1** 驱动。
- M-03：两个 2x1 跳线分别短接 `SPKOUTLN` 与 `LP`、`SPKOUTRN` 与 `RP`，即把左右声道的扬声器输出接到插座的 L/R 触点。
- M-04：蓝色插座接 `IN1LN` / `IN1RN`，与 `MICBIAS1` 无关。

### 这组结果暴露的问题

`wm8994.c` 当前把 `hp_out = 1`、`spk_out = 0` 写死（`wm8994.c:1879-1882`），注释写 `Currently only headphones are supported!`。而 M-03 显示 SPKOUT 已经通过跳线直接接到同一个插座的 L/R 触点。

这意味着插座的 L/R 实际上同时挂着 HPOUT1 与 SPKOUT 两路模拟输出。两个输出都使能时，两路源会并联驱动同一负载，至少会互相加载、音质下降，严重时可能让对方输出级工作异常。

VelaGuard 必须在软件上二选一，不能两路都开。三种处理方式：

1. 只开 HPOUT1（现状的 `hp_out=1, spk_out=0`）。跳线仍在，SPKOUT 输出级未使能，视为高阻，理论上不影响 HPOUT1。这是最小改动方案。
2. 只开 SPKOUT。需要把写死值反过来，并确认跳线存在时 SPKOUT 的负载与音量是否合适。
3. 保留两路能力但互斥使能，由配置选择。

推荐方式 1，理由是现有驱动已经这样写，且 HPOUT1 是 BSP 唯一声明的输出设备。但要在 BVO-04 明确记录跳线存在这一硬件事实，并在验收时确认 `spk_out` 保持关闭属于有意设计，需要在代码注释里写明原因。

另外，若使用者把跳线拔掉，SPKOUT 与插座断开，行为不变；若保留跳线又误开 SPKOUT，就会出现上述并联问题。这条要写进 BVO-04 的验收条件和手册说明。

### 未决点

音量安全初值。BSP 默认 `WM8994_INITVOLUME 250`（千分比）。HPOUT1 直连外部 Line In 时这个值可能偏大，由 BVO-04 实测后固化。

### 与执行方案的差异

执行方案要求输出到 Line Out。实测显示绿色插座由 HPOUT1 驱动并兼作扬声器输出点，并不存在独立的 Line Out 接口。BVO-04 的验收口径应写成绿色 3.5mm 插座输出，不再沿用 Line Out 的说法，并在手册里注明该插座的电平特性按耳机输出而非线路电平对待。

## 7. RAM、DMA、线程与缓冲预算

### 基线

| 项目 | 数值 | 来源 |
|---|---|---|
| 堆总量 | 7,013,984 B | `board-baseline.md` 的 `/proc/meminfo` |
| 已用 | 317,128 B | 同上 |
| 峰值 | 428,024 B | 同上 |
| AXI SRAM | 512 KB @ `0x24000000` | `scripts/qspi_flash.ld` |
| SRAM4 | 64 KB @ `0x38000000` | 同上 |
| SDRAM | 8 MB @ `0xd0000000`，堆偏移 2 MB | `board.h` `BOARD_SDRAM2_SIZE`/`_HEAP_OFFSET` |
| DTCM | 128 KB @ `0x20000000` | `memory.ld`（本配置未进堆） |

### 音频占用估算（16 kHz、16 bit、单声道、15 秒上限）

| 项目 | 估算 | 说明 |
|---|---|---|
| 原始录音 | 480,000 B | 16000 × 2 × 15 |
| DMA 双缓冲 | 2 × 4096 B = 8192 B | 缓冲大小待 BVO-03 按中断频率定 |
| WAV 封装额外 | 约 44 B | `pcm_decode` 的头部 |
| 播放缓冲（TTS 回复片段） | 按 30 秒音频上限约 960 KB，建议按段落流式送入 | 实际以 TTS 返回格式为准，见 BVO-06 |
| 采集线程栈 | 8192 B | 与现有 `VG_HMI_SCAN_STACKSIZE` 同级 |
| 音频播放线程栈 | 8192 B | 同上 |
| SAI 驱动内部 | 按 F7 蓝本的 `CONFIG_STM32F7_SAI_MAXINFLIGHT` 16 个容器估，约若干 KB | BVO-03a 定具体值 |

余量判断：峰值 428 KB 加上录音 480 KB 加缓冲与栈，总量仍在 7.0 MB 的 15% 以内，余量充足。真正的限制不在总量，而在**位置**：

- DTCM 与 ITCM 不能被 DMA1/DMA2 访问（`stm32_dma.c` 的 `dmacapable` 明确拒绝），DMA 缓冲不能放这里。
- 选择 1（SAI4 + BDMA）：DMA 直接读写的缓冲必须在 SRAM4。整段录音不受此限，主核可以边抽取边把 PCM 搬到 AXI SRAM 或 SDRAM 累积。
- 选择 2（SAI1 + DMA1/DMA2）：DMA 缓冲可放 AXI SRAM 或 SDRAM，位置约束更松。

按选择 1 估算 SRAM4 占用：DMA 双缓冲 2 × 4096 B = 8 KB，加上抽取滤波的中间状态与系数表，总量控制在 16 KB 以内比较稳妥。SRAM4 实际可用量 = 64 KB 减去 `.sram4` 段占用，具体余量由 BVO-03 实测。

### cache 与 DMA 一致性

- 本板 `CONFIG_ARMV7M_DCACHE=y` 且为写透（`_WRITETHROUGH=y`），`up_enable_dcache()` 在 `stm32_start.c:266` 无条件调用。
- NuttX H7 的 DMA 层不做 cache 维护，`stm32_dma.h` 的回调注释明确要求客户端在 RX 完成后自行 invalidate。
- 因此 SAI 驱动必须在 RX 完成回调里调用 `up_invalidate_dcache`，在 TX 启动前确保数据已写出。缓冲地址按 `ARMV7M_DCACHE_LINESIZE` 对齐。
- 写透模式降低了脏数据风险，但不取消对齐与 invalidate 要求，因为读方向仍可能读到过期行。
- 需要注意，内核对齐检查本身在写透模式下不生效：`stm32_dma.c:2107-2125` 的 `#if defined(CONFIG_ARMV7M_DCACHE) && !defined(CONFIG_ARMV7M_DCACHE_WRITETHROUGH)` 在本板配置下为假，`dmacapable` 不会因为未对齐而拒绝。对齐只能靠驱动自己保证，不能指望内核报错。

## 8. 时钟方案

### 现状

- HSE 25 MHz。PLL1 供 SYSCLK 400 MHz。PLL3 供 LCD。
- PLL2 已在 `board.h` 启用：M=5、N=160、P=2，VCO 800 MHz，P 输出 400 MHz。目前没有实际消费者（ADC 未启用，SPI4/5/6 未启用）。

### 问题

`boards/arm/stm32h7/stm32h750b-dk/include/board.h:122-128`：

```c
#define STM32_PLLCFG_PLL2Q       4
#define STM32_PLLCFG_PLL2R       4

#define STM32_VCO2_FREQUENCY     ((STM32_HSE_FREQUENCY / 5) * 160)
#define STM32_PLL2P_FREQUENCY    (STM32_VCO2_FREQUENCY / 2)
#define STM32_PLL2Q_FREQUENCY
#define STM32_PLL2R_FREQUENCY
```

三个问题：

1. `STM32_PLLCFG_PLL2Q` 是裸 `4`，不是 `RCC_PLL2DIVR_Q2(4)`。对比同文件 PLL1 的写法（`RCC_PLL1DIVR_Q1(4)`）可以看出这是遗漏。`stm32h7x3xx_rcc.c:836` 会把它直接或运算进 `PLL2DIVR` 寄存器，若 `RCC_PLL2DIVR_Q2(n)` 不是简单移位，这个值会写错。
2. `STM32_PLL2Q_FREQUENCY` 与 `STM32_PLL2R_FREQUENCY` 定义为空。`arch/arm/src/stm32h7/stm32_spi.c:199,212` 在 SPI4/5/6 启用时会用 `STM32_PLL2Q_FREQUENCY` 计算内核时钟，空定义导致编译失败。
3. `STM32_PLLCFG_PLL2CFG` 只置了 `RCC_PLLCFGR_DIVP2EN`，没有 `DIVQ2EN`/`DIVR2EN`。若 SAI 需要 PLL2Q 或 PLL2R 作核时钟，必须先打开对应的使能位。

### 对 SAI 的影响

H7 的 SAI 核时钟来自 `RCC_D2CCIP1R_SAI23SEL`（SAI2/3）与 `RCC_D3CCIPR_SAI4ASEL`（SAI4），可选 PLL1/2/3 或外部时钟（`stm32h7x3xx_rcc.h:519-525, 649-654`）。现有 `board.h` 已启用 PLL2 且未分配用途，是 SAI 核时钟的自然选择。

需要注意 SAI 的 MCLK 要覆盖采样率分频。按 ST BSP 对 SAI4 的算法（`MX_SAI4_ClockConfig`，`bsp/stm32h750b_discovery_audio.c:496-528`），48k 族用 PLL2N=344、PLL2P=7（PLL2M=25），44.1k 族用 N=429、P=38。这是 BSP 在 25 MHz HSE 下算出的参数，可以直接作为 BVO-03a 的参考，但要重新验证与本板当前 PLL2 设置（M=5、N=160）是否兼容。两者 M 值不同（5 对 25），说明 BSP 的方案需要改 `board.h` 才能用。

### 结论

时钟需要在 BVO-03a 里处理，范围包括：修正 `board.h` 的 PLL2 Q/R 定义、按选定采样率重算 PLL2 参数、选择 SAI23SEL/SAI4ASEL 源。这会改动板级头文件，属于 nuttx 仓改动，要在功能分支提交。

## 9. 接口参数合同

本节固化给 BVO-02 至 BVO-05 使用。凡标为待定的，由对应任务实测后回填本文。

| 项目 | 取值或规则 | 依据 | 最终确认 |
|---|---|---|---|
| 采集设备节点 | `/dev/audio/pcm0c` | `packages/ai_agent/include/agent_config.h:374`；`audio_get_path` 默认加 `/dev/audio` 前缀（`nuttx/audio/audio.c:1793-1800`） | BVO-03 |
| 播放设备节点 | `/dev/audio/pcm0p` | `agent_config.h:377` | BVO-04 |
| 采样格式 | 16 kHz、16 bit、小端、单声道 | `voice_asr.h` 的 PCM 合同；BSP 支持 8k-96k | BVO-03/04 |
| 单次录音上限 | 15 秒，代码强制截断 | 执行方案第 2 节 | BVO-02/03 |
| 原始音频上限 | 480,000 B | 16000 × 2 × 15 | BVO-03 |
| 录音 PCM 累积位置 | AXI SRAM 或 SDRAM；**禁止 DTCM/ITCM** | `stm32_dma.c` 的 `dmacapable` 拒绝 DTCM/ITCM | BVO-03 |
| 采集 DMA 缓冲位置 | 选择 1 必须在 SRAM4；选择 2 可放 AXI SRAM/SDRAM | BDMA 仅可达 D3 与 SRAM4（`stm32_dma.c:2146-2160`） | BVO-03a |
| DMA 缓冲对齐 | 按 `ARMV7M_DCACHE_LINESIZE` 对齐 | 驱动自身要求。内核对齐检查在写透模式下被条件编译掉（`stm32_dma.c:2107-2125` 的 `#if` 排除 `_WRITETHROUGH`），不能靠它兜底 | BVO-03 |
| cache 维护 | RX 完成后 `up_invalidate_dcache`；TX 前确保数据写出 | `stm32_dma.h` 回调注释 | BVO-03/04 |
| 播放音频格式 | WAV（`AUDIO_FMT_PCM`）；不使用 MP3 | `nuttx/audio/pcm_decode.c` 只解 WAV；`apps/audioutils/libmad/Makefile` 需构建期下载源码 | BVO-06/11 |
| PDM 抽取 | 64 倍抽取，用 CMSIS-DSP `arm_fir_decimate_*`（Apache-2.0）按需自建系数 | 本地无 ST PDM2PCM 库；`apps/mlearning/cmsis/CMSIS_5/CMSIS/DSP` 有源码 | BVO-03 |
| 输出通路 | 只开 HPOUT1，`spk_out` 保持关闭 | M-02 与 M-03：HPOUT1 与 SPKOUT 经跳线并联到同一插座 | BVO-04 |
| 录放互斥 | 同一时刻只有一个音频会话，先停止再切换 | 执行方案第 2 节 | BVO-05 |
| 播放音量初值 | 保守初值，实测后固化（BSP 默认 250/1000） | BSP `WM8994_INITVOLUME` | BVO-04 |
| 按键事件源 | 优先 `INPUT_BUTTONS` + `board_button_irq`；若成本过高退到 board 内 `stm32_gpiosetevent` 直接投递 | `drivers/input/button_lower.c:212-217` | BVO-02 |
| 按键极性 | 按下为高（PULLDOWN + IT_RISING） | M-05 已实测确认 | BVO-02 |
| SAI 核时钟源 | PLL2，参数在 BVO-03a 重算 | 第 8 节 | BVO-03a |

仍需后续任务实测才能定：可用采样率档位、播放音量安全初值、SRAM4 实际可用余量、PDM 抽取滤波系数与增益标定。

## 10. 实测结果

完整清单与判定方法见 [pre-measurement-checklist.md](pre-measurement-checklist.md)。用户在 2026-10-21 完成全部 7 项：

| 编号 | 事项 | 实测结果 | 影响 |
|---|---|---|---|
| M-01 | 麦克风通路 | `MP34DT01TR` CLK PE5、DOUT PE4，走 SAI4 PDM 直连 | 确定采集走 SAI4，推翻初稿的 WM8994 路线 |
| M-02 | 绿色插座驱动源 | HPOUT1 | 播放复用现有驱动路径 |
| M-03 | 两个 2x1 跳线 | `SPKOUTLN`-`LP`、`SPKOUTRN`-`RP` 短接 | 暴露 HPOUT1 与 SPKOUT 并联问题，见第 6 节 |
| M-04 | 蓝色插座输入 | `IN1LN` / `IN1RN`，与 MICBIAS1 无关 | 不影响本轮 |
| M-05 | PC13 按下电平 | 按下为高 | 与 BSP 一致，BVO-02 可直接用上升沿 |
| M-06 | MICBIAS1 | 不需要软件使能 | 采集初始化可省一步 |
| M-07 | 板卡版本 | B-01 | 与原理图文件名一致，本文页码引用有效 |

实测推翻了初稿两处判断：一是采集路线（初稿推荐经 WM8994，实测为 SAI4 直连），二是 Line Out 的存在性（实测为 HPOUT1 兼作扬声器输出，无独立线路输出）。两处已在第 5、6 节更正，初稿结论不再作为实施依据。

**证据强度**：本轮 7 项结论来自用户口头实测，未附照片或仪器读数。对确定路线和接口参数足够，但不构成板级验收证据。若 BVO-03 或 BVO-04 的实测结果与此处矛盾，应优先复核本节结论。

## 11. 新增任务 BVO-03a（STM32H7 SAI 驱动移植）

执行方案里 BVO-03 与 BVO-04 假定 SAI 驱动已经存在，实际 nuttx 树里没有。本项单列，作为两者的共同前置。

范围限定在 `../nuttx/arch/arm/src/stm32h7`，不改 `drivers/audio/`：

- 新增 `stm32_sai.c`、`stm32_sai.h`、`hardware/stm32_sai.h`，以 `arch/arm/src/stm32f7/` 的三个同名文件为蓝本。
- 把 `arch/arm/src/stm32h7/Kconfig:685` 的空壳 `STM32H7_SAI` 补成可用配置，加 `STM32H7_SAI1_A/B`、`SAI2_A/B`，按 F7 模式 `select AUDIO`/`I2S`/`SCHED_HPWORK`。若走选择 1，还要加 SAI4 与 BDMA 的组合项。
- `arch/arm/src/stm32h7/Make.defs` 增加源文件。
- 不改动现有 `stm32_dma.c`。

移植必须处理的改造点：

1. RCC。H7 走 `RCC_D2CCIP1R_SAI1SEL`/`SAI23SEL` 与 `RCC_D3CCIPR_SAI4ASEL`，使能位在 `RCC_APB2ENR`/`RCC_APB4ENR`，与 F7 的 PLLI2S 方案不同。
2. DMA。F7 是 `stm32_dmasetup(handle, paddr, maddr, nbytes, flags)`，H7 是 `stm32_dmasetup(handle, stm32_dmacfg_t *)` 结构体形式；通道分配改成 `stm32_dmachannel(DMAMAP_*)`。
3. cache。F7 驱动完全没有 cache 维护，H7 要在 RX 完成回调补 `up_invalidate_dcache`。
4. master/slave 语义拆分。F7 把 `SAI_CR1_SYNCEN` 当主从开关用（`sai_receive` 里 `priv->syncen ? SLAVE_RX : MASTER_RX`，`stm32f7/stm32_sai.c:1242`），而该值来自 `CONFIG_STM32F7_SAIx_x_SYNC_WITH_y`，把同步与主从混在一个宏里。H7 版要拆成两个独立选项。
5. PDM。SAI4 需要新增 `CR1.PDMEN`、`CR4.MICPAIRNBR` 与 PDM 时钟配置。F7 与 H7 的寄存器头都没有这些位定义，要按参考手册新增。BSP 的 `MX_SAI4_Block_A_Init`（`bsp/stm32h750b_discovery_audio.c:1800-1802`）显示了 `PdmInit.Activation=ENABLE`、`MicPairsNbr=2`、`ClockEnable=SAI_PDM_CLOCK2_ENABLE` 的实际取值。
6. 板级 API 保持 `stm32_sai_initialize(int intf, sampleratecb)` 形式，intf 编号在 H7 头文件重定义。

完成条件：

- 主机侧：Kconfig 能选中，编译通过不新增告警，`nm` 可见 `stm32_sai_initialize`，不影响其它板配置。
- 板端：TX 路径由 BVO-04 用已知测试音验证，RX 路径由 BVO-03 验证。BVO-03a 本身不单独声称音频可用。

## 12. 未决风险与不做什么

风险：

1. **BDMA 依赖 EXPERIMENTAL**。选择 1 需要打开 `CONFIG_STM32H7_BDMA`，该选项被标记为实验性质。BVO-03a 要先验证它在 SAI4 上稳定可用，否则退到选择 2（SAI1）。
2. **SAI1 的 PDM 能力未核实**。选择 2 依赖 SAI1 支持 PDM 模式，这一点没有在参考手册里确认过。SAI4 有 PDM 模式是 BSP 的既成事实，SAI1 不一定相同。
3. **PDM 抽取滤波要从零设计**。64 倍抽取的系数、位序、增益标定都没有现成实现可参考，需要按 MP34DT01TR 数据手册与 SAI4 的 PDM 位流格式自行推导与实测校准。这是 BVO-03 里工作量最不确定的一块。
4. **HPOUT1 与 SPKOUT 并联**。只要跳线保留，两路输出就挂在同一负载上。软件必须保证 `spk_out` 关闭，且这一约束要写进 BVO-04 验收与手册；若将来有人误开 SPKOUT，硬件层面没有保护。
5. `board.h` 的 PLL2 定义缺陷会同时影响 SAI 与未来任何 SPI4/5/6 用途，修正时要一并验证不影响 LCD 与网络。
6. BVO-03a 是新增任务，执行方案的依赖关系需要相应更新；本轮只在 `maintenance-todo.md` 记录，不改执行方案原文。

本轮不做：

- 不写任何驱动代码，不修改 nuttx、apps、packages 的任何文件。
- 不做 BVO-02 至 BVO-05 的实现。
- 不改 `scripts/configs/velaguard-lvgl.defconfig`。
- 不修改 `AGENTS.md`、ADR 0009、执行方案。
- 不动 `maintenance-todo.md` 里 `OUT-01` 等既有表述。
- 不声称任何编译、烧录或板端运行结果。

## 13. 版本记录

核对时间：2026-10-21，Asia/Shanghai。

| 仓库 | 分支 | HEAD | 未提交改动 |
|---|---|---|---|
| `contest2026_004_TeamFalcons` | `velaguard/maintenance-stability` | `6b9ec87` | 有（`AGENTS.md`、推进方案删除、维护证据日志、AI 日志目录、`connect_info`） |
| `../nuttx` | `velaguard/eth-mii-stm32h750b-dk` | `1b5e71d2f87` | 有（`velaguard-lvgl/defconfig`、`arch/arm/src/armv7-m/vg_diag.h`） |
| `../apps` | `velaguard/netinit-esp8266` | `0ea954b79` | 无 |
| `../packages` | `velaguard/agent-hmi-stack-heap-fix` | `9276188` | 无 |

资料包路径：`/mnt/f/Project/Embeded/H750B-DK/BOARD INFO/H750B-DK`（WSL 挂载）。ST BSP 版本 v3.3.4，Board ID MB1381B。

本轮未构建、未烧录、未在板端执行任何命令；板端基线数字引自 2026-10-18 的既有记录，不代表当前固件。
