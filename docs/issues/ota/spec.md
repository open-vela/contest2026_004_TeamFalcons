# VelaGuard OTA 实现规格

Status: ready-for-agent
Feature: ota

## Problem Statement

立项时，VelaGuard 只能通过 ST-Link 写 QSPI，现场升级需要电脑、调试器和停机。当前已完成存储探测、固件身份、安全重启、FAT/BCB 选槽，以及本机按键确认后的诊断镜像安装到 B。trial 自检与回滚、MQTTS 控制、HTTPS 下载、应用验签和正式屏幕安装确认仍待实现，OTA 页面仍使用模拟数据。

设备用户需要一条受控的 MQTTS Control + HTTPS Download OTA 路径：新版本由云端提议，设备在后台下载并验证，安装前必须由用户在本机明确确认，失败或断电后仍能回到可用固件。升级不能重新格式化 eMMC，不能丢失现有 AI 凭据、MQTT 参数、点表和运行配置，也不能把 eMMC 容量当作固定已知值。

## Solution

采用已经确认的 MQTTS Control + HTTPS Download OTA：

- MQTTS 承载 OTA Offer、下载和安装进度、结果以及新固件自检状态。
- HTTPS 流式下载 Staging Image，写入现有 eMMC FAT 的 OTA Staging Directory，不使用原始分区，不重新格式化。
- 应用验证云端 manifest 的 ECDSA P-256 签名、设备型号、槽位、版本、时间窗和镜像 SHA-256。
- 下载和校验可以在后台完成；安装前进入 READY_AWAITING_CONFIRM，只接受板载触摸或按键产生的 Install Confirmation。
- 用户确认后应用写入 pending BCB 并重启。内部 Flash 中的 boot stub 读取现有 eMMC FAT，验证暂存镜像，烧写 B 槽，再跳转 B 槽。
- A 槽为出厂和恢复镜像，只由 ST-Link 写入；B 槽只由 OTA 写入。B 槽启动后自检，成功写 confirmed，失败或 trial 次数超限回到 A 槽。
- OTA 只使用 RJ45 出口，ESP-01S Wi-Fi 出口拒绝 OTA。

实现按 P0 到 P5 推进。P0 已于 2026-10-28 完成：eMMC 实测 4GB 级、FAT16 64 KiB 簇，QSPI scratch 擦写回读通过；原始证据见 [OTA P0 证据](../../ota-evidence/2026-10-28/p0-probe.md)。P0 对单颗/总容量的解释已按任务 04 更正：当前按 DFM 使用 8 KiB 逻辑擦除，保持原 64 MiB 软件上限，不扩大布局。P1 的身份基础（ticket 02）已完成：`VERSION` 单一来源、A/B 镜像元数据检查、状态和屏幕统一显示版本/槽位、`vgota reboot` 安全重启；证据见 [t02-identity](../../ota-evidence/2026-10-28/t02-identity.md)。

任务 03 已完成 boot stub 的 eMMC 单线读取、只读 FAT16/32、双份 BCB 选序与 A/B 跳转，提供 `vgota bootinfo` 和仅诊断构建的 BCB fixture；见 [t03-boot-selection](../../ota-evidence/2026-10-28/t03-boot-selection.md)。普通启动路径不写 QSPI；pending 仍回 A，镜像安装与 trial 自检/回滚分别属于任务 04、05。

任务 04 已完成 pending 镜像的 manifest/摘要校验、B 槽安装、回读和 TRIAL 持久化；用户本机按键确认后的诊断安装已启动 B，完整 8 MiB A 槽摘要前后一致。见 [t04-install](../../ota-evidence/2026-10-28/t04-install.md)。任务 03 当时 pending 回 A 的限制由本任务替代。

任务 05 已完成 B 槽新固件自检、confirmed 提交、重启直接进入 B（`b-confirmed`）、自检失败或指令回滚到 A 槽（`a-failed`），以及硬件看门狗超时复位与 trial 计数自动步进 1->2->3 超限后降级回滚 A 槽；见 [t05-rollback](../../ota-evidence/2026-10-28/t05-rollback.md)。

任务 06 已完成暂存存储管理、Manifest 校验矩阵（Device ID、型号、槽位、版本、时间窗、签名及摘要）、空间不足拒绝、写临时文件与原子改名，以及进入 `READY_AWAITING_CONFIRM` 并在屏幕和状态机中就绪；验证无 pending BCB，重启不触发安装；见 [t06-staging](../../ota-evidence/2026-10-28/t06-staging.md)。正式应用验签、本地屏幕长按确认和网络 OTA 仍待后续任务实现。

