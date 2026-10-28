# OTA 任务 08：严格 HTTPS、签名和续传主机证据

日期：2026-10-28。实现与本轮无人值守主机验证已完成；整板联网下载和本机安装验收仍保留在[人工及外部条件待办](../../issues/ota/acceptance-todo.md)，不能据此宣称完整 OTA 已完成。

## 本轮实现

- 独立 mbedTLS 客户端使用 `MBEDTLS_SSL_VERIFY_REQUIRED`、受控 CA、主机名和验证结果；不复用 AI 链路的可选证书校验，不跟随重定向，不回退未知 CA。NuttX socket 固定绑定 eth0。
- 只接受 HTTPS 和 provisioning 提供的精确主机/端口允许列表；拒绝用户名、片段、控制字符和越界 URL。拒绝含糊的 Content-Length、Transfer-Encoding 及压缩响应。
- 下载前取得并验证固定 512 字节 manifest，使用 P-256 验证前 220 字节规范编码的 SHA-256 与 raw r||s 签名。再次核对 Offer 的 task_id、版本、大小、摘要、时间窗、设备、型号及槽位；网络入口不接受 fixture manifest。
- 镜像保持 4 KiB 流式处理，下载时和 FAT 回读分别计算 SHA-256。完整数据、签名和向量验证成功才进入 READY，下载器没有写 pending 或确认安装的接口。
- 只有强 ETag、同一镜像大小和精确 Content-Range 允许续传；ETag 改变、缺少强校验器或服务端忽略 Range 时清理部分文件并安全重下。最多三次退避重试，有限总期限。
- status 使用独立短锁的完整快照。下载线程阻塞网络读时，界面状态读取与心跳超时检查不等待下载互斥锁。
- 公共 CA 与签名公钥由 `VG_OTA_CA_FILE`、`VG_OTA_PUBLIC_KEY_FILE`、`VG_OTA_SIGNING_KEY_ID` 生成本地构建头；缺失时生产 OTA 拒绝。生成器拒绝包含 PRIVATE KEY 的材料。测试根和私钥只在 `.debug/ota-network-tests/` 生成，没有提交。
- 当前板端软件随机源不满足生产熵要求，严格 TLS 默认返回 entropy_unverified。H750 硬件 RNG 的官方资料核对见[研究记录](t08-trust-research.md)，驱动与无网络板测继续推进。

## 已执行

`make -C app/velaguard/host_tests test`：通过，包括新增 HTTP 解析和下载并发状态测试。

`python3 app/velaguard/host_tests/test_ota_network.py`：通过。测试使用仓库 mbedTLS 3.4.0 源码的主机静态库、真实 loopback TLS 服务、动态生成的测试 CA、真实 P-256 签名和实际暂存文件；没有外部网络或开发板安装操作。

```text
valid: rc=0 state=READY_AWAITING_CONFIRM bytes=16384
resume-drop / resume-changed / resume-ignore / resume-no-etag: READY
bad-signature: signature_invalid_or_untrusted
bad-device: bad_device_id
bad-model: bad_model
downgrade: version_downgrade_rejected
expired: offer_expired
reject-bad-range: invalid_content_range
reject-bad-digest: digest_mismatch
reject-always-drop: download_retries_exhausted
unknown-ca / hostname-mismatch / expired-certificate: tls_verification_failed
forbidden-host: ota_host_forbidden
wifi-rejected: wifi_not_supported
no-rj45: rj45_unavailable
test_ota_network: OK
```

测试同时断言没有 BCB 文件，拒绝状态没有进入 READY，程序输出不包含测试 query token 或完整 URL。续传测试核对服务端实际收到 `Range: bytes=5000-` 和对应 If-Range，ETag 变化和忽略 Range 后的第三次请求从零开始；不只核对最终返回值。

并发回归测试对上一提交的 staging 源码运行时，在一秒期限内因状态读取被网络锁阻塞而失败；当前源码通过。该对照证明测试能检测本次修复的实际阻塞路径。

`bash scripts/build.sh`：通过，生产信任材料未配置时保持拒绝。

```text
OTA trust: unconfigured (production OTA refuses)
check_fw_images: OK version=0.1.0 A=0x90000000 B=0x90800000 a_size=1528848 b_size=1528848
```

## 验收范围

本轮没有以太网接线，不执行板端 HTTPS/MQTTS 联网，也不代用户确认安装。实际 CA/签名公钥部署、每设备凭据、RNG 板测、板端资源与联网故障矩阵需要各自证据。生产信任材料缺失时，编译成功不代表设备已可接收生产 OTA。
