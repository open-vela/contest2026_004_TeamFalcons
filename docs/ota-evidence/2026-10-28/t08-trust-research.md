# OTA 第八阶段：TLS、验签与 provisioning 接口核对

日期：2026-10-28。调查基线：竞赛仓 `03ba346`、NuttX `09fcb4d0762`；实现期间的源码行号可能变化，后续改动以对应实现与验收记录为准。范围：本地源码和依赖自带 API 文档，末节追加 ST 官方补证。这里只记录接口与缺口，没有连接真实服务、读取秘密或执行板端安装，也不作为生产 TLS 已通过的证明。

## 结论

OTA 应在竞赛仓内使用独立的严格 TLS 适配层。现有 Agent HTTPS 接口可作为超时与 socket 所有权的参考，但不能直接用于 OTA 信任链。当前构建的随机数后端是明确不适合密码学用途的 XORSHIFT128，生产 OTA TLS 必须默认拒绝，直到真实熵源完成核验。主机真实 TLS 测试、ECDSA 验签、HTTP 下载状态机和故障测试可以先无人值守完成。

## 现有 TLS 的具体行为

1. `packages/ai_agent/src/infra/vela_tls.c:1370-1372` 使用 `MBEDTLS_SSL_VERIFY_OPTIONAL`，其 TLS 上下文没有 CA 证书对象（同文件 `132-146`）。`1384-1386` 设置 hostname，但不能因此推导证书链已被强制验证。mbedTLS 自带文档明确 OPTIONAL 在验证失败时仍继续握手，REQUIRED 在失败时中止（`apps/crypto/mbedtls/mbedtls/include/mbedtls/ssl.h:1943-1966`）。
2. `vela_tls.c:1228-1235` 在系统时间过旧时改写到固定 2026 时间。OTA 应直接返回时间无效，不以固定时间通过证书有效期或 manifest 时间窗。
3. `vela_tls.c:1253-1278` 支持 Agent HTTP 代理；`222-275` 的连接池按 host/port 复用。OTA 不应借用这个池或代理路径，否则 CA、出口和验证模式可能与当前任务不一致。
4. 现有流式接口为 `vela_https_request_stream()`，声明位于 `vela_tls.h:110-140`。它可以借鉴分块消费和 deadline/cancel 的组织方式；下载器还需要先取得响应的 Content-Length、ETag、Content-Range 等元数据后，才能决定追加还是从零写入，不能仅凭流式函数名认定可安全续传。
5. 当前受控 TCP 连接在创建 socket 后设置非阻塞再 connect，使用 poll、SO_ERROR 和 getpeername 验证连接就绪，关闭由单一所有者负责（`vela_tls.c:521-618`）。这是可以参照的资源生命周期；同段没有为 socket 指定 eth0。

## 严格 TLS 的最小适配

- 在客户端上下文中加载已配置 CA，使用 `mbedtls_ssl_conf_ca_chain()`、`MBEDTLS_SSL_VERIFY_REQUIRED`、`mbedtls_ssl_set_hostname()`；握手成功后检查 `mbedtls_ssl_get_verify_result()==0`。hostname API 同时配置证书匹配目标和 SNI；verify result 为非零或不可得时拒绝。对应 API 文档位于 `ssl.h:3293`、`3765-3784`、`4475-4487`。
- CA 和签名公钥缺失时拒绝；不提供未知 CA 回退、OPTIONAL 模式、证书错误忽略或固定时间补救。生产信任材料与测试材料必须分别提供。测试私钥、CA 和凭据放 `.debug` 等未提交目录，生产构建不能依赖测试生成物。
- 用 POSIX socket 直连，创建后、connect 前执行 `setsockopt(fd,SOL_SOCKET,SO_BINDTODEVICE,"eth0",sizeof("eth0"))`，失败即关闭并拒绝。NuttX 实现会按设备名查找并绑定连接，找不到设备返回 ENODEV（`nuttx/net/socket/setsockopt.c:184-238`）；本次查看的 `.config` 设置 `CONFIG_NET_BINDTODEVICE=y`。
- 检查真实 RJ45 link/IP，拒绝 Wi-Fi 出口；不使用 `vg_net_mgr_inject` 的人工覆盖作为生产准入证明。`vg_net_mgr_status()` 是锁保护的逻辑状态快照（`app/velaguard/vg_net_mgr.h:15-50`），其状态也可供显示，但 socket 仍需实际绑定。DNS 的出口约束和断线后的 socket 行为还需联网板验，不能以 bind 宏或正常遥测替代。
- 给 DNS/TCP/TLS/接收各阶段执行总期限和空闲期限，保持有界重试。URL/query/用户信息/凭据不得进入错误字符串或请求转储。采用有限错误码上报，3xx 默认拒绝；若以后允许跳转，必须对每个新目标重新执行 allowlist、TLS 和出口检查。

