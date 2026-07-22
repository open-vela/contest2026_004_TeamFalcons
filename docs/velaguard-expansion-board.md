# VelaGuard H750B-DK 扩展板方案（Arduino + STMod+ 单板）

| 项 | 内容 |
|----|------|
| 状态 | **终审通过（2026-07-14）**；文档交付（无 KiCad/Gerber） |
| 任务 | `.trellis/tasks/07-14-velaguard-arduino-shield` |
| 主板 | STM32H750B-DK / MB1381B |
| 资料 | `D:\Study\Embeded\openvela\report\H750B-DK`（原理图、pinmux、PnP） |
| 关联固件 | UART7 RS485：`.trellis/tasks/07-13-resolve-usart3-rs485-vcp-conflict/design.md` |
| 固件 pin 源 | `nuttx/.../stm32h750b-dk/include/board.h`：`GPIO_UART7_RX=PA8`，`GPIO_UART7_TX=PB4`；console=`USART3` PB10/PB11 |

## 1. 目标与形态

在 **不破坏** 板载 LCD/Touch/Audio/Ethernet/FDCAN/ST-LINK VCP 的前提下，用 **一块扩展板** 同时对接：

- **Arduino Uno** 连接器 CN2 / CN3 / CN6 / CN7 → RS485、LED、DO
- **STMod+** P1 → ESP-01 备用 Wi-Fi（USART2）

```text
工业传感器 ── RS485 ──► [扩展板 UART7] ── Arduino ──► H750B-DK
云 MQTT 备用 ── Wi-Fi ──► [ESP-01 USART2] ── STMod+ ──► H750B-DK
主网 ── 板载 RJ45 ──────────────────────────────────► H750B-DK
调试 ── ST-LINK VCP (USART3 D0/D1) ─────────────────► 仅 console
```

**决策摘要**

| ID | 决策 |
|----|------|
| D1 | 单板一体（Arduino 母座 + STMod 插针/短排线） |
| D2 | 本仓库交付文档方案，不包含 KiCad/PCB |
| D3 | 改 H750 本体焊桥，STMod 切 USART2 |
| D4 | 第一版含 1 路 DO |
| D5 | DO = 低边 MOSFET/开漏（非干接点继电器） |

## 2. 架构分区

| 分区 | 连接器 | 内容 |
|------|--------|------|
| RS485 | Arduino | 3.3V 收发器、A/B 端子、120Ω 跳线、TVS |
| 指示 | Arduino | PWR、RS485_ACT、STATUS |
| DO1 | Arduino | N-MOSFET 低边 + 端子 |
| ESP-01 | STMod+ | 模组座、独立 LDO、RST/EN |
| 电源 | CN3 + STMod 5V | 3V3 逻辑 / 5V→LDO→3V3_ESP；**不用 VIN** |

## 3. 功能引脚合同（权威）

### 3.1 RS485 — UART7（与固件锁定）

| 信号 | MCU | Arduino | 收发器 | 说明 |
|------|-----|---------|--------|------|
| TX | PB4 | **D10** | DI | UART7_TX |
| RX | PA8 | **D5** | RO | UART7_RX |
| DIR | PK1 | **D4** | DE 与 /RE 短接 | 高=发送；空闲低=接收 |
| VCC | 3V3 | CN3 3V3 | VCC | 非隔离 |
| A/B | — | 螺钉端子 | A/B | SM712 类；两端 120Ω（板端可跳线） |
| GND | GND | CN3 | GND | 竞赛级共地 |

- 固件设备路径目标：`/dev/rs485`（见 07-13 design）。
- 演示波特率：9600 8N1（可配置）。
- **禁止** 使用 D0/D1（USART3 = ST-LINK VCP / console）。

### 3.2 ESP-01 — USART2（STMod）

| 信号 | MCU | STMod | ESP-01 | 说明 |
|------|-----|-------|--------|------|
| MCU TX | **PD5** | P1-2 | RXD | 需 SB16 合 / SB13 断 |
| MCU RX | **PD6** | P1-3 | TXD | 需 SB12 合 / SB11 断 |
| RST | **PH10** | P1-12 | RST | 低复位；上拉 + MCU 可控 |
| EN | **PA3** | P1-14 | CH_PD | 高使能；上拉 + MCU 可控 |
| 3V3 | LDO | — | VCC | 输入取 **P1-15 5V**，Iout 峰值 ≥500mA |
| GND | GND | P1-6/16 | GND | 与数字地单点汇合 |

