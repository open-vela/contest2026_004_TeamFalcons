# 耳机无声后的时钟与初始化修复

状态：BVO-04 仍未完成。用户在旧固件低音量提示音、yes 后反馈听不到，只有一点点杂音。此反馈推翻了把传输完成当作可听输出的可能推断。更新固件后已再次低音量播放，新的听音反馈尚待用户确认；严格连播仍检出欠载。

## 现象及依据

旧 HEX cf25e38f…：编解码器 ID=0x8994，但播放期间非 ID 寄存器读回零，见 before-codec.log、before-active-read.log。停止后可读，但 PM1=0x0003、PM3=0、CHARGE_PUMP1=0x1f25、ANA_HP1=0、OUTPUT_MIXER1=0，说明耳机输入级、充电泵及输出级、路由未完成使能，见 before-idle.log。ID 正确不代表播放电路已初始化。

[WM8994 Rev4.6](https://statics.cirrus.com/pubs/proDatasheet/WM8994_Rev4.6.pdf) 第201页 Control Interface Clocking 规定：选定 AIF1CLK 作 SYSCLK 且 AIF1CLK_ENA=1 时，需要有效输入时钟；时钟停止前必须清除 AIF1CLK_ENA。NuttX 原 H7 路径只在 DMA 期间使能 SAI，不能为 codec 启动及 DC Servo/Write Sequencer 提供完整时钟生命周期。STM32F7 参考驱动则在初始化末尾就开启 SAI；不能直接把这个始终开启的行为复制到 PDM。

再次核对本地 MB1381 B-01 第13页以及板型 BSP。保留绿色 CN9→HPOUT1，不改变连接或使能 SPKOUT。BSP SAI2 输出 GPIO 使用 very-high speed；原 H7 驱动直接采用 pinmap 宏，未设置速度，落到 low speed。本次 SAI2A 的 PI4/5/6/7 按 BSP 设置为 GPIO_SPEED_100MHz。初始化时钟生命周期和 GPIO 驱动速度一起更正，本轮没有仪器测量或分别撤销单项的 A/B，不能断言全部现象只由其中一项造成。

## 修改

- 给 WM8994 lower 增加可选 mclk_enable 回调；本板经 STM32H7 SAI 显式取得、释放时钟。仅支持非 PDM、异步 master TX、NOMCK=0；有活动或待发送 DMA 时拒绝切换，不对所有 PCM/从模式/同步模式开启时钟。
- 初始化时先配置分频并提供 MCLK，再软件复位和初始化耳机电路；检查 PM1、PM3、ANA_HP1、左右混音路由读回。错误返回初始化失败，不注册成功的音频设备。
- 空闲时 AIF1CLK_ENA=0；播放前恢复 MCLK，再开 AIF1CLK 和解除耳机静音；最后一块 DMA 排空后保留 MCLK，待 codec 静音并关闭 AIF1CLK 后释放。停止后 SAIEN=0，无活动 DMA。设备仍保留静音的模拟耳机准备状态和持久 DMA 句柄，不宣称模拟电路完全断电或每次释放 DMA 句柄。
- Write Sequencer 轮询上限50次、每次20ms（另有单次 I2C 等待），替代无上限循环。初始化读回错误、时钟启停错误经初始化/结束路径返回；未故障注入覆盖这些新失败路径。
- 初始时钟保持期间可能留下没有音频数据的 idle underrun 标志，只在新流提交前清除；不在流内各块之间清除，继续保留严格欠载检测。
- 持有时钟时拒绝更改不同采样率或字宽，避免写受 SAIEN 保护的字段。已验证 PDM 路径未改初始化、时钟参数或 GPIO。
- `vgaudio codec` 一次命令读取固定 WM8994 寄存器集合，无写探测；`vgaudio sai` 取消空闲48k分频下仍打印16k目标值的误导，频率明确为推算。

本次未重配 PLL2、PLL3。16k测试 WAV仍为16位小端立体声，L=R，1秒；MCKDIV=12。沿用 HSE标称25MHz，PLL2P=344000000/7 Hz，MCLK=4095238.095238 Hz，BCLK=511904.761905 Hz，FS=15997.0238095 Hz，误差-186.011905ppm。没有频率仪器实测，寄存器正常读回只支持控制时钟可用，不能替代频率测量。

## 本次板测

最终 HEX e45bd6d1…，见 check.log。保持 NxPlayer volume15，即 raw9，约-48dB；文件幅度及生成方式不变，没有提高音量。

| 检查 | 结果 |
|---|---|
| 初始化及空闲 | ID8994；PM1=0303，PM3=0030，充电泵9f25，ANA_HP1=00ee，左右路由0001；HPOUT音量000a静音，AIF1CLK=0 |
| 播放期间 | PM1等正确读回；HPOUT0049解除静音；AIF1CLK=1；RATE=0033，AIF1=4010，DAC1音量00c0、FILTER1=0010；SAI最后配置分频12 |
| 首次提示音、随后yes | 每文件64000字节、8次DMA完成；累计16/128000，DMA errors/timeouts、SAI underruns/clockerrors均0；这只是软件路径证据，新的人工听音未确认 |
| 严格重复回归 | 基线16次/128000字节后，正常轮0、1、2通过；轮3（第4文件，speech）计数48/384000，underruns=3、SR=40009；脚本立即失败，未进入10次停止/重启，见accept-failed.log |
| 失败后收尾 | 不复位；stop、quit返回NSH，SAIEN=0，AIF1CLK=0，耳机静音；无playthread/WM8994线程遗留，见cleanup.log |
| TX非法请求 | 无效接口、空长度、奇数字节、超容量、非法偏移20次检查 result=0。新时钟回调/读回超时未故障注入，不据此声称完整错误路径通过 |
| PDM保持 | 失败收尾后16384字节×3轮仍返回，RMS .0109/.0096/.0092；不代表PDM→PCM或连续录音完成 |
| 存储/网络 | 失败后512字节写回成功，1670ms；RJ45 link=1、ping_ok=1、MQTT up；WiFi assoc=0 |
| Modbus | slave1 total10→16，全部timeout，只证明采集任务/计数继续，无在线从站读数通过证据 |
| 堆/任务 | 两文件前used229104、后229256，maxfree6291440相同；严格回归后312344，期间Agent等后台线程初始化，差值未归因，不能宣称资源回归通过 |
| LVGL/触摸 | HMI任务存在；人眼显示和实际触摸切页尚未确认 |

中间 HEX 9cb5e654… 也恢复了正确耳机寄存器，并播放一组提示音/yes，但累计欠载7；仅作为过程记录，不用于最终通过。其原始命令见 intermediate-listen.log。

欠载仍需继续定位；计数3为3个DMA完成快照带粘滞位，不是3个独立事件。不能将与Agent初始化的时间重叠解释为已证明因果。FEIF原始状态仍为31，相关限制沿用上级记录。两次软件烧录复位属于更新固件；严格失败后没有用复位恢复来代替停止验收。一次中间诊断固定时间发送过快，0x200与quit发生输入串接，原始记录保留；最终check/cleanup等待命令提示符且有上限。

## 最后一次重播与串口输入失败

09:06的relisten.log中，speech命令只回显到`play /mnt/emmc/bv`，20秒未返回提示符。发送空回车后，板端报告`File /mnt/emmc/bv not found`，接着stop正常返回，见timeout-stop.log。没有复位；该现象证明命令输入不完整，不能记作语音播放成功，也不足以断定固件死锁或归因于特定中断/环境。

用户随后要求重复播放。09:07改为每字符间隔5ms发送完整命令，两条文件路径均完整回显，退出正常，见paced-relisten.log及同名ps1。末次计数dma_done=72、txbytes=576000、errors=0、timeouts=0，但累计underruns已到9；不把这组重播计作无欠载通过。新的实际听音仍待用户反馈。

## 构建、来源、提交

在竞赛仓根目录两次执行 `bash scripts/build.sh`。构建前两份libapps.a摘要一致；更新后仍逐字节一致，vgaudio成员只有1个，无须清理archive。见archives.json。未修改vendor、点表、密钥配置或其他任务改动，没有推送。

- NuttX基线8291a2c2fc3，本次源码提交 **3d6641a5fab**。
- 竞赛仓基线22268e5，本次vgaudio与证据提交见本文件所在git提交；apps仍为0ea954b79。
- 最终构建在上述提交前执行，来源为这些基线加sources.sha256所列改动，源码与本次提交相同；没有声称提交后重建。
- 实际烧录 `.debug/nuttx.hex`，SHA256 **e45bd6d1f367f0c90b4f768968ba76cafd5e9b9f90ba6d3efe0b4b8df61149cc**。
- BIN SHA256 **bcffa876386b2fc02887b9745fd37fc2a6754a8ac4b199a354dad879994409fc**。
- 烧录日志flash.log；配置摘要config.sha256；未归档可能含既有配置凭据的完整.config或镜像正文。
- 日志校验305文件、66642事件，1条历史orphan警告，工具PASSED WITH WARNINGS；未改写AI日志或manifest。

BVO-04保持进行中：需要用户确认新固件是否可听、继续处理严格连播欠载，在同一最终固件完成20次正常与10次停止重启以及完整并行回归。BVO-03继续未完成。