## User Stories

1. 作为现场运维人员，我希望在板上看到 eMMC 实际容量、FAT 类型、剩余空间和 QSPI 实际容量，以便确认 OTA 布局不会依赖错误的容量假设。
2. 作为现场运维人员，我希望在 QSPI scratch 区做一次擦除、写入和回读校验，以便在正式安装前确认烧写路径可用。
3. 作为设备用户，我希望状态和屏幕显示当前固件版本和启动槽位，以便知道设备正在运行哪个镜像。
4. 作为发布运维人员，我希望云端发布带 task_id、版本、大小、摘要和签名的 OTA Offer，以便设备只接受明确授权的版本。
5. 作为设备用户，我希望设备只在 RJ45 链路可用时接受 OTA，以便升级流量不走 ESP-01S Wi-Fi 出口。
6. 作为设备用户，我希望下载在后台进行，采集、告警、屏幕和本地规则继续运行，以便升级不阻塞现场监控。
7. 作为设备用户，我希望设备在下载前检查剩余空间，以便不会因为空间不足写坏暂存文件。
8. 作为设备用户，我希望设备在下载中断后继续使用当前固件，以便网络波动不影响运行。
9. 作为设备用户，我希望设备在支持 Range 时续传，不支持时安全地从零开始，以便大镜像下载不会因为一次断线全部浪费。
10. 作为安全负责人，我希望设备验证 HTTPS 服务器证书和主机名，以便镜像不会从伪造服务器下载。
11. 作为安全负责人，我希望设备验证 manifest 的 ECDSA P-256 签名，并绑定 Device ID、型号、目标槽位、版本、大小、镜像摘要、task_id 和时间窗，以便其他设备或其他版本的镜像不能被安装。
12. 作为安全负责人，我希望设备在下载完成后再次回读并计算镜像 SHA-256，以便 eMMC 写入错误或存储损坏被发现。
13. 作为设备用户，我希望无效签名、错误型号、错误槽位、过期时间和摘要不匹配都被拒绝并上报原因，以便问题可以定位。
14. 作为设备用户，我希望校验完成后看到版本、大小、摘要前缀和重启提示，以便在安装前了解将要发生什么。
15. 作为设备用户，我希望长按屏幕上的安装按钮才执行安装，以便误触不会升级。
16. 作为设备用户，我希望点击取消可以丢弃本次任务，以便可以在不合适的时间拒绝升级。
17. 作为设备用户，我希望 READY 状态超时后自动丢弃暂存并上报 expired，以便旧任务不会一直占用空间。
18. 作为设备用户，我希望同一个 task_id 在取消、超时或失败后不再重复弹窗，服务器必须发布新的 task_id，以便升级提议不会反复打扰现场。
19. 作为安全负责人，我希望安装确认只能来自板载触摸或按键，MQTT、手机、语音识别结果和设备端 Agent 都不能确认安装，以便远程通道没有安装授权能力。
20. 作为安全负责人，我希望 `ota/confirm` 只表示新固件自检状态，不能作为远程安装授权，以便主题语义不会扩大权限。
21. 作为设备用户，我希望确认安装前再次检查高等级活动告警、存储可写、摘要匹配、设备空闲和 RJ45 链路，以便不安全状态下不会开始烧写。
22. 作为设备用户，我希望存在高等级活动告警时拒绝安装，以便告警处理优先于升级。
23. 作为设备用户，我希望存储不可写或空间不足时拒绝安装，以便不会留下半写状态。
24. 作为设备用户，我希望有正在进行的本地配置事务时拒绝安装，以便点表或配置操作不会被重启打断。
25. 作为设备用户，我希望下载和校验失败后当前 A 槽固件继续运行，以便 OTA 失败不改变设备可用性。
26. 作为设备用户，我希望断电发生在下载或暂存阶段时，重启后不会安装不完整镜像，以便断电不会造成启动失败。
27. 作为设备用户，我希望断电发生在 pending BCB 写入时，重启后仍有一个有效 BCB 或回到 A 槽，以便启动控制不会因为一次写坏而失效。
28. 作为设备用户，我希望断电发生在 QSPI 烧写时，boot stub 发现镜像不完整并回到 A 槽，以便 B 槽写坏不会让设备变砖。
29. 作为设备用户，我希望 B 槽第一次启动进入 trial，只有自检成功才写 confirmed，以便新固件必须证明自己能运行。
30. 作为设备用户，我希望 B 槽自检失败或启动挂起时自动回到 A 槽，以便失败版本不会形成启动循环。
31. 作为设备用户，我希望 trial 次数超过上限后 boot stub 自动标记 failed 并回到 A 槽，以便反复失败不会无限重试。
32. 作为设备用户，我希望 boot stub 选择最高有效 seq 的 BCB，并在 BCB 无效时回到 A 槽，以便双份启动控制文件提供恢复能力。
33. 作为设备用户，我希望 boot stub 在 FAT 类型不支持、簇链损坏、目录项异常、文件超出槽位或 eMMC 读取失败时回到 A 槽，以便只读解析失败不会阻止启动。
34. 作为设备用户，我希望 boot stub 在跳转前检查 B 槽向量表的初始 MSP 和复位入口，以便损坏镜像不会把 CPU 带到无效地址。
35. 作为设备用户，我希望 A 槽始终是出厂和恢复镜像，OTA 永远不写 A 槽，以便恢复目标稳定。
36. 作为设备用户，我希望 B 槽只由 OTA 写入，以便现场不会被误写恢复镜像。
37. 作为设备用户，我希望 OTA 使用现有 eMMC FAT 和固定 8.3 暂存目录，不重新格式化，以便 AI 凭据、点表和运行配置保留。
38. 作为设备用户，我希望设备以运行时读取的 eMMC 容量和 `df` 结果决定可用空间，以便 4GB 级器件的容量差异不会导致布局错误。
39. 作为发布运维人员，我希望设备上报 task_id、版本、状态、进度、字节数和错误码，以便发布过程可观察。
40. 作为发布运维人员，我希望设备上报 declined、expired、failed、installed 和自检结果，以便云端知道任务结果。
41. 作为安全负责人，我希望完整下载 URL、MQTT 密码、API key 和签名私钥不出现在日志、串口、屏幕或 MQTT payload 中，以便凭据不会泄漏。
42. 作为安全负责人，我希望 MQTTS 使用每设备凭据并验证服务器证书，以便控制通道不是共享明文口令。
43. 作为安全负责人，我希望 OTA 在系统时间无效时拒绝开始，以便证书有效期和 manifest 时间窗有意义。
44. 作为设备用户，我希望 OTA 拒绝低于当前版本的 signed manifest，恢复旧版本只能走 A 槽或工厂流程，以便签名镜像不能用来降级攻击。
45. 作为设备用户，我希望 Wi-Fi 出口收到 OTA Offer 时明确拒绝并上报原因，以便不会出现半支持的升级路径。
46. 作为设备用户，我希望下载期间断网后当前固件继续运行，重新连接后可以报告状态并按新 task_id 重新开始，以便网络问题不会破坏设备。
47. 作为维护开发者，我希望 A 槽和 B 槽镜像使用各自的链接地址、槽位标记和同一份版本来源，以便启动后能确认镜像与槽位匹配。
48. 作为维护开发者，我希望 boot stub 构建有 128 KiB 片内 Flash 大小断言，以便加入 FAT、BCB 和烧写逻辑后不会链接溢出。
49. 作为维护开发者，我希望主机测试覆盖 FAT、BCB、manifest、状态机和断电恢复矩阵，以便高风险逻辑在烧板前就能回归。
50. 作为维护开发者，我希望板级验收使用本地 HTTPS 和 MQTTS 测试服务，覆盖真实下载、本地确认、重启和回滚，以便主机测试不能代替设备证据。
51. 作为维护开发者，我希望测试构建可以写入 fixture 镜像和 BCB，生产构建排除该入口并拒绝未签名 manifest，以便测试便利不会变成生产后门。
52. 作为维护开发者，我希望 OTA 状态和 BCB 在重启后可以恢复或安全清理，以便 READY 前重启和确认后重启都有确定行为。
53. 作为设备用户，我希望安装确认后如果 BCB 写入失败，设备留在 READY 并显示错误，不执行重启，以便不会在没有启动控制的情况下重启。
54. 作为设备用户，我希望 OTA 页面不再显示模拟版本和模拟进度，以便屏幕内容对应真实任务。
55. 作为出厂和维修人员，我希望 A 槽和 boot stub 仍可通过 ST-Link 恢复，以便两个 QSPI 槽都不可用时还有最后手段。

