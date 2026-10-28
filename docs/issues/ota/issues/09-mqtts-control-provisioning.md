# 09: MQTTS 控制、每设备 provisioning 与结果上报

**What to build:** 设备通过 MQTTS 订阅 OTA Offer，发布 status、result 和 confirm。每设备 MQTT 凭据来自加密 provisioning，服务器证书和主机名必须验证。OTA 只允许 RJ45 出口，Wi-Fi 出口收到 Offer 时明确拒绝。重连后重新订阅并上报当前状态，不自动安装。`ota/confirm` 只表示新固件自检结果，不能授权安装。

**Blocked by:** 08、02。

**Status:** ready-for-agent

- [ ] 有效 MQTTS OTA Offer 触发下载，status、result 和 confirm 携带 task_id 与版本上报。
- [ ] 无效服务器证书或无效客户端凭据被拒绝，明文 MQTT 上的 OTA Offer 不被接受。
- [ ] Wi-Fi 出口收到 Offer 时拒绝并上报原因，RJ45 出口正常工作。
- [ ] 重连后重新订阅并上报当前状态，不自动安装，不重复弹窗已终止的 task_id。
- [ ] `ota/confirm` 只上报自检成功或失败，没有任何远程安装授权路径。
- [ ] MQTT 密码、完整 URL、API key 和私钥不出现在日志、屏幕或 payload。
- [ ] 板级验收使用本地 MQTTS 测试服务覆盖有效 Offer、无效证书、断线和结果补报。
- [ ] 现有明文遥测、告警、状态和点表主题行为不回归。