## 熵源：当前真实缺口与门控

`nuttx/.config` 当前开启 `CONFIG_DEV_URANDOM=y` 和 `CONFIG_DEV_URANDOM_XORSHIFT128=y`。NuttX `drivers/crypto/Kconfig:35-41` 明确说明 XORSHIFT128 不适合密码学用途；`43-51` 对 congruential 也有相同限制。`53-73` 还说明 RANDOM_POOL/ARCH 的安全性取决于熵质量或具体实现，不能只看到配置名称就认定可靠。

两条现有调用路径最终都落到这个设备：

- `agent_secure_random()` 只从 `/dev/urandom` 或 `/dev/random` 读取，并处理短读/EINTR（`packages/ai_agent/include/agent_compat.h:115-145`）。`vela_tls.c:64-74` 在读取失败时拒绝，但读取成功本身不证明熵质量。
- 本地 mbedTLS 的 NuttX `entropy_poll.c:106-116,168` 使用 `getrandom()`；NuttX `libs/libc/misc/lib_getrandom.c:75-91` 在 flags=0 时打开 `/dev/urandom` 并读取。换用默认 `mbedtls_entropy_func` 不会自动绕开 XORSHIFT 后端。

最小门控应同时满足：

1. 编译时明确排除 XORSHIFT128 和 CONGRUENTIAL 作为生产 OTA 的熵源。
2. 只有经过板级核验的硬件 RNG 或有明确熵注入与就绪证据的池才能被生产适配层标记可用；配置一个随机池名称或添加时间戳不能充当证据。
3. 运行时读取完整熵字节数，初始化/健康检查/读超时失败均拒绝 TLS，且不回退到可预测 PRNG。
4. 当前构建默认返回 `entropy_unverified` 一类稳定错误码。主机测试使用主机操作系统的真实随机源，这不能替代板端核验。

本次在 `nuttx/arch/arm/src/stm32h7` 没有找到 RNG 驱动 `.c/.h` 或可启用的 RNG Kconfig 项；仅在 `stm32h7x3xx_rcc.c:316`、`stm32h7x7xx_rcc.c:303` 找到 `CONFIG_STM32H7_RNG` 条件时钟分支。其它 STM32 系列存在 RNG 驱动，不能据此判断 H7 驱动已经可用，也不能直接复制寄存器定义。

硬件待办：按仓库硬件 skill 先查看本地官方硬件/芯片资料与 BSP，核对 H750 的实际 RNG、内核时钟、供电和健康/错误状态；在 `../nuttx` 实际 checkout 的 VelaGuard 分支实现或接入驱动；验证启动就绪、连续读、错误/超时和重启行为，再开启生产熵门控。这里没有依据网上资料判断引脚或修改驱动。

## ECDSA P-256 与 manifest