## Implementation Decisions

### 启动链与槽位

- boot stub 位于 H750 片内 128 KiB Flash，是唯一在运行期写 QSPI 的组件。应用不得在 QSPI XIP 运行期间擦写 QSPI。
- QSPI A 槽从 `0x90000000` 开始，B 槽从 `0x90800000` 开始，各 8 MiB。任务 04 已按 DFM 和成对 ID 验证 8 KiB 逻辑擦除、256 字节编程、完整 B 镜像回读及 A 不变。地址上限保持 64 MiB，不把 P0 旧解释当作器件数量或总容量证明。
- A 槽只由 ST-Link 写入，作为出厂和恢复镜像。B 槽只由 OTA 写入。boot stub 和构建脚本都要有地址范围检查，拒绝跨槽写入。
- A 镜像和 B 镜像由同一份源码构建，使用不同的链接地址、槽位标记和版本来源。B 镜像启动后检查自身槽位标记与 boot stub 设置的槽位一致；不一致时自检失败。
- boot stub 增加最小 eMMC 1-bit 读取驱动、只读 FAT16/32 解析、BCB 选择、QSPI 间接擦写、镜像校验和跳转。FAT12、exFAT、不支持的扇区或簇大小、损坏簇链、目录项异常、文件超出槽位和读失败都回到 A 槽。
- 暂存文件使用固定 8.3 名称，boot stub 只读取 OTA Staging Directory，不遍历整个 FAT。暂存目录缺失、文件缺失或文件名不匹配时回到 A 槽。
- boot stub 在跳转前检查目标槽位向量表：初始 MSP 位于 SRAM 范围并对齐，复位入口地址位于目标槽内且 Thumb 位置位。检查失败时回到 A 槽。
- boot stub 在 B trial 启动前设置独立看门狗，超时覆盖 B 启动和自检。应用接管后刷新看门狗；自检失败或应用未接管时复位，trial 计数继续。A 槽正常启动不启用该 trial 看门狗。具体超时和硬件可用性在 P1 验证。
- boot stub 自身不通过 OTA 更新。boot stub 更新属于 ST-Link 工厂流程，不在本规格范围内。

