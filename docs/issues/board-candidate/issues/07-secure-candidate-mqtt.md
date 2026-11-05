# 07: 通过专用 MQTTS 操作配置会话

**What to build:** 云端通过专用安全 MQTT 通道完成设备查询、建立会话、下发候选和取消；证书、凭证、ACL 或订阅未就绪时配置服务保持关闭，远程端没有任何确认入口。

**Blocked by:** 04, 下发候选并生成可确认版本.

**Status:** complete

- [x] 板端使用独立身份和方向隔离的配置主题建立 MQTTS 连接，只有有效 CONNACK 和 SUBACK 后才处理请求。
- [x] 收发回调只做有界复制入队，解析、校验和状态工作由后台完成；遥测、候选和 OTA 会话不会互相踢线。
- [x] 请求与状态使用协议版本、QoS、非 retained 和身份关联，PUBACK 不被解释为配置生效。
- [x] 真实 Broker 验证证书、主机名、错误凭证、错设备、错方向、retained 和超长消息，cloud query→open→put→cancel 可完整演示。

验证记录：

- 完整主机测试通过；真实 mbedTLS/MQTT 夹具覆盖证书、主机名、错误凭证、拒绝订阅、retained、错设备和超长消息。
- 真机经 RJ45 连接局域网 TLS Broker，完成 query→open→put→awaiting_local_confirm→cancel；取消后点数、ID 和基础摘要不变。
- 真机对错设备、retained 和错方向消息均不返回状态，证实设备侧不会只依赖 Broker ACL。
- 线程生命周期固定在 `velaguard_app_main`，NSH 只保存加密凭据并要求复位。首次真机 put 发现 32 KiB 栈溢出，候选构建对象已移到堆；最终线程栈占用 69.6%。
- 证据见 [config-mqtt-board-accept.log](../../../maintenance-evidence/2026-11-05/config-mqtt-board-accept.log)。
- 外部连接资料当前只提供明文 MQTT/WebSocket，生产 MQTTS Broker 地址、页面和云端 PDF 服务尚未上线；本票以局域网真实 TLS Broker 完成板端软件验收。
