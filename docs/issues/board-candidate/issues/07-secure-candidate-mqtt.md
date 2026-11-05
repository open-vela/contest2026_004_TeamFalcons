# 07: 通过专用 MQTTS 操作配置会话

**What to build:** 云端通过专用安全 MQTT 通道完成设备查询、建立会话、下发候选和取消；证书、凭证、ACL 或订阅未就绪时配置服务保持关闭，远程端没有任何确认入口。

**Blocked by:** 04, 下发候选并生成可确认版本.

**Status:** ready-for-agent

- [ ] 板端使用独立身份和方向隔离的配置主题建立 MQTTS 连接，只有有效 CONNACK 和 SUBACK 后才处理请求。
- [ ] 收发回调只做有界复制入队，解析、校验和状态工作由后台完成；遥测、候选和 OTA 会话不会互相踢线。
- [ ] 请求与状态使用协议版本、QoS、非 retained 和身份关联，PUBACK 不被解释为配置生效。
- [ ] 真实 Broker 验证证书、主机名、错误凭证、错设备、错方向、retained 和超长消息，cloud query→open→put→cancel 可完整演示。
