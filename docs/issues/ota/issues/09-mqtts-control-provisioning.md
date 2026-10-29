# 09: MQTTS 控制、每设备 provisioning 与结果上报

**What to build:** 设备通过 MQTTS 订阅 OTA Offer，发布 status、result 和 confirm。每设备 MQTT 凭据来自加密 provisioning，服务器证书和主机名必须验证。OTA 只允许 RJ45 出口，Wi-Fi 出口收到 Offer 时明确拒绝。重连后重新订阅并上报当前状态，不自动安装。`ota/confirm` 只表示新固件自检结果，不能授权安装。

**Blocked by:** 08、02。

**Status:** in-progress

- [ ] 有效 MQTTS OTA Offer 触发下载，status、result 和 confirm 携带 task_id 与版本上报。
- [ ] 无效服务器证书或无效客户端凭据被拒绝，明文 MQTT 上的 OTA Offer 不被接受。
- [ ] Wi-Fi 出口收到 Offer 时拒绝并上报原因，RJ45 出口正常工作。
- [ ] 重连后重新订阅并上报当前状态，不自动安装，不重复弹窗已终止的 task_id。
- [ ] `ota/confirm` 只上报自检成功或失败，没有任何远程安装授权路径。
- [ ] MQTT 密码、完整 URL、API key 和私钥不出现在日志、屏幕或 payload。
- [ ] 板级验收使用本地 MQTTS 测试服务覆盖有效 Offer、无效证书、断线和结果补报。
- [ ] 现有明文遥测、告警、状态和点表主题行为不回归。

## 2026-10-29 无人值守进展

- [x] 独立严格 TLS MQTT-C 会话、设备凭据导入、精确 Offer 订阅及确定性校验已实现。
- [x] 真正的主机 MQTTS + HTTPS + 签名 manifest 流程到 READY；拒绝、跨重启重复任务拒绝、合成 B 自检结果、断线与 PUBACK 丢失补报测试通过。
- [x] 结果持久队列、队列满拒绝、任务终止日志上限、无远程安装授权路径与 Agent C 门禁通过主机测试。
- [x] A/B 生产构建通过，默认不含测试信任和凭据。
- [ ] 原验收清单中真机网络、真实 B 自检上报、与采集/告警/AI 同时运行的项目仍待验收；主机证据不代替。

契约见[控制协议](../../../velaguard-ota-control-protocol.md)，证据见[T09](../../../ota-evidence/2026-10-29/t09-control-host.md)，人工和外部条件见[待办](../acceptance-todo.md)。