- 当前 mbedTLS 版本是 3.4.0（`apps/crypto/mbedtls/mbedtls/include/mbedtls/build_info.h:39-50`），固件配置启用 `CONFIG_MBEDTLS_ECDSA_C`、`ECP_C`、`ECP_DP_SECP256R1_ENABLED` 和 `HAVE_TIME_DATE`。mbedTLS 应用 Makefile 编译依赖的 library 源码（`apps/crypto/mbedtls/Makefile:23-59`）。
- 可用 API 包括 `mbedtls_ecp_group_load()`、`mbedtls_ecp_point_read_binary()`、公钥点校验、`mbedtls_mpi_read_binary()` 与 `mbedtls_ecdsa_verify()`。verify 输入为已哈希消息和 r/s 整数（`include/mbedtls/ecdsa.h:353-386`）；不要把固定 64 字节 raw r||s 当作 ASN.1 DER 签名传给 DER 接口。
- 既有 manifest 是固定 512 字节，签名偏移 220、长度 64，签名覆盖前 220 字节（`boot/ota_core/vg_ota_manifest.h:5-25`，`vg_ota_manifest.c:45-65`）。应对规范编码的前 220 字节计算 SHA-256，再按 key_id 选只读生产公钥验签，不能对带填充的 C 结构体内存直接签名。
- 现有 `vg_ota_manifest_bind()` 和 staging 校验只拒绝空签名，未执行 ECDSA（`vg_ota_manifest.c:68-80`、`app/velaguard/vg_ota_staging.c:301-305`）。第八阶段必须把真实验签落实到 READY 前；生产拒绝 fixture 标志，设备标识、型号、槽位、版本、时间窗和摘要仍单独检查。

## provisioning 能复用的部分与限制

现有 `vg_provision_load(struct vg_llm_credentials*)` 从固定 eMMC 文件读取密文、取得设备 UID、验证/解包后返回结构体，不打印秘密（`app/velaguard/vg_provision.c:424-466`）。`vg_provision_store_is_persistent()` 可检查是否真的写到持久存储。OTA 应使用单独类型和独立文件保存 MQTT 主机、端口、client id、每设备用户名/密码及下载主机允许列表，不应覆盖 LLM 文件或复用 LLM api_key 字段；CA、签名公钥与 MQTT 密码分别管理。

现有 v1 blob 的上限为 512 明文字节（`vg_provision.h:27-38`），不是 OTA 任意结构的通用序列化格式。其实现为 UID 派生 SHA-256 流 XOR 加 HMAC-SHA256；nonce 由明文长度和 UID 确定，同 UID 同长度会重复（`vg_provision_crypto.c:24-47,118-148,172-192`）。因此不能把它称为 AES-GCM、随机 nonce 或硬件密钥保护。新 OTA 凭据存储宜使用标准 AEAD 与真实随机 nonce、独立域分离、认证失败拒绝、原子替换、只读脱敏状态及使用后清零；若暂时沿用已有 v1，必须明确限制，不得宣称获得新的生产强加密保证。

无人值守可完成类型、序列化、边界/认证失败测试和导入脚本；真实每设备凭据及 ACL、CA/公钥归属、板端熵源和加密存储读回仍需要部署/板级证据。导入命令应传密文块，避免完整密码/API key 出现在会被回显的串口命令中。

## 改动归属与后续证据

| 内容 | 所属仓库 | 本轮建议 |
|---|---|---|
| OTA HTTP 状态机、严格 TLS 适配、ECDSA、allowlist、OTA provisioning 与状态上报 | 竞赛仓 `app/velaguard`、`scripts`、文档 | 直接实现，不借用 Agent 宽松 TLS |
| 共享 TLS 的通用修复 | `../packages/ai_agent` 的实际 checkout | 本任务不必修改；如以后改，按该公共仓分支/PR 流程 |
| mbedTLS 通用适配或系统 socket/DNS 修复 | `../apps` 或 `../nuttx` 实际 checkout | 发现真实阻塞再修改，不使用竞赛仓 patch/vendor 改写 |
| H7 RNG 驱动/系统熵初始化 | `../nuttx` | 本地官方硬件资料核对后独立实现及验收 |
| 网线、真实服务证书/凭据、板载人工安装、断电注入 | 人工/板级验收待办 | 未执行项保持未验证 |

