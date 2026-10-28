# BVO-04 音频播放验证

状态：未完成；最终严格欠载复验和提交记录见 [2026-10-22](../../2026-10-22/bvo-04/README.md)。BVO-03 继续未完成。本轮只验证已知 PCM WAV 输出，不做录音回放闭环。

## 硬件与来源

核查 MB1381 H750XB B-01 原理图第 13 页 Audio：绿色 CN9 的 6、4 脚分别直连 U15 WM8994 的 HPOUT1L、HPOUT1R；3 脚为 HPOUT1FB/地。C113/C114 是经 R182/R183 接地的旁路支路，不是串联隔直电容。CN18 和 CN17 分别为独立差分 SPK 左、右输出。原理图没有显示它们与 CN9 并联；旧 BVO-01 据跳线口述推断并联的结论不足以成立。用户此前实测可支持 HPOUT1 通路及跳线端子识别，不能证明两个网络间的连通关系。

用户已授权绿色插座 + HPOUT1 + 极低初始音量。编解码器 SPKOUT 始终关闭。使用高阻抗有源音箱输入或适配的耳机，首测降低音箱增益；HPOUT1 是耳机输出，不称独立 Line Out。

SAI2 block A：PI4 MCLK、PI5 SCK、PI6 SD、PI7 FS，对照本板 BSP `MX_SAI2_Block_A_Init`。I2C4 PD12/PD13，与触摸共用总线，编解码器目标地址为 7 位 0x1a。原理图 CS/ADDR 上拉 R193 DNF，数据手册内部默认下拉，对应 8 位写地址 0x34；不对其他地址写探测。ID 0x8994 已在此前板测读到，本轮更新后再次验证。

## 主时钟语义与计算

直接阅读 ST RM0433 Rev 9（2026-10）§51.4.8，第 2246–2247 页，以及 §51.6.2，第 2273–2274 页：bit 27 保留，bit 19 NOMCK=0 使能主时钟；OSR=0 为 MCLK/fs=256。不得套用其他 STM32 系列的 MCKEN 定义。撤销先前未提交的 bit27 写操作，同时删除错误的公共位定义。BSP 也使用 NoDivider=SAI_MASTERDIVIDER_ENABLE、MckOutput=DISABLE。

HSE 25 MHz / PLL2M 25 × PLL2N 344 / PLL2P 7 = 49,142,857.142857 Hz。
播放 16 kHz、立体声、16 位、两个 16 位时隙：MCKDIV=12、NOMCK=0、OSR=0；MCLK=4,095,238.095238 Hz，FS=15,997.0238095 Hz，BCLK=511,904.761905 Hz；相对标称误差 -186.011905 ppm（-0.0186011905%）。48 kHz 默认分频 4 不代表已按 16 kHz 开始播放。未改 PLL2、PLL3 或系统时钟。没有示波器/逻辑分析仪频率实测，以上为配置推算。

参考原文：
- [ST RM0433](https://www.st.com/resource/en/reference_manual/dm00314099-stm32h742-stm32h743753-and-stm32h750-value-line-advanced-armbased-32bit-mcus-stmicroelectronics.pdf)
- [WM8994 Rev4.6](https://statics.cirrus.com/pubs/proDatasheet/WM8994_Rev4.6.pdf)，控制地址表119、音频接口及 HPOUT1 音量寄存器。

## 测试数据

见 fixtures/README.md 和 SHA256SUMS。两份 WAV 均为 16 kHz、16 bit、小端立体声、L=R、1 秒。测试音为 1 kHz，峰值 2047；语音为 TFLM micro_speech 的 yes_1000ms.wav，幅度除以 4 后复制到左右声道。通过只写 `/mnt/emmc/bvo04/` 的 `vgaudio fixtures` 安装，与编译进镜像的字节一致。无需主机文件服务。

首次构建与修复中发现：高寄存器探针调用误用 uint8_t；SAIEN 误读 bit17（DMAEN），应为 bit16。NxPlayer 原始 tone 数据不含 WAV 头，不适合直接交给当前 PCM WAV 解码器，使用上述文件替代。NxPlayer 控制台 volume 参数是百分比，15 表示 150/1000，不能输入 300 当作 30%。

## 验收记录

尚未完成：可辨语音、人工听音、20 轮正常播放、10 轮主动停止重启、TX 错误路径及并行功能回归。当前不得标记 BVO-04 完成。
