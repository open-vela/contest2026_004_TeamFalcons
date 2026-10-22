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

## 第三阶段：TCP / TLS 截止时间与无密钥故障注入

新增 NSH 诊断 `vgvoice netprobe host port /path budget_ms [cancel_ms]`，使用同一个 `vela_https_request_stream`。只发送固定的无效测试凭据和固定测试 JSON，不读 provision，不修改产品端点，不输出响应正文。不是设备端 Agent 工具。

原始传输的 300 ms 握手预算实际在 8050 ms 后才返回，见 `probe-before.log`。修复为：受控调用用非阻塞 TCP 与 TLS，在至多 100 ms 的 poll 片段间检查原 deadline / cancel；socket 只 close 一次，去掉重叠的 shutdown；永久收发错误不再无限当作可重试。普通后台调用仍沿用原有连接流程。

### 结果及证据边界

`probe-after-final.log` 与 `probe-check-final.log`：25 项检查全部通过，均在同次开机中执行：

| 场景 | 次数 | 耗时 |
|---|---:|---|
| 本地正常响应与故障后的下一次成功 | 6 | 500–710 ms |
| 本地 HTTP 401 | 3 | 520–750 ms |
| 本地 HTTP 429 | 3 | 510–770 ms |
| 声明 113 B、实际 13 B 的截断响应 | 3 | 510–750 ms，明确读取失败 |
| TLS 服务不回应，300 ms deadline | 3 | 310–390 ms，timeout |
| TLS 服务不回应，300 ms 后取消 | 3 | 330–510 ms，cancelled |
| 握手后无 HTTP 响应，1000 ms deadline | 3 | 1030–1080 ms，timeout |
| 真实 MiMo 端点 + 固定无效凭据 | 1 | HTTP 401，920 ms |

随后同次开机、真实凭据的 ASR 成功：HTTP 200，5620 ms。这是合成样本 `q1.wav`，没有上传个人录音。未发生复位、硬故障或存储读取失败。没有把本地服务的 429 当成真实服务商限流。

`firmware-deadline-final.json`、`build-deadline-final.log`、`flash-deadline-final.log` 保存实际固件及来源。`scripts/voice_test/tls_fixture.py` 是有次数、时长、线程上限的本地故障服务；证书与临时私钥仅在主机临时目录，不入仓。`nsh_plan.ps1` 每步等待提示符且有超时，避免慢命令与下条命令交错；`check_probe.py` 对命令、错误类型和时限逐条判定。

保留 `probe-check.log` 的首次失败：其中一次恢复探测被故障服务的四个滞留线程阻塞，另一次真实端点返回 400（测试缺少有效格式的 JSON），不能算作通过。服务改为观察连接关闭、及时释放模拟线程，探测改为发送固定合法格式 JSON 后重跑，最终 25 项通过。没有修改失败原始记录。

### 仍未关闭的 BVO-07 缺口

- `getaddrinfo` 仍使用平台阻塞 DNS，默认接收/发送超时为 30 s、重试 3 次。现在返回后会检查预算，但不能中途取消 DNS；不能宣称全链路截止时间已完成。
- 语音 Agent 阶段仍使用普通 `llm_chat_tools` 和 300 s 本地客户端超时；放弃只是释放客户端槽位，尚未给这段物理请求传递语音 deadline。
- 当前受控传输遇到配置了 HTTP CONNECT 代理时明确失败，避免退回固定 30 s 隧道等待。VelaGuard 当前直连路径已测。
- 当前网络及存储的历史异常未复现，不等于已经排除。回收后可见 TIME_WAIT、ref=0 的连接，这是 TCP 正常状态；不能要求表中完全没有记录。

上述都是工程缺口，不再归为需要管理员权限、必须重写密钥或必须人工操作的阻塞。阶段 B 仍未通过，继续作为后续子目标处理。