### OTA Staging Directory

- 暂存目录为现有 eMMC FAT 上的 `/mnt/emmc/VGOTA/`，使用固定 8.3 文件名：`IMAGE.BIN`、`IMAGE.TMP`、`MANIFEST.BIN`、`BCB0.BIN`、`BCB1.BIN`、`STATE.BIN`。
- 不重新格式化 eMMC，不创建原始分区，不迁移 AI 凭据、MQTT 参数、点表或运行配置。暂存目录与 `/data` 共享 FAT 剩余空间。
- 下载前用 `statfs` 检查剩余空间，要求镜像大小、manifest、两份 BCB、状态文件和保留余量都能放下。空间不足时拒绝任务并上报原因。
- 镜像先写 `IMAGE.TMP`，完成 fsync 和回读校验后改名为 `IMAGE.BIN`。manifest 和状态文件最后写入。启动时发现没有有效状态或没有 manifest 的 `IMAGE.TMP` 直接清理。
- 镜像大小不得超过槽位可用大小减去向量表和保护余量。manifest、BCB 和状态文件使用固定大小，便于 boot stub 定位和校验。
- 暂存目录和文件由 OTA 服务管理。其他模块不得写入该目录。清理策略在 confirmed、failed 或 expired 后执行，保留 BCB 供状态查询，删除镜像和 manifest 释放空间。

### 应用侧 OTA 状态机