- 独占串口；**不得** 与 RS485 共 UART。
- 故障只影响云/Wi-Fi，不得拖死采集与 UI（固件约束）。

### 3.3 DO1 — 低边开关

| 项 | 规格 |
|----|------|
| 控制 | **PE6 = D6**，推荐高电平导通 |
| 拓扑 | N-MOSFET 低边；栅极串阻 + 下拉；漏极 = OUT |
| 端子 | OUT、GND；负载接外部 V+（建议 ≤12V）→ 负载 → OUT |
| 保护 | 续流二极管；可选 TVS；演示级约 0.5–1A |
| 共地 | 与 H750 **必须共地** |
| 用途 | 告警灯/蜂鸣器联动；**非 AI 直控**（应用层本地确认） |

### 3.4 LED

| LED | 驱动 | 含义 |
|-----|------|------|
| PWR | 3V3 + 电阻 | 扩展板得电 |
| RS485_ACT | **D2 / PG3** | 发送或活动（固件可选） |
| STATUS | **D3 / PA6** | 通用状态（固件可选） |

## 4. 全量扇出表

### 4.1 Arduino

| Pin | Conn | MCU | 可用性 | VelaGuard |
|-----|------|-----|--------|-----------|
| D0 | CN2-1 | PB11 | **禁用** VCP | NC |
| D1 | CN2-2 | PB10 | **禁用** VCP | NC |
| D2 | CN2-3 | PG3 | 可用 | LED_RS485_ACT |
| D3 | CN2-4 | PA6 | 可用 | LED_STATUS |
| D4 | CN2-5 | PK1 | 可用 | RS485 DIR |
| D5 | CN2-6 | PA8 | 可用 | RS485 RX |
| D6 | CN2-7 | PE6 | 可用 | DO1 |
| D7 | CN2-8 | PI8 | 可用 | 预留 |
| D8 | CN6-1 | PE3 | 可用 | 预留 |
| D9 | CN6-2 | PH15 | 可用 | 预留 |
| D10 | CN6-3 | PB4 | 可用 | RS485 TX |
| D11 | CN6-4 | PB15 | 可用* | SPI 预留（STMod 切 UART 后） |
| D12 | CN6-5 | PI2 | 可用* | SPI 预留 |
| D13 | CN6-6 | PD3 | 可用* | SPI 预留（板载 LD8） |
| D14 | CN6-9 | PD13 | **禁用** I2C4 | NC 外挂主设备 |
| D15 | CN6-10 | PD12 | **禁用** I2C4 | NC 外挂主设备 |
| A0–A3 | CN7 | ADC | 可用 | 模拟预留 |
| A4–A5 | CN7 | ADC | 可用 | 保持 ADC；SB34/36 保持断 |
| 3V3/5V/GND | CN3 | 电源 | 用 | 逻辑 / 参考 |
| IOREF | CN3 | 3V3 | 用 | 电平参考 |
| NRST | CN3 | 复位 | 慎用 | 不进业务复位链 |
| VIN | CN3 | 外部供电 | **不用** | 原理图 VIN 风险 |

\* 出厂 STMod 为 SPI 模式时与 D11–D13 同源；切 USART2 后 Arduino SPI 可独立保留。

### 4.2 STMod+ P1

| P1 | 出厂常见 | VelaGuard 目标 | MCU | 计划 |
|----|----------|----------------|-----|------|
| 1 | NSS PA15 (SB21) | NC | — | 避 JTAG |
| 2 | MOSI PB15 (SB13) | **USART2_TX PD5** | PD5 | ESP RXD |
| 3 | MISO PI2 (SB11) | **USART2_RX PD6** | PD6 | ESP TXD |
| 4 | SCK PD3 (SB10) | 保持或 NC | PD3 | 不用 RTS |
| 5–6 | 3V3/GND | GND 必接；3V3 勿直供 ESP 峰值 | — | |
| 7/10 | I2C4 | **禁用** | PD12/13 | |
| 8/9 | MOSIs/MISOs | 预留 | PI3/PB14 | |
| 11 | INT PH12 | **禁用** | PH12 | FDCAN1_TX |
| 12 | RST PH10 | **ESP RST** | PH10 | |
| 13 | ADC PA4 | 预留 | PA4 | |
| 14 | PWM PA3 | **ESP EN** | PA3 | |
| 15–16 | 5V/GND | LDO 输入 / GND | — | |
| 17 | PH1 | **禁用** | PH1 | OSC 风险 |
| 18–20 | GPIO | 预留 | PI11/PH4/PH8 | |

