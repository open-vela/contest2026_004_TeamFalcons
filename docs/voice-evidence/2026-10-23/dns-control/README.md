# 受控 DNS 截止、取消与连接回收

日期：2026-10-23，BVO-07 续接。仅验证有界网络传输；阶段 A/B 和完整 30 轮语音成功率仍未通过。

## 实现与边界

NuttX 新增同步 `dns_query_controlled`，沿用现有 DNS 报文校验、缓存和服务地址，UDP/TCP 等待每至多 100 ms 检查同一剩余预算。TCP 截断回退、长度前缀和正文均受控，不建立遗留解析线程，不修改全局重传时间或连接池大小。可选的单次服务地址只用于隔离测试，绕过共享缓存且不改变系统 DNS。旧 DNS API 保留；同时修复原有 TCP 短读写偏移、负返回值与无符号长度比较，以及 UDP 转 TCP 前未关闭 socket 的问题。

ai_agent 仅对受控请求使用新解析器，解析结果经 NUMERICHOST/NUMERICSERV 转换为 addrinfo，避免再次进入平台 DNS。未完成的受控 DNS/TLS socket 设零等待 SO_LINGER 后关闭；正常请求与旧调用保持原有关闭方式。NuttX 的零等待 TCP 关闭已有独立实现，Kconfig 原来错误依赖写缓冲，本次放开 TCP 的配置依赖；本固件未增加写缓冲。构建脚本配置后复查必需项，防止 Kconfig 静默丢弃。

板端未启用 resolv.conf 文件模式。文件操作、内存分配和平台调度不能由网络轮询强行抢占，因此这些有限测试不等于任意系统负载下的硬实时保证。三意图前处理仍需进一步缩短。

## 主机验证

`test_dns_control.c` 编译实际 NuttX resolver，使用 Linux socket 与最小平台适配。36 次故障/恢复覆盖 UDP 无响应、主动取消、TCP 分段长度与正文、正文停顿、半截长度、错误 ID、短响应及后续成功；另验证系统服务地址路径和缓存隔离。检查前后文件描述符数量一致。`host-dns-final.log` 与 `host-dns-asan.log` 通过；`host-dns.log` 保留最初暴露无符号比较错误的失败。完整主机回归见 `host-tests-linger.log`。

## 真机来源和修复前失败

多仓来源、dirty 源码、配置、HEX/BIN/ELF 摘要见各 firmware JSON；烧录记录独立保留。最终 DNS 版本为 `firmware-linger-supported.json` 与 `flash-linger-supported.log`，实际 `.config` 已确认 `CONFIG_NET_SOLINGER=y`。

`board.log` 的 27 次网络返回值与时间符合预期，但留下 6 个 ref=0 的 TCP 关闭连接。`board-linger.log` 仍有同样问题：预设虽增加 SO_LINGER，旧 Kconfig 静默去掉该项。因此这两轮不能作为连接回收通过证据。随后修正依赖并增加构建后的必需项检查，再构建烧录。

## 最终 DNS 复测

`board-linger-supported.log`：3 组各 9 次，共 27 次。9 次成功、6 次超时、6 次取消、3 次错误 ID、3 次短响应，均符合预先固定的期望。300 ms 超时实际约 310 ms；150 ms 后取消实际总耗时 230–250 ms。TCP 分段回复可正常解析。测试前后 UDP 表为空，TCP 表仅正常 MQTT 连接，没有残留 DNS 关闭连接。随后同次启动真实 MiMo ASR 返回 200，用时 3570 ms。

`check-supported.log` 由 `scripts/voice_test/check_dns.py` 核验原始命令、返回码、耗时和 socket 表，不依靠人工挑选成功行。测试服务只绑定隔离网络主机 192.168.137.1:8053，最多 4 个线程、200 次请求、900 秒，未传输产品密钥。

COM7 模拟 Modbus 从站仅响应 FC03/04；板端是唯一主站，点表未修改。上述测试采集帧统计均无 CRC/超时/其他错误；有限测试不替代长期稳定性。

TLS 回归 `board-tls.log` 和 `check-tls.log` 共 25 次通过：200、401、429、截断、握手超时、握手取消、HTTP 响应停顿及恢复；每组连接表仍仅 MQTT。最后真实 ASR 200、3460 ms。首次检查器漏掉带 ANSI 清行符的命令行，修正解析后统计完整 25 次；原始串口记录未改写。完整端到端性能首轮仍为 8/30，见 [事实与固定样本证据](../voice-facts/README.md)。
