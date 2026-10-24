# P1/P2 接手修复与板端验收

日期：2026-10-24。目标：完成已授权的 TTS 流式播放、ASR/TTS 连接复用与预连接验证。
各版本日志分别保留，早期失败不作为最终镜像通过的证据。正式镜像已完成完整语音、取消及重连验收。

## 已定位并修复的问题

| 问题 | 证据 | 修改 |
|---|---|---|
| 请求带入未初始化的 provider 路由头 | `llm_stream_prepare` 的栈对象未初始化；无 `/` 的模型仍可能生成 `X-Model-Provider-Id`。污染存储的回归测试修复前确定失败，修复后通过。`provider-fix.log` 的真实 ASR/LLM/TTS 均恢复 200 | 初始化完整请求快照，provider 显式终止 |
| SSE 结束被误当成 HTTP 分块结束 | 旧 chunk decoder 在 sink 返回完成时直接写 DONE；旧排空函数以套接字暂时无数据判定成功。缺零块、缺 trailer 终止行的回归测试修复前失败 | 继续解析到完整 HTTP 边界；尾部读取受取消、原预算及 2 秒上限约束；响应声明 close 时不保留连接 |
| 已发送的 POST 被盲目重放 | 旧复用失败重试会再次读取已经耗尽、无 rewind 接口的上传源 | 请求开始后发生不确定错误直接报告并丢弃连接；发送前识别到失效连接仍正常重建 |
| 诊断 TTS 调用栈超出 32 KiB | 旧镜像反汇编仅 main、TTS request、TLS exchange、PCM decode 四层就占 33640 字节，尚未计其余调用；`iob-isolate.log` 出现 PCM 样本覆盖返回地址的硬故障 | PCM 解码片段从 16384 缩至 3072 字节，重采样缓冲同步使用共享上限；增加大 SSE 行的逐字节回归验证 |
| ICMP 套接字被新任务继承，耗尽 IOB | `network-snapshot.log` 仅余 1/128 IOB；只读 SWD 快照中 127 个占用节点均为网关 ping 回复，ID 为 2..128，排队在无人读取的 ICMP socket 中；任务继承开关已启用，而 ping socket 未设 CLOEXEC | 公共 apps 的 ping 和 ai_agent 受控 TLS socket 均在创建时设置 SOCK_CLOEXEC；真实 loopback+exec 测试修复前失败、修复后通过 |
| 快速新建连接耗尽固定 TCP 池 | `release-reconnect.log` 前 7 次成功、第 8 次进入 TLS 前失败；`release-network.log` 显示 7 个 ref=0、state=09 的 TIME_WAIT 连接占位，配置只有 8 个固定槽位且开启 SOLINGER | 主线配置保留 8 个预分配项，按 4 个增长，最多 32 个；保留正常关闭和 120 秒等待规则 |

`host_tests/Makefile` 原先只构建 `test_tts_player`，没有执行它。本次补入实际执行，并加入请求构造及套接字生命周期回归。

## TLS 验签失败的直接证据

`hash-probe.log` 与 `hash-dump/` 捕获同一次失败握手；复核脚本为
`handoff-tools/analyse_hash.py`，结果为 `hash-analysis.json`：

- 线上证书与握手数据可在主机通过签名验证，接收流双副本一致。
- 客户端随机数一致，板端 server_random 的偏移 0、4、8、12、16、20、24、28 与线上副本不同。
- 板端 SKE 摘要不等于线上正确数据的摘要，但等于使用错误随机数重新计算的摘要。
- `copy-probe.log` 在 memcpy 返回后立即发现源/目标不同，源位于 SDRAM，目标位于片内 SRAM。
- 大块 SDRAM 内的 128 组普通复制测试通过；不能据此认为所有内存区域及访问方式均正确。

反汇编 `hash-dump/tls-before-unaligned-fix.asm` 进一步确认：编译器把常量 32 字节 memcpy 内联为逐字 LDR，源为未对齐的 SDRAM 地址。普通库 memcpy 会先处理对齐，因此原先 128 组函数指针调用测试通过，不能排除内联访问错误。