### 4.3 主板已有、扩展板不重复

Ethernet RJ45、FDCAN1/2（CN10/CN11）、LCD/Touch、Audio、ST-LINK。

## 5. 焊桥：出厂默认 vs VelaGuard 目标

来源：MB1381-B01 Pick-and-Place（`mb1381-manufacturing/.../MB1381-B01_PickandPlace.txt`）。  
**SB_ON** = 出厂贴 0Ω / 导通；**SB_OFF** = 出厂断开。

| 桥 | 出厂 (B01 PnP) | VelaGuard 目标 | 操作 |
|----|----------------|----------------|------|
| SB13 | ON（SPI MOSI） | **OFF** | 去掉 0Ω |
| SB16 | OFF（USART2 TX） | **ON** | 贴 0Ω / 锡桥 |
| SB11 | ON（SPI MISO） | **OFF** | 去掉 0Ω |
| SB12 | OFF（USART2 RX） | **ON** | 贴 0Ω / 锡桥 |
| SB10 | ON（SPI SCK） | ON 可保持 | Arduino D13 SPI 仍通 |
| SB9 | OFF（USART2 RTS） | OFF | 不用 RTS |
| SB21 | ON（NSS/JTDI） | **OFF** | 避 JTAG 冲突 |
| SB19 | OFF（CTS） | OFF | 不用 CTS |
| SB34 | OFF | OFF | A4 不抢 I2C4 |
| SB36 | OFF | OFF | A5 不抢 I2C4 |

**回滚**：恢复上表「出厂」列；拔掉扩展板即可隔离业务外设。

> 改焊桥前请用万用表/显微镜核对丝印；不同板次若与 B01 PnP 不一致，以实物为准并更新本节。

## 6. 电源树

```text
H750 5V (CN3 与/或 STMod P1-15)
  └─→ ESP LDO (≥800mA 推荐) → 3V3_ESP → 仅 ESP-01
        输入/输出电容按 LDO 手册；靠近模组

H750 3V3 (CN3)
  └─→ RS485 收发器、LED、逻辑

外部 V+ (≤12V，可选)
  └─→ 用户负载 → DO1 OUT → MOSFET → GND

VIN → 不连接
```

## 7. 建议 BOM（可替换同规格）

| 功能 | 建议器件级 |
|------|------------|
| RS485 | SP3485 / MAX3485 / THVD1451（3.3V） |
| 总线保护 | SM712 或等效 |
| 终端 | 120Ω + 跳线/开关 |
| ESP LDO | ≥800mA 低压差 LDO（优于仅标 1A 的线性模块空载） |
| Wi-Fi | ESP-01 / ESP-01S + 母座 |
| DO | AO3400 / 2N7002 级 N-MOS（按电流选型）+ 续流二极管 |
| 端子 | 3.5/5.08mm 螺钉 2P（A/B）、2P（DO） |
| 连接器 | Arduino 母座堆叠；STMod 兼容 2×10 2.00mm（主板为 Samtec SQT-110 RA 座） |
| LED | 0805 + 限流电阻 |

## 8. 机械与布局

| 项 | 要求 |
|----|------|
| Arduino | 母座对齐 CN2/CN3/CN6/CN7；高度避开 LCD 边框与用户键 |
| STMod | 主板 P1 为 **直角 2×10 2.00mm** 座（PnP：`CON_SAMTEC_SQT-110-01-F-D-RA`）；扩展板用配套插针或 ≤10cm 软排线 |
| RS485 端子 | 板外沿；A/B 丝印清晰 |
| ESP 天线 | 远离 LCD 金属框与大面积地覆铜遮挡 |
| DO 端子 | 与 RS485 分区；丝印额定 |
| 测试点 | UART7 TX/RX/DIR、USART2 TX/RX、3V3_ESP |

画板前请 **实物测量** 两连接器相对坐标与允许高度（本文不替代 Gerber 尺寸）。