本报告只确认现有接口与可审查的实现路径；没有将主机 TLS 测试、配置宏或研究结论写成真实设备安全能力。

## 补充：本地官方硬件包与 H7 RNG 实现证据

2026-10-28 按 `velaguard-board-hw` 的顺序读取原理图，再读包内 ST BSP，并检查现有 NuttX 源码。官方资料包路径为 `F:\Project\Embeded\H750B-DK\BOARD INFO\H750B-DK`，包含 242 个文件及四份板级设计/制造 ZIP。没有使用文件名含 unofficial 的引脚表作结论。

### 已证实的器件身份与板级时钟来源

- `mb1381-h750xb-b01-schematic.pdf` 的 PDF 第 3 页，图纸标题 STM32 microcontroller IOs、Sheet 3 of 19、Variant H750XB：U6A/U6B 标为 `STM32H750XBH6`。第 4 页 U6C 也是相同型号。第 3 页已经渲染并逐项查看，未仅依赖文本搜索。
- 同份原理图封面/通用项目名仍写 STM32H745I-Disco，但具体器件与 Variant 字段是 H750XB；不能因封面标题而把目标判为 H745。
- 第 3 页可见 X1 的 25 MHz 有源振荡源与 OSC_25M 网络，也可见 X2 的 32.768 kHz 网络。这只能证明所画板级时钟连接，不能证明 RNG 当前选择哪个内核时钟、其分频限制或启动健康状态。
- 包内 ST BSP 的 `stm32h750b_discovery.h:148-159` 给出 BSP 3.3.4、板名 STM32H750B-DISCO、板号 MB1381B，与上述原理图器件身份一致。该 BSP 是板级外设包，不包含完整 STM32H7 HAL RNG/RCC 驱动。
- 现有 `nuttx/boards/arm/stm32h7/stm32h750b-dk/include/board.h:262-264` 为 USB 选择 HSI48。它不能独立证明 RNG 内核时钟配置。现有 H7 RCC 头虽然定义 RNGSEL、RNGEN 和复位相关位，但缺少本次器件参考手册交叉验证，不能靠宏名补全驱动。

### 包内缺失的必要资料

本次目录与 ZIP 内容核对没有找到适用于 STM32H750XB 的芯片参考手册、数据手册、勘误表、HAL RNG 源码或 RNG 配置/错误恢复示例。因此，资料足以确认 MCU 身份，尚不足以实现并宣称验收 H7 RNG。具体缺口为：

1. H750 RNG 的寄存器地址、字段、保留位、读写/清错语义及复位序列的一手依据。
2. RNG 内核时钟的允许来源、频率/比率限制、时钟监测和 HSI48/PLL 选择规则；以及这些规则与现有 USB、系统时钟的相互影响。
3. H750 具体修订版的随机数种子/时钟错误恢复流程、启动丢弃要求、健康测试与相关勘误。
4. 当前固件如何接入真正硬件熵源、初始化顺序以及读取失败时如何保证有限等待的实现和板端证据。

这些资料可以从官方来源补全；取得后仍必须与本地原理图和 BSP 的实际器件交叉核对。此处没有断言 MCU 不支持 RNG，也没有猜测任何寄存器或引脚。

### F7 现有驱动可参考与不可照搬之处

`nuttx/arch/arm/src/stm32f7/Kconfig:1501-1505` 的 `STM32F7_RNG` 依赖对应系列的 HAVE_RNG 并选择 ARCH_HAVE_RNG。驱动 `stm32_rng.c:41-42` 同时检查 F7 RNG 和设备节点配置；`297-325` 注册 `/dev/random` 或 `/dev/urandom`，可以参考 NuttX 设备接口组织方式。

