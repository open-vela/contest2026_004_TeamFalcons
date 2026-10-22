# CodeBuddy 会话续接：栈水位与握手硬故障

2026-10-22，在 WSL 读取最近 CodeBuddy 会话 `01a16228-5c03-7f87-b09b-c4fb4ade3b52` 后继续。保留原授权范围 BVO-07 至 BVO-14，不推送。

## 第一阶段：真实栈水位

将未完成的手工分配、填色栈诊断改为 NuttX 原生 `CONFIG_STACK_COLORATION` 和 `up_check_tcbstack`。诊断使用 TCB 引用，并与线程启动/停止互斥；报告的 size 排除 TLS。工作线程仍为 48 KB。新增 `vgvoice round stack`，不改变录音或工具权限。

- `build-stack.log`：从本仓 `bash scripts/build.sh` 构建成功。
- `host-tests.log`：主机测试通过；该结果不证明板端握手安全。
- `firmware-stack.json`、`flash-stack.log`：实际烧录的镜像摘要、多仓 HEAD 和来源文件摘要。
- `baseline.log`：运行版本 22:41:07，启动空闲栈 used=592、size=49128；空闲堆 used=850632、free=5987136、maxfree=5795432。
- `stack-stress.json` / `stack-stress.log`：最多六组连续替换；第四至第五组间再次硬故障，后续命令无应答。不是恢复成功。诊断和合成按键不能替代人工操作。

## 更正缺陷 22 的旧结论

48 KB 栈仍发生 `CFSR=01000000`，现场高水位 **24680**，剩余 **24448**。因此原先根据增加栈后 24 次未复现推断栈溢出、并宣称已修复，证据不足；本次重新打开缺陷 22。48 KB 暂时保留，不能把它当作根因修复。

当前镜像的符号和反汇编表明：

- PC `0x9007e296`：`mbedtls_asn1_get_len` 的 `ldrh.w r3, [r3, #1]`。
- LR `0x9007e4af`：`mbedtls_asn1_get_bitstring_null`；调用栈是证书 DER 解析。
- 异常帧中的 r3=`0xd0279478`，实际半字读地址为 `0xd0279479`，位于 SDRAM 且未对齐。
- `fault-mpu-registers.log`：CCR=`0x00070200`，UNALIGN_TRP 未设置；MPU CTRL=`5`，region 0 仅映射 QSPI（RBAR=`0x90000000`、RASR=`0x05020035`）。源码未映射 SDRAM。

下一步核对 SDRAM 的内存属性；尚未把此次现象解释为内存破坏或栈耗尽。

## 硬件与架构依据

本地官方原理图 `mb1381-h750xb-b01-schematic.pdf` 第 9 页：U7 为 MT48LC4M32B2B5；提取结果见 `schematic-sdram.txt`。本地 ST BSP `stm32h750b_discovery_sdram.h` 定义 SDRAM 地址 `0xD0000000`；BSP 中芯片容量不能直接套用，现有 NuttX 板级只使用低 16 位总线和 8 MB 地址空间。

[Arm/Keil 的 Cortex-M7 SDRAM 未对齐访问说明](https://www.keil.com/support/docs/3777%20%20.htm)解释了默认 Device 内存即使 CCR 未开启 UNALIGN_TRP 仍要求自然对齐，推荐用 MPU 设为 Normal 内存。[ST AN4838](https://www.st.com/content/st_com/en/technical-documents/an4838-introduction-to-memory-protection-unit-management-on-stm32-mcus-stmicroelectronics_asset/chunk-001-topic-an4838-overview.html)说明 MPU 内存属性与区域优先级。这里用原理图和 BSP 确认板上器件及地址，用架构资料判断内存类型，尚须板测。

阶段 A / B 继续保持未通过；真人录音、听音、触摸与断电验收没有执行。

## 第二阶段：修复 SDRAM 内存属性

NuttX 提交 `6f11173642c` 在 H750B-DK 且启用 FMC 时，将板级声明的 8 MB SDRAM 设置为 Normal、non-cacheable、shareable、XN；不改时钟、引脚、QSPI 或 DMA 行为。保持不可缓存，避免引入 LTDC/DMA 缓存一致性要求。该地址此前使用默认 Device 属性，解释了证书中普通字节数组的未对齐访问为何触发异常。

- `build-mpu.log` / `flash-mpu.log` / `firmware-mpu.json`：从竞赛仓入口构建并烧录，保存摘要及源码来源。
- `fixed-mpu-registers.log`：映射后的 MPU 实测；新增 region 1，RBAR=`0xd0000000`、RASR=`0x110c002d`（Normal、8 MB、不可缓存、XN）。
- `baseline-mpu.json` / `baseline-mpu.log`：本次启动基线。
- `mpu-stress.log`：同一六组替换测试，无 HARDFAULT；最高栈 used=24680 / size=49128。ASR 有 HTTP 200 及明确的取消结果；这六组不等于六次完整成功问答。
- `mpu-recovery.json` / `mpu-recovery.log`：不复位，结束合成轮次后再次 ASR 成功（HTTP 200，15920 ms），存储目录可列举。仅保留合成样本 `q1.wav` 和 `pref.txt`。

本次状态是根因修复及有限回归通过，尚不能声称全部网络/存储稳定性问题消失。新 TCP 统计发现三条 CLOSED、ref=0 的记录持续存在，接下来单独核查回收。`/proc/net/tcp` 需要 CONFIG_NET_STATISTICS；原先缺文件并非没有挂载 procfs。