## 9. 原理图分区建议（给画板）

1. `CONN_ARDUINO` — 母座与丝印  
2. `RS485` — 收发器、TVS、终端、A/B  
3. `DO_LED` — MOSFET、LED、端子  
4. `CONN_STMOD` — STMod 接口  
5. `ESP01_PWR` — LDO、ESP 座、RST/EN 阻容  
6. `PWR` — 滤波、反接保护（可选）

## 10. 验收清单

### 10.1 文档/合同（本任务）

- [x] 扇出表含用途/禁用/冲突  
- [x] RS485 = D10/D5/D4，与 07-13 一致  
- [x] ESP = PD5/PD6 + PH10 + PA3 + 独立 LDO  
- [x] DO = D6 低边  
- [x] 焊桥出厂 vs 目标 + 回滚  
- [x] BOM 与布局原则  

### 10.2 硬件装配后（后续）

- [ ] 焊桥按目标修改并记录  
- [ ] 3V3_ESP 空载/发射峰值压降可接受  
- [ ] RS485 环回或对 USB-RS485 Gate B  
- [ ] ESP AT（`AT`）经 USART2  
- [ ] DO 驱动 LED/蜂鸣器  
- [ ] 拔网线后本地采集/UI 不受 ESP 影响（固件）  

### 10.3 软件接口（其他任务）

| 功能 | 合同提示 |
|------|----------|
| RS485 | `/dev/rs485`，DIR 极性高=TX |
| ESP | 独占 USART2 设备节点（编号以实现为准）；RST/EN GPIO |
| DO | GPIO D6；默认安全态 = 关断（低） |
| Console | 保持 USART3 VCP |

## 11. Out of scope / 二期

- KiCad 工程与投板（可另开任务）  
- 隔离 RS485、EMC 认证、强电  
- 干接点继电器、多路 DO  
- 应用层 Modbus/MQTT/AI（既有 issue）  
- 占用 D0/D1 或 I2C4 外挂主设备  

## 12. 终审记录（2026-07-14）

对照：用户 CubeMX 核对 + 本地 pinmux + B01 PnP + openvela `board.h`。

| 检查项 | 结果 |
|--------|------|
| RS485 TX/RX = PB4/PA8 = UART7 | **OK**（与 `board.h` 一致） |
| RS485 DIR = PK1 = D4 | **OK**（pinmux 仅 Arduino，无板载外设） |
| 不占用 D0/D1 USART3 VCP | **OK** |
| ESP USART2 = PD5/PD6 via SB16/SB12 | **OK**（原理图/PnP 路径） |
| 出厂 SPI 模式 → 须改焊桥 | **OK**（已文档化） |
| 不用 I2C4 / PH12 / VIN | **OK** |
| DO/LED 脚无板载功能冲突 | **OK** |
| STMod P1-18 PI11 | **未使用**（NuttX `board.h` 注释写 User 在 PI11，BSP 实测 User 为 PC13；预留脚勿接负载） |

**非阻塞备注（画板/装机时注意，不否定方案）：**

1. **ESP 接线交叉**：MCU PD5(TX)→模组 RXD，PD6(RX)←模组 TXD。  
2. **SB21 出厂 ON**：改 USART2 时务必断开，避免 PA15/JTAG 挂到 P1-1。  
3. **ESP 供电**：必须 5V→独立 LDO，峰值电流与输入电容按模组实测。  
4. **机械**：STMod 为直角座，单板对齐需实物量坐标后再定 Gerber。  
5. **PB4 与 NJTRST**：仅 SWD 时通常无碍；全 JTAG 调试时注意 PB4 复用。

**结论：方案确认可用，作为扩展板设计合同。**

## 13. 参考

- 本地：`STM32H750B-DK_外设与引脚分配.md`、`STM32H750B-DK_pinmux.csv`  
- 本地：MB1381B 原理图 / PnP；MB1280 fanout  
- openvela：`nuttx/boards/arm/stm32h7/stm32h750b-dk/include/board.h`  
- ST：[UM2488](https://www.st.com/resource/en/user_manual/um2488-discovery-kits-with-stm32h745xi-and-stm32h750xb-mcus-stmicroelectronics.pdf)  
- 产品：`VelaGuard_项目手册.md`、`VelaGuard_推进方案.md`  