- OTA 服务是独立模块，由网络或 MQTT 事件驱动，不阻塞 HMI、采集、告警和本地规则。OTA 服务的依赖通过端口注入：控制通道、下载器、暂存存储、manifest 验证器、时钟、本地确认、安全联锁、状态上报和重启。
- 状态机状态为：`IDLE`、`OFFER_RECEIVED`、`DOWNLOADING`、`VERIFYING`、`READY_AWAITING_CONFIRM`、`INSTALL_PENDING`、`REBOOTING`、`CONFIRMED`、`DECLINED`、`EXPIRED`、`FAILED`、`ROLLED_BACK`。
- `IDLE` 收到合法 OTA Offer 后进入 `OFFER_RECEIVED`。预检包括版本高于当前版本、型号匹配、目标槽位为 B、大小不超过槽位、时间窗有效、RJ45 可用、存储空间足够、系统时间有效。预检失败上报 failed，不进入下载。
- `OFFER_RECEIVED` 开始下载后进入 `DOWNLOADING`。下载完成进入 `VERIFYING`。manifest 签名、字段绑定、镜像 SHA-256 和回读摘要都通过后进入 `READY_AWAITING_CONFIRM`。
- 同一时间只允许一个活动 OTA 任务。READY 或下载中的设备收到新 Offer 时拒绝并上报 busy，直到当前任务进入终态。
- `READY_AWAITING_CONFIRM` 只接受本地确认端口产生的安装事件。确认后再次执行安全联锁检查；通过则写入 pending BCB，进入 `INSTALL_PENDING`，同步完成后进入 `REBOOTING` 并重启。BCB 写入失败时留在 READY，显示错误，不重启。
- 取消进入 `DECLINED`，超时进入 `EXPIRED`。两者都清理暂存并上报结果。相同 task_id 在终态后不再弹窗；服务器需要新 task_id 才能再次提议。
- 设备在 READY 前重启时，如果暂存镜像、manifest、状态文件和 BCB 都有效且任务未过期，可以恢复 READY 并再次请求本地确认。存在 pending BCB 时由 boot stub 安装，应用不重复确认。
- 新固件启动后执行自检。自检成功向另一份 BCB 写入 confirmed，上报 `ota/confirm` 自检结果，然后回到正常状态。自检失败写 failed 并重启，boot stub 回到 A 槽；如果 B 槽在自检前挂起，trial 计数和看门狗负责回滚。
- 状态机不提供自动安装路径。下载完成、校验通过或 MQTT 收到任何消息都不能触发安装。

### Boot Control Block

- BCB 使用两份固定 4 KiB 文件 `BCB0.BIN` 和 `BCB1.BIN`，包含 magic、格式版本、seq、状态、槽位、版本、task_id、镜像大小、镜像 SHA-256、trial 计数和 CRC32。
- 状态至少包括 `PENDING`、`TRIAL`、`CONFIRMED`、`FAILED`。应用只写 PENDING；boot stub 写 TRIAL 和 FAILED；应用在新固件自检后写 CONFIRMED 或 FAILED。
- 应用写 BCB 使用临时文件、fsync、回读和替换，并选择当前较低 seq 或无效的那一份写入，保证至少一份旧 BCB 可读。
- boot stub 读取两份 BCB，选择最高有效 seq。两份都无效时回到 A 槽。boot stub 对 TRIAL 和 FAILED 的记录在原文件内就地写入，依靠 CRC 和另一份 BCB 提供恢复能力。
- pending BCB 必须绑定暂存 manifest 和镜像。boot stub 验证 BCB 与 manifest 的 task_id、版本、槽位、大小和 SHA-256 一致后才烧写。
- BCB 不放在片内 Flash。H750 的 128 KiB 片内 Flash 是单个扇区，无法独立擦除，部分字编程还可能触发 ECC 问题。
- BCB 写入顺序为镜像、manifest、状态文件、pending BCB。pending BCB 是启动安装的唯一开关；前面的文件不完整时 boot stub 拒绝安装并回到 A 槽。

### Manifest 与签名验证

- 云端 manifest 使用固定二进制格式，避免 JSON 规范化歧义。字段包括格式版本、Device ID、设备型号、目标槽位、版本、镜像大小、镜像 SHA-256、task_id、签发时间、过期时间、密钥 ID、ECDSA P-256 签名和 CRC32。
- 签名覆盖 manifest 头部字段和镜像摘要，不覆盖签名本身和 CRC。CRC 用于 boot stub 检测意外损坏。字段使用固定字节序和固定长度，字符串有明确上限。
- 应用验证签名、字段绑定、时间窗和镜像摘要。生产构建只接受生产公钥；测试构建可以使用测试公钥和测试 fixture，但生产构建必须拒绝测试签名。
- Device ID 使用现有稳定设备标识。型号固定为本板型号。目标槽位必须是 B。版本必须高于当前运行版本；降级只能通过 A 槽或工厂流程。
- boot stub v1 不实现签名验证，只验证 manifest CRC、字段绑定、镜像大小和 SHA-256。信任边界是应用完成签名校验后才写 pending BCB。物理改写 eMMC 的威胁不在 v1 范围。
- 公钥和密钥 ID 支持后续轮换；私钥只存在云端签名服务，不进入固件、eMMC、日志或 MQTT payload。

### 传输与信任