驱动硬件实现不能直接作为 H750 的正确依据：

- `113-142` 使用 F7 控制寄存器打开生成/中断；`149-208` 检查 F7 时钟与种子错误并重试，丢弃首值且比较连续相同值。对应寄存器字段在 F7 自己的 `hardware/stm32_rng.h:43-60`，不是 H750 参考手册。
- `243-275` 的读路径在 mutex 内使用无期限的 `nxsem_wait()`；错误中断只重试，并没有向读者提供有限期限失败。直接移植不能满足 OTA TLS 有限等待要求。
- 未来 H7 适配应让时钟/种子/健康错误和超时明确返回，退出时停用中断并清理缓冲区所有权，保证短读/取消/并发不会继续写入过期调用者缓冲区。生产熵门控在这些行为核验前保持关闭。

### 可以无人值守推进的边界

现有资料允许先做接口定义、有限等待/错误返回的主机测试、strict TLS 的 entropy_unverified 拒绝测试和发布检查。当前不具备安全实现 H750 寄存器驱动的充分证据，本轮不应为了绕过缺失证据而套用 F7 地址或打开一项自证的 trusted 开关。

补全官方芯片资料并实现驱动后，无网络板测本身可通过 ST-Link 和 COM3 自动完成：读取设备节点、有限长度/并发读、重启、错误/超时行为及不回退软件 PRNG。统计分布正常或连续样本不同只能作为诊断，不能单独证明密码学熵质量。人工接线和网络不是这类板测的前置条件，但必要的官方寄存器/时钟证据仍不可省略。

## 官方 ST 补证：H750 确有 RNG，允许继续实现有界硬件读源

