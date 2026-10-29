# 板端 MQTTS 与 HTTPS 网络验收（测试信任）

日期：2026-10-29。构建：`2c96a5030f4e-dirty`，A 槽，版本 0.1.0。设备：STM32H750B-DK，COM3，RJ45 `eth0`。

本页记录使用**本地测试 CA、测试签名密钥和测试 MQTT 凭据**完成的板端网络验收。生产 CA、生产公钥和每设备生产凭据仍未提供，因此不能据此宣称生产 OTA 已就绪。测试材料只存在于忽略的 `.debug/board-ota-accept/`，没有提交仓库。

## 测试环境

- 板端 RJ45 地址由主机 ICS 分配（本轮 192.168.137.56 / .69 变动），主机地址 192.168.137.1。
- 测试 CA 与服务器证书：ECDSA P-256，服务器证书 SAN `IP:192.168.137.1`。
- 固件使用 `VG_OTA_CA_FILE`、`VG_OTA_PUBLIC_KEY_FILE`、`VG_OTA_SIGNING_KEY_ID=board-accept` 构建，`vgota provision status` 显示 `configured=1 trust=1 entropy=1`。
- 测试凭据通过 `scripts/ota/pack_credentials.py` 绑定设备 UID 后由 `scripts/provision-ota.ps1` 导入；UID 不写入证据。
- 主机测试服务是独立 Python 程序：MQTT 3.1.1 over TLS（端口 8883）与 HTTPS（端口 8443），只监听本机地址，Offer 主题为 `vg/<device_id>/ota/offer`，仅 QoS 1、非 retained。
- 下载镜像使用本次构建的 B 槽镜像，`size=1553704`，`sha256=73993b73...`，向量表合法。

## 时间基准修复

板端 `vg_time_sync` 把北京时间墙钟（UTC+8）写入 `CLOCK_REALTIME`，供 UI 和日志直接显示；OTA 协议时间却按 Unix UTC 解释。首次板端 Offer 因此被判为 `queued_offer_expired`，下载后的 manifest 被判为 `manifest_expired_or_time_invalid`。

修复：

- `vg_sntp.c/h` 增加 `vg_time_wall_to_utc()`。
- 新增 [`vg_ota_clock.h`](../../../app/velaguard/vg_ota_clock.h)，在 NuttX 且启用 `CONFIG_VG_TIME_SYNC` 时把协议时间换算为 UTC；host_tests 保持系统时间。
- `vg_ota_service.c`、`vg_ota_staging.c`、`vg_ota_confirm.c`、`vg_ota_https.c` 的墙钟比较改为 `vg_ota_utc_now()`。
- `test_time_sync.c` 增加 wall→UTC 与往返断言；`make -C app/velaguard/host_tests test` 通过。

修复后同一板端构建完成正常下载并进入 READY。

## 结果