- OTA 控制使用 MQTTS。现有明文 MQTT 遥测保持不变；生产构建的 OTA 控制通道必须使用 TLS，禁止在明文 MQTT 上接受 Offer。
- MQTTS 使用每设备凭据，用户名使用 Device ID，密码来自加密 provisioning，不在 Kconfig、固件镜像、日志或屏幕中出现。TLS 必须验证服务器证书和主机名。
- HTTPS 下载必须验证服务器证书、主机名和有效期，不允许 `VERIFY_OPTIONAL`、`VERIFY_NONE` 或未知证书回退。CA bundle 在 v1 编译进固件；测试构建使用测试 CA，生产构建使用生产 CA。
- OTA HTTPS 主机限制在 provisioning 的允许列表中。重定向最多跟随 3 次，每一跳都必须通过 TLS 验证并落在允许列表内。完整 URL 和 query token 不写入日志、屏幕或状态 payload。
- 系统时间必须来自可信时间同步并处于有效范围。时间无效时 OTA 预检失败，证书有效期和 manifest 时间窗都不能被绕过。
- 下载使用 HTTPS GET 流式写盘，不要求整份镜像进入 RAM。支持 Range 时按同一版本、同一摘要和同一 ETag 续传；服务端不支持 Range 或 ETag 变化时从零开始。
- 下载设置最大镜像大小、连接空闲超时、总超时、重试次数和退避策略。重试耗尽后进入 FAILED，清理临时文件，当前固件继续运行。
- OTA 只允许 RJ45 出口。检测到 ESP-01S Wi-Fi 出口时拒绝 Offer 并上报 `wifi_not_supported`。现有遥测 MQTT 的出口策略不变。
- OTA 控制消息使用 QoS 1、retain false。设备只订阅 Offer 主题，发布 status、result 和 confirm 主题。重连后重新订阅并上报当前状态，不自动安装。

### MQTT 主题与载荷

- Offer 主题为 `vg/{device_id}/ota/offer`，方向为云端到设备。载荷包含 task_id、版本、manifest 下载地址、镜像下载地址、镜像大小、镜像 SHA-256 和过期时间。地址只通过 MQTTS 传输，不进入日志。
- 状态主题为 `vg/{device_id}/ota/status`，方向为设备到云端。载荷包含 task_id、版本、状态、进度百分比、已下载字节数、错误码和当前槽位。
- 结果主题为 `vg/{device_id}/ota/result`，方向为设备到云端。载荷包含 task_id、版本、结果和错误码。结果至少包括 installed、declined、expired、failed、rolled_back 和 self_check_failed。
- 自检主题为 `vg/{device_id}/ota/confirm`，方向为设备到云端。载荷包含 task_id、版本、槽位和自检结果。该主题只表示新固件自检状态，不能触发安装，也不能作为远程安装授权。
- 进度上报按百分比或时间节流，避免占用 MQTT 队列。状态 payload 不包含完整 URL、token、密码或私钥。
- 现有遥测、告警、状态和点表主题的行为保持不变。固件版本和启动槽位可以加入现有状态载荷，字段名在 MQTT 合同文档中统一。

### 本地安装确认与联锁

- Install Confirmation 只来自板载触摸或按键。OTA 服务暴露的确认端口只能由板端输入层调用；MQTT、手机、语音识别结果和设备端 Agent 没有调用路径。
- READY 页面显示版本、镜像大小、摘要前缀、task_id 和重启提示。安装使用长按，默认阈值 2 秒；取消使用点击。长按和点击的阈值集中在 UI 输入层，避免误触。
- READY 默认超时 10 分钟。超时后清理暂存并上报 expired。相同 task_id 不再重复弹窗。
- 确认时再次检查：无高等级活动告警、存储可写、摘要仍匹配、无活动本地配置事务、RJ45 链路可用、系统时间有效。任意一项失败时留在 READY 并显示原因，不写 BCB、不重启。
- 电源稳定性如果板上有可测信号，则纳入联锁；没有可靠测量时不声称具备该保护。
- 安装确认事件不可由网络、手机、语音或 Agent 代答。开发助手也不能代用户确认。
- 新固件自检确认由固件自动完成，不构成第二次人工确认。`ota/confirm` 只上报自检结果。

### 构建、版本与测试构建

- 固件版本使用单一构建来源，状态、屏幕、manifest、Offer 和 BCB 使用同一版本字符串。槽位标识进入构建和运行状态。
- 构建脚本生成 A 和 B 两个镜像，分别链接到各自槽位地址，并生成 boot stub。A 镜像用于 ST-Link 出厂和恢复，B 镜像用于 OTA。
- boot stub 构建必须有片内 Flash 大小断言，加入 FAT、BCB 和 QSPI 烧写代码后仍须小于 128 KiB。
- 构建脚本保留写入 A 槽和 boot stub 的流程，B 槽默认保持擦除状态。恢复流程只写 A 槽和 boot stub。
- OTA 测试 fixture 入口由独立 Kconfig 控制，默认关闭。生产 defconfig 必须关闭该入口，测试构建可以使用 fixture 写入暂存镜像、manifest 和 BCB。生产构建检查必须证明 fixture 命令不存在或不可用。
- 测试公钥、测试 CA 和测试 MQTT 凭据只存在于测试构建和本地测试服务。生产构建只接受生产公钥和生产 CA。
- OTA 页面替换现有模拟页面，不再显示固定版本、固定大小或定时器驱动的假进度。

