# 08: HTTPS 流式下载与生产信任

**What to build:** 设备通过 HTTPS 流式下载 Staging Image，不要求整份镜像进入 RAM。TLS 严格验证服务器证书、主机名和有效期，OTA 主机限制在 provisioning 的允许列表内。manifest 使用生产公钥验证，镜像在下载过程中和写盘回读后都计算 SHA-256。支持 Range 时续传，不支持或 ETag 变化时从零开始。有效任务在本地确认后完成安装；无效证书、签名、Device ID、型号、槽位、版本、时间窗或摘要都被拒绝。

**Blocked by:** 07。

**Status:** ready-for-agent

- [ ] 有效 HTTPS OTA Offer 完成流式下载、manifest 验签、回读摘要、READY、本地确认和 B 槽安装。
- [ ] 无效服务器证书、错误主机名、过期证书或未知 CA 被拒绝，不使用 `VERIFY_OPTIONAL` 或未知证书回退。
- [ ] 无效签名、错误 Device ID、错误型号、错误槽位、过期 manifest 或摘要不匹配被拒绝。
- [ ] 下载中断后按 Range 续传或安全地从零开始，未完成下载不产生 pending BCB。
- [ ] 写盘后回读摘要不匹配时拒绝任务，当前 A 槽继续运行。
- [ ] 完整 URL、query token、MQTT 密码和 API key 不出现在日志、屏幕或状态 payload。
- [ ] 板级验收使用本地 HTTPS 测试服务覆盖成功、断线、无效证书和无效签名。
- [ ] 主机测试覆盖下载状态、续传绑定、回读摘要和拒绝原因。