ST [ES0392 Rev15，2.6.4](https://www.st.com/resource/en/errata_sheet/es0392-stm32h742xig-stm32h743xig-stm32h750xb-stm32h753xi-device-errata-stmicroelectronics.pdf) 明确列出 FMC 不支持非对齐读取，要求用编译选项保证对齐。本次在 NuttX 的 H750B-DK Make.defs 中，对启用 FMC 的 GCC/Clang 构建统一加入 `-mno-unaligned-access`，覆盖 C/C++、应用及库代码。修改后该复制改为调用安全库实现，见 `tls-after-unaligned-fix.asm`。

`writeback-probe.log` 是已排除的缓存策略实验：write-back 仍在首轮复现相同复制错误。因此已恢复原 write-through 配置，没有把缓存切换作为修复。SDRAM MPU 属性保持不变。`aligned-probe.log` 在恢复缓存原配置、仅加入非对齐访问限制后，8 次独立新握手及完整 TTS 均成功，无复制检查失败、无 -0x4380，结束时 IOB 128/128。

这些证据支持 FMC 非对齐读取是所捕获 -0x4380 的根因；没有证据继续将渲染越界、音频 DMA 或未知堆写者写成该样本的既定根因。少量通过样本不替代长期稳定性测试。

独立佐证（原排查会话的两份失败转储）：`../tls-root-cause/pull-dump0.log` 与
`pull-dump-p2b.log` 记录的 in_msg 地址 0xd027efc5 / 0xd02a11b5 均为奇地址
（in_msg = in_buf+13 构造性为奇，位于 SDRAM FMC 区），server_random 的复制源
恰好落在非对齐地址上，与 ES0392 机制直接吻合。这也解释了原取证链的盲点：双副本
捕获与 in_msg 比对只能证明源完好，被污染的 randbytes 在 mbedtls 私有结构内、
恰是无法转储的唯一一环；copy-probe 在 memcpy 返回后立即比对源/目标补上了这一环。

## 已完成的阶段验证

`final-cloexec-accept.log` 对应 write-through 尚未更改、其余修复已完成的镜像：

- 两轮真实板端 ASR→MiMo 选工具→只读查数→事实播报→TTS 完整播放成功。
- ASR/TTS 复用同一语音连接；后续预连接命中热连接为 0 ms。
- 两轮 play_start 分别为 15150/13730 ms，tts_end 为 20170/19160 ms，播放启动早于接收完成 5.02/5.43 秒；整轮为 20770/20020 ms。
- 两轮 `complete=1 failed=0 leaked=0`；播放中取消为 `partial=1`，700 ms 回收，`retained_pcm=0 controlled_active=0`。
- `streamtest` 完成，40 次欠载、39 次恢复；各检查点 IOB 为 128/128。
- 语音线程栈 49128 字节，实测最高 30280，余量 18848。
- 取消后的独立新握手仍出现一次 -0x4380，因此这份日志不能作为稳定性全部通过的证明。

计时说明：play_start 是 AUDIOIOC_START 成功后的应用日志，尚未用外部声学仪器测量实际声压输出。played 字段按现有接口表示交给驱动的字节，完整性另由驱动归还/完成事件判定。

## TLS 修复后的首次正式镜像验证

构建命令 `bash scripts/build.sh --clean`，随后运行 `scripts/flash.ps1` 并执行 ST-LINK 硬复位。镜像 SHA-256 见 `release.sha256`，关键开关见 `release-config.txt`：正常健康探测已开启，TLS 取证已关闭，保留原缓存策略。`flash-release.log` 及 `release-accept.log` 对应这份镜像。

- 两轮 ASR、LLM 工具选择、板端只读查数、TTS 均成功；播放器 `complete=1 failed=0 leaked=0`。
- 两轮 play_start 为 21860/17740 ms，tts_end 为 27400/23300 ms，启动早于接收完毕 5.54/5.56 秒；整轮为 28110/24030 ms。ASR/TTS 相位显示使用热连接，第二轮预连接耗时 0 ms。
- 第三轮播放中取消，TTS 如实返回 cancelled/partial，940 ms 回收，`retained_pcm=0 controlled_active=0`。之后独立新握手的 TTS 返回 200 并完整播放。
- 正式镜像语音工作线程栈最高 29552/49128，余量 19576；诊断 ttss 为 21728/32696，余量 10968。
- 本地欠载流测试完成；每个资源检查点 IOB 均为 128/128。
- 主机完整回归通过，见 `host-tests.log`；包含真实请求构造、套接字跨 exec 所有权、HTTP 边界及大 SSE 行分片的回归。

随后追加的 `release-reconnect.log` 并非全通过：连续 8 次新建连接中 7 次成功，最后一次因上述固定 TCP 池耗尽失败，IOB 保持 128/128。这个版本的完整语音验收成功不能覆盖这次资源上限失败。连接池修改后的最终镜像使用单独的 `capacity.sha256`、`capacity-config.txt`、`flash-capacity.log` 和 `capacity-accept.log`，避免混用版本。

## 连接池扩容后的握手发送失败与修复

`capacity-accept.log` 与 `senderrno-accept.log`（同一镜像加 errno 日志行，`senderrno.sha256`）显示：扩容后完整语音、取消与重连均通过，但快速连续新建连接时第 7/8 次在握手发送阶段约 30ms 内失败，受控 send 返回 errno=107（ENOTCONN），`/proc/net/tcp` 中该连接停在 SYN_SENT，另有 7 个 TIME_WAIT。

归因与修复分两层：

- 根因在 NuttX TCP poll 事件回调（`nuttx/net/tcp/tcp_netpoll.c`）：本配置未启用 `CONFIG_NET_TCP_WRITE_BUFFERS`，`psock_tcp_cansend()` 无条件返回 OK，任何 devif TX 通知都会让 SYN_SENT 连接被报告 POLLOUT；上层在 SO_ERROR==0 时进入握手，send 因连接未建立返回 ENOTCONN。这与 pollsetup 初始判定和发送路径自身的 `_SS_ISCONNECTED` 守卫矛盾。修复为同一守卫，提交 `5454419b235`。
- 防御在 `packages/ai_agent` 的 `tls_connect_controlled`（提交 `67e8b14`）：poll 报可写且 SO_ERROR==0 后，用 `getpeername()` 确认连接已建立才进入握手，未建立时在同一请求预算内继续等待。

修复后镜像 `enotconn.sha256`（`enotconn-config.txt`、`flash-enotconn.log`、`enotconn-accept.log`）的完整验收：streamtest 40 次欠载 39 次自恢复；两轮完整 ASR→LLM→TTS 流式播放 `complete=1 failed=0 leaked=0`；播放中取消 1260ms 回收；取消后独立新握手成功；连续 8 次新建连接前 7 次全部 `rc=ok`（含修复前必失败的第 7 次），ENOTCONN 不再出现；第 8 次在两次握手尝试后按预算如实返回 `rc=timeout`（30.26s）——此时 SYN 未获网关应答，属于出口侧环境限制，板端不再误判为发送失败。全程无 `-0x4380`，各检查点 IOB 128/128，语音线程栈余量约 19.7KB。正常语音流程每轮至多 2-3 个连接，全部稳定。

日志采集器校验检查 316 文件、74391 事件，没有报告错误，但有一份 2026-09-16 的历史孤立日志未进入 manifest 的警告。该问题未通过改写历史日志或 manifest 处理，本次提交不纳入其他会话的未提交日志。

## 源码对应关系

| 实际仓库 | 本次提交 | 内容 |
|---|---|---|
| nuttx | `45dd33ad936` | H750B-DK 的 FMC 对齐编译选项 |
| nuttx | `5454419b235` | TCP poll 仅对已连接 socket 报告 POLLOUT |
| apps | `0c8bc59ce` | ping socket 不跨任务继承 |
| packages/ai_agent | `7c33205` | 请求初始化、HTTP 完整边界、POST 不盲重放、受控 socket 所有权及回归 |
| packages/ai_agent | `67e8b14` | POLLOUT 后 getpeername 确认建立再握手；受控 send 失败打印 errno |
| apps/crypto/mbedtls/mbedtls | `509abe981` | 诊断开关下的公开握手摘要与复制检查；正式镜像关闭 |

本仓提交包含小块 PCM 及共享重采样上限、主机回归入口、连接池配置和本目录证据。公共树在各自实际 checkout 提交，没有生成补丁脚本。本次仅完成本地提交，未推送或创建 PR。

## 验证范围与剩余事项

本次采用板端 NSH 驱动、已有录音文件和真实 MiMo HTTPS 服务。离线点位如实播报离线，没有伪造当前读数。
不将本轮少量样本与旧版本直接相减，宣称固定整轮收益；P0 全量冷/热、告警负载矩阵仍未补齐。
P2 本期覆盖 ASR/TTS；LLM 仍由原 ai_agent 线程建立独立连接，跨线程 LLM 连接复用及 P3/P4 不属于本次实现。

原始失败日志、各版本烧录日志及计划均在本目录。完整密钥和 SWD 的原始内存快照不入库。