| 场景 | task_id | 实际结果 | 证据 |
|---|---|---|---|
| 正常 MQTTS + HTTPS 流式下载 | T-BOARD-NET-06 | READY_AWAITING_CONFIRM，1553704 B，100%，无 BCB；暂存有 MANIFEST/IMAGE/STATE/SUPPRESS | [serial-ready.txt](board-network/serial-ready.txt)、[normal7.log](board-network/normal7.log) |
| READY 10 分钟超时 | T-BOARD-NET-06 | EXPIRED，result=expired，镜像与 manifest 清理，仅剩 STATE/SUPPRESS；bootinfo 仍 A/no-bcb | [serial-expired.txt](board-network/serial-expired.txt)、[expire.log](board-network/expire.log) |
| 超时后重发相同 task_id | T-BOARD-NET-06 | 拒绝，error_code=task_suppressed，状态保持 EXPIRED | [suppressed.log](board-network/suppressed.log) |
| manifest 无效签名 | T-BOARD-FAIL-SIG-01 | 下载 manifest 后拒绝，signature_invalid_or_untrusted，result=failed | [fail-sig.log](board-network/fail-sig.log) |
| HTTPS 自签名证书 | T-BOARD-FAIL-TLS-01 | 拒绝，tls_verification_failed | [fail-selfsigned.log](board-network/fail-selfsigned.log) |
| HTTPS 错误主机名 | T-BOARD-FAIL-TLS-02 | 拒绝，tls_verification_failed | [fail-wronghost.log](board-network/fail-wronghost.log) |
| HTTPS 未知 CA | T-BOARD-FAIL-TLS-03 | 拒绝，error_code=manifest_download_failed（错误归类偏笼统） | [fail-unknownca.log](board-network/fail-unknownca.log) |
| HTTPS 证书已过期 1 小时 | T-BOARD-EXPIRED-CERT-01 | 拒绝，error_code=manifest_download_failed | [fail-expired.log](board-network/fail-expired.log) |
| 下载中断后 Range 续传 | T-BOARD-RANGE-04 | 首包被断开后以 `Range: bytes=517901-`、`If-Range: "img-v1"` 续传，最终 READY 1553704 B | [range4.log](board-network/range4.log) |
| ETag 变化 | T-BOARD-ETAG-01 | 观察到板端丢弃旧 ETag 后从 0 重新请求，再以新 ETag Range 续传；测试服务再次注入中断后 download_retries_exhausted，未取得 READY | [etag.log](board-network/etag.log) |
| MQTTS 每设备鉴权与订阅 | T-BOARD-NET-06 | 测试凭据 CONNACK 成功，订阅主题精确匹配，QoS 1 PUBLISH/PUBACK 正常 | 各日志 connect/subscribe/puback |
| 结果持久补报 | T07-INSTALL-02 | 连接测试 Broker 后补报旧的 failed/interrupted_before_confirmation 结果，收到 PUBACK 后清除 | [normal6.log](board-network/normal6.log) |
| 维护取消并上报 | T-BOARD-RANGE-04 | `vgota decline range_test_cleanup` 后 result=declined；这是维护入口，不是屏幕取消 | [serial-declined.txt](board-network/serial-declined.txt) |

## 本轮发现

1. **已修复：OTA 协议时间基准错误。** 板端墙钟为 UTC+8，OTA 直接与 UTC manifest 比较，导致正常任务被误判过期。修复见上。
2. **未修复：重启后墙钟恢复滞后。** 板端从 `clock.txt` 恢复上次持久化的时间，SNTP 首次同步前可能落后数分钟。若 Offer 在此时到达，刚签发的 manifest 会被判为 `manifest_expired_or_time_invalid`。建议在 `vgota reboot` 前持久化墙钟、缩短持久化间隔，或在 OTA 服务接受 Offer 前等待一次有效校时。
3. **未修复：TLS 证书时间仍使用 UTC+8 墙钟。** mbedTLS 通过 `time(NULL)` 校验证书，板端墙钟比 UTC 快 8 小时；有效期不足 8 小时的证书可能被提前拒绝，`notBefore` 在 8 小时内的证书可能被判为尚未生效。过期证书不会被放宽，但短周期证书会误拒。建议为 mbedTLS 配置 UTC 时间回调，或把系统时钟改为 UTC 并统一显示层加时区。
4. **错误归类。** manifest 下载阶段的未知 CA 和过期证书都归为 `manifest_download_failed`，未保留 `tls_verification_failed`；`OFFER_RECEIVED` 状态可能短暂显示上一任务的错误码，最终错误正确。
5. **ETag 场景只完成部分。** 板端行为符合“ETag 变化后从零重新下载”，但测试服务在第四次请求后再次注入中断，重试预算耗尽；需要更克制的服务器脚本复测一次完整 READY。

## 未完成

- 生产 CA、生产签名公钥和每设备 MQTTS 凭据的受控 provisioning。
- 真实板载输入确认一份签名任务、B 安装、自检和结果上报；本轮所有 READY 都未确认安装。
- 屏幕点击取消、实体按键/触摸长按、10 分钟页面停留的真实触摸验收。
- RJ45 拔插对照、断电注入、循环安装/回滚和并发采集/告警/AI 资源检查。
- 错误密码、明文 MQTT 和 Wi-Fi 出口拒绝的板端矩阵；其中 Wi-Fi 出口拒绝仍需要物理断网线或明确的链路注入方案。
- 生产构建恢复、测试凭据删除和干净启动检查。