前节仅本地资料包的缺口现已部分由 ST 一手资料补齐，不能再把它解释为 H750 无 RNG。ST [STM32H750XB 产品页](https://www.st.com/en/microcontrollers-microprocessors/stm32h750xb.html) 与 [DS12556 Rev 8](https://www.st.com/resource/en/datasheet/stm32h750xb.pdf) 第 38 页 3.27 均确认该器件包含模拟熵源及调理器。器件身份仍以本地原理图 U6 的 STM32H750XBH6 为依据。

[RM0433 Rev 8](https://www.st.com/resource/en/reference_manual/rm0433-stm32h742-stm32h743753-and-stm32h750-value-line-advanced-armbased-32bit-mcus-stmicroelectronics.pdf) 的定位：第 134 页 Table 8 给 RNG 地址范围；第 416 页给 RNGSEL；第 456 页给 RCC 时钟使能；第 1255-1257 页给初始化、读取及错误处理；第 1259-1262 页给 RNG 寄存器。此次完整 PDF 下载被 ST 服务器返回 HTTP 567，web 工具也因文件过大未取得可本地渲染全文；以上通过 ST 官方检索段落读取，并与下述官方器件头交叉核对，未使用论坛附件替代最新版。

寄存器定义的第二份一手依据是 ST 官方 [stm32h750xx.h，提交 81db1ec](https://github.com/STMicroelectronics/cmsis-device-h7/blob/81db1ec63cdc191fae1565b772da3ea5aa29a683/Include/stm32h750xx.h)：

| 项目 | 已核对定义 | 官方 CMSIS 行号 |
|---|---|---|
| RNG 基址 | `0x48021800` | 2162、2223 |
| CR / SR / DR 偏移 | `0x00 / 0x04 / 0x08` | 1831-1833 |
| CR | RNGEN=bit2，IE=bit3，CED=bit5 | 16669-16677 |
| SR | DRDY=bit0、CECS=bit1、SECS=bit2、CEIS=bit5、SEIS=bit6 | 16680-16694 |
| RCC RNGSEL | D2CCIP2R[9:8] | 15344-15348 |
| RCC 门控/复位 | AHB2ENR.RNGEN=bit6；AHB2RSTR.RNGRST=bit6 | 15647-15649、15972-15974 |

RM0433 第 1257 页要求 seed error 恢复时清 SEIS、丢弃 12 个 DR 字，再复检 SEIS；不能照搬 F7 仅开关 RNGEN 的错误恢复。首次实现可以在任何健康/时钟错误时直接失败，不必自动恢复后继续本次 TLS，但不能向调用者交付错误状态下的数据。[ST HAL RNG 源码](https://github.com/STMicroelectronics/stm32h7xx-hal-driver/blob/7e541d92019e18f98d211fc4ab9197ec8e8105f6/Src/stm32h7xx_hal_rng.c) 有跨 H7 型号条件分支；H750 器件头没有 CONDRST，不能套用其它 H7 型号的 CONDRST 恢复流程。

### 现有时钟能否保留

ST [HAL RCC 扩展头，提交 7e541d9](https://github.com/STMicroelectronics/stm32h7xx-hal-driver/blob/7e541d92019e18f98d211fc4ab9197ec8e8105f6/Inc/stm32h7xx_hal_rcc_ex.h#L920) 明确 RNGSEL：00=HSI48、01=PLL1Q、10=LSE、11=LSI，消除了 RM 检索文本中 hsi8 的排版/OCR 歧义。可新增 HSI48 48 MHz 内核源而保持现有系统/PLL 配置；这是基于文档和源码的实现可行性判断，尚未上板验证。[来源：RM0433 416、1257、1259 页](https://www.st.com/resource/en/reference_manual/rm0433-stm32h742-stm32h743753-and-stm32h750-value-line-advanced-armbased-32bit-mcus-stmicroelectronics.pdf)

当前 NuttX 基线 `09fcb4d0762` 的 `board.h:99-113,187-193` 配置 SYSCLK 400 MHz、HCLK 200 MHz；48 MHz 大于 HCLK/32，可保留 CED=0。该推导仍需实际 RCC 读回证实。当前 `.config` 未开启 HSI48；`hardware/stm32h7x3xx_rcc.h:169-170,587-588,884` 已有 HSI48ON/RDY、RNGSEL mask 与 RNGEN，RCC 启动代码 `stm32h7x3xx_rcc.c:316-319` 也已有 RNG 门控分支，但没有 RNGSEL 具体枚举和完整 RNG 驱动。

不宜直接打开现有 HSI48 Kconfig 后忽略启动期限：同文件 `728-739` 的 HSI48 ready 等待没有期限。为 OTA 提供有界熵读取时，应让 HSI48 就绪、DRDY、mutex 获取和错误退出均有期限；不修改其它外设的时钟选择位，也不把系统 PLL 重配为随机数源的一部分。

### 勘误和剩余核验

[ES0392 Rev 15，2025-09](https://www.st.com/resource/en/errata_sheet/es0392-stm32h742xig-stm32h743xig-stm32h750xb-stm32h753xi-device-errata-stmicroelectronics.pdf) 第 1 页明确适用 H750xB，并按 REV_ID 区分 Y/W、X、V 修订。当前全文检索及条目表未发现 RNG 专项；这不免除 RM 健康/错误处理要求。后续无网络板测应记录实际 REV_ID，核对对应系统/时钟条目。

结论更新：官方资料已确认存在可实现的 H750 RNG 路径。现在可以在实际 `../nuttx` 功能分支实现独立、有界、错误时拒绝的 `/dev/random` 硬件源，再用 COM3/ST-Link 无网络验收；无须由用户先接以太网。仍未实现/执行的项包括：完整 RM 本地留存与相关页面复核、实际 RCC 与芯片修订读回、初始化/数据/错误/超时/并发/重启测试、硬件源与 TLS 适配联测。完成这些之前，生产 TLS 的熵门控继续默认拒绝。此次调查没有写寄存器、改变时钟或运行驱动。