### 失败与恢复

- 下载中断：当前 A 槽继续运行，临时文件不产生 pending BCB。重试或新 task_id 重新开始。
- manifest 或摘要失败：清理暂存，上报 failed，当前固件继续运行。
- 暂存写入或改名失败：保留或清理临时文件，不写 pending BCB，上报 failed。
- pending BCB 写入失败：留在 READY，显示错误，不重启。用户可重试或取消。
- pending BCB 写入后断电：boot stub 读取双份 BCB，选择最高有效 seq；有效 pending 触发安装，无效或缺失回到 A 槽。
- QSPI 擦除或编程失败：boot stub 不跳转 B 槽，写 failed 或回到 A 槽，上报失败原因。B 槽可以在下一次任务中重写。
- QSPI 写入后回读摘要不匹配：视为安装失败，回到 A 槽。
- B 槽向量表无效或镜像越界：boot stub 回到 A 槽。
- B 槽自检失败：应用写 failed 并重启，boot stub 回到 A 槽。
- B 槽启动挂起：trial 看门狗复位，boot stub 增加 trial 计数；超过默认 3 次后写 failed 并回到 A 槽。
- 两份 BCB 都无效、FAT 不支持、簇链损坏、eMMC 读取失败：boot stub 回到 A 槽。
- A 槽也无效：boot stub 进入安全停机并保持可被 ST-Link 恢复的状态，不尝试跳转到未知地址。A 槽恢复属于工厂流程。
- 失败任务不自动重试。云端需要新的 task_id 才能再次提议。设备上报 rolled_back 或 failed 后回到正常状态。

### 实施阶段

- P0（2026-10-28 已完成）：读取 eMMC CID、CSD、EXT_CSD 和 `df`，确认 4GB 级容量、FAT16、64 KiB 簇；在板上验证只读 FAT 解析原型和 QSPI scratch 擦写回读。QSPI 旧单颗/容量解释按任务 04 更正，保留现有软件地址上限。
- P1：boot stub 的 eMMC 读取、FAT 解析、BCB 选择、QSPI 烧写、trial/rollback 和 B 镜像构建。
- P2：OTA Staging Directory 的文件存储、镜像、manifest、BCB 和状态文件写入，以及断电测试。不格式化 eMMC，不迁移凭据。
- P3：HTTPS 下载、manifest 验签、SHA-256、READY 状态、本地确认和安装交接。
- P4：MQTTS 控制、每设备凭据 provisioning、协议状态和结果补报。
- P5：自检确认、回滚、故障注入、长测和证据。
- P0 到 P5 的板级验收完成前，不得把现有明文 MQTT、模拟 OTA 页面或启动 stub 当作 OTA 已完成。

## Testing Decisions

- 主机测试接缝只有一条：OTA 核心通过端口注入依赖。启动安装核心注入块设备读、QSPI 读/擦/写、复位/跳转和看门狗端口；应用编排核心注入控制通道、下载器、暂存存储、manifest 验证、时钟、本地确认、安全联锁、状态上报和重启端口。真实适配层不进入主机测试。
- 好的测试只断言外部行为：状态转换、上报的 status/result payload、暂存文件内容、BCB 内容、boot slot 选择、是否触发重启、拒绝原因。测试不依赖内部函数调用顺序，不复制实现细节。
- 启动安装核心的测试使用内存 FAT16/FAT32 镜像和模拟 QSPI。覆盖正常安装、BCB 双份选择、seq 回绕、CRC 错误、簇链损坏、文件越界、向量表无效、QSPI 擦写失败、回读摘要不匹配、trial 超限、两份 BCB 无效和 A 槽恢复。
- 应用编排核心的测试使用 fake 控制通道、fake 下载器、临时目录存储、fake 时钟、fake 联锁和 fake 重启。覆盖合法 Offer、非法签名、错误 Device ID、错误型号、错误槽位、错误版本、过期时间、摘要不匹配、空间不足、下载中断、Range 续传、回读失败、取消、超时、同 task_id 不重复、确认时联锁失败、BCB 写入失败和确认后重启。
- 本地确认端口必须有测试证明：只有确认端口可以触发安装，控制通道、下载器、状态上报和 Agent 工具路径都不能触发安装。测试构建的 fixture 入口可以准备暂存内容和测试 BCB，但只在测试构建存在；生产构建必须排除该入口，生产路径只能由确认端口触发安装。
- 暂存存储测试使用真实 POSIX 文件 API 加临时目录，并用现有 `--wrap` 方式注入 write、read、fsync、rename 失败，验证临时文件、改名和断电点。参考现有文件存储测试的做法。
- manifest、FAT、BCB 解析器做随机损坏输入和边界输入测试，确保不会越界读取、不会把无效内容当成有效镜像。
- 主机测试使用固定的测试时钟、测试公钥、测试 CA 和确定性 fixture。生产密钥和凭据不进入仓库。
- 板级验收覆盖 P0 探测、真实 eMMC FAT 读写、QSPI scratch 擦写、fixture 安装到 B 槽、本地长按确认、重启后版本和槽位变化、B 槽自检、trial 回滚、取消和超时。
- 网络验收使用本地 HTTPS 和 MQTTS 测试服务，覆盖有效证书、无效证书、有效签名、无效签名、下载中断、Range 行为、MQTT 重连和结果补报。测试服务证书和凭据只用于测试。
- 断电验收在下载写盘、临时文件改名、BCB pending 写入、QSPI 擦除、QSPI 页编程、B 槽 trial 启动和 confirmed 写入这些点执行。无法自动断电时记录人工操作证据，不声称已覆盖未执行的点。
- 长测覆盖重复下载、安装、回滚、失败重试、eMMC 剩余空间、日志增长和 MQTT 状态上报，确认没有启动循环、空间泄漏或状态机卡死。
- 生产构建验收检查测试 fixture 入口、测试公钥、测试 CA 和测试凭据都不存在或不可用，未签名 manifest 被拒绝。
- 现有主机测试和板级脚本是测试先例：真实模块源码加 fake 或 wrap，Python 启动栈检查，串口 NSH 验收。新增测试沿用这些方式，不引入只读实现细节的 mock。

## Out of Scope

- 云端发布服务、签名服务和 CI 流水线的实现。本规格只定义设备侧协议契约，测试使用本地 fixture 服务。
- boot stub 自身的 OTA 更新、Secure Boot、硬件信任根、加密固件、防回滚安全计数器、增量升级、超过两个槽位。
- A 槽的 OTA 写入。A 槽只通过 ST-Link 出厂和恢复。
- eMMC 重新格式化、原始分区、固定容量布局和凭据迁移。
- Wi-Fi OTA、手机确认、语音确认、设备端 Agent 确认、任何远程安装授权命令。
- 生产 CA 轮换和证书生命周期管理。v1 的 CA bundle 随固件构建。
- 遥测 MQTT 的 MQTTS 迁移。现有明文遥测不在本规格内改变。
- 点表、配置和 Skill 的 OTA 更新。本规格只处理固件镜像。
- 多设备型号支持和跨型号镜像分发。
- 完整断电认证、环境测试和量产可靠性认证。
- OTA 页面之外的 UI 重设计。

## Further Notes

- P0 已完成 eMMC 和 scratch 探测。QSPI 当前以任务 04 的 DFM 成对 ID、8 KiB 逻辑擦除和安装回读为依据；单颗/总容量结论待独立核验，保留既有 64 MiB 软件上限及 A/B 各 8 MiB 布局，不改写原始历史证据。
- eMMC 手册原记录为 8GB，实测为 4GB 级；实现以 CID、CSD、EXT_CSD 和 `df` 为准，不按 8GB 设计固定布局。
- boot stub v1 不验证签名，信任边界是应用完成签名校验后才写 pending BCB。物理改写 eMMC 的威胁不在 v1 范围。
- `ota/confirm` 只表示新固件自检状态，不是安装授权。安装授权只来自板载触摸或按键。
- 现有 OTA 页面是模拟 UI，不能作为能力证据。实现完成后必须用真机下载、确认、重启和回滚证据替换。
- 现有明文 MQTT、模拟 OTA 页面和启动 stub 都不构成 OTA 已完成的证据。
- 本规格遵循 ADR 0006 和 ADR 0008。测试接缝、阶段门槛和失败恢复与这两份 ADR 保持一致。
