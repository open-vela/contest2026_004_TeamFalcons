# VelaGuard MQTT 与 AI Bridge 合同（v1）

| 项 | 内容 |
|----|------|
| 状态 | 合同文档；板端 MQTT 客户端待 RJ45/ESP 联调 |
| 依据 | `VelaGuard_项目手册.md` §8.3–8.6、issue #07、ADR 0002 |
| 无网线阶段 | 可用主机 broker + `tools/ai_bridge_stub` 自测协议 |

## 1. 架构

```text
VelaGuard (板)  --MQTT-->  Broker  <--MQTT--  AI Bridge (主机/云)
                                              |--HTTPS--> MiMo / TTS / 手册解析
```

- 板端**不**直连 MiMo HTTPS。
- 正式：MQTTS + 每设备 token；试验：受控局域网可明文 MQTT。
- 本地安全环（采集/告警/日志）不依赖 MQTT。

## 2. 身份与鉴权

| 项 | 规则 |
|----|------|
| `device_id` | 量产由 STM32 UID 派生；运行时不可改（ADR 0003） |
| Client ID | 建议固定 `vg-{device_id}` |
| Username | `device_id` |
| Password | 版本化 HMAC token：`HMAC-SHA256(PRODUCT_AUTH_SECRET, "velaguard:mqtt:v1:" + device_id)`（手册 §16.1） |
| Clean session | v1：`true`，重连后重新订阅 |
| LWT | 见 status |

试验构建可用配置文件覆盖 broker 地址与明文密码，**不得**进量产默认。

## 3. Topic 树

根：`vg/{device_id}/...`（无环境前缀）

| Topic | 方向 | QoS | retained | 用途 |
|-------|------|----:|----------|------|
| `vg/{id}/status` | 设备→云 | 0 | **是** | 在线/链路/版本摘要 |
| `vg/{id}/telemetry` | 设备→云 | 0 | 否 | 周期采样摘要 |
| `vg/{id}/alarm` | 设备→云 | 1 | 否 | 告警事件 |
| `vg/{id}/event` | 设备→云 | 1 | 否 | 通用结构化事件 |
| `vg/{id}/ai/request` | 设备→云 | 1 | 否 | AI 诊断/配置请求 |
| `vg/{id}/ai/response/{req_id}` | 云→设备 | 1 | 否 | AI 结构化响应 |
| `vg/{id}/config/candidate` | 云→设备 | 1 | 否 | 候选传感器配置 |
| `vg/{id}/tts/request` | 设备→云 | 1 | 否 | TTS 请求 |
| `vg/{id}/tts/response/{req_id}` | 云→设备 | 1 | 否 | TTS/分片元数据 |
| `vg/{id}/ota/*` | 双向 | 1 | 否 | 见手册 §8.6 |

## 4. 消息 JSON 骨架

### 4.1 status（retained）

```json
{
  "device_id": "vg-...",
  "online": true,
  "firmware": "0.1.0",
  "build_mode": "PRODUCTION",
  "network": "rj45|esp01|none",
  "uptime_ms": 12345,
  "ts_ms": 0
}
```

LWT 建议同一 topic，payload：`{"device_id":"...","online":false}`，retained=true。

### 4.2 ai/request（诊断最小集）

```json
{
  "req_id": "req-001",
  "type": "diagnosis",
  "device_id": "vg-...",
  "alarm": {
    "level": "WARNING",
    "summary": "温度偏高",
    "temp_c": 72.5,
    "threshold_c": 70
  },
  "context": {
    "backend": "MOCK",
    "recent_events": []
  }
}
```

### 4.3 ai/response/{req_id}

```json
{
  "req_id": "req-001",
  "type": "diagnosis",
  "ok": true,
  "summary": "温度持续超过阈值",
  "risk_level": "medium",
  "possible_causes": ["负载偏高", "散热异常"],
  "recommended_actions": ["检查负载", "检查散热"],
  "need_shutdown": false,
  "confidence": 0.7
}
```

- `req_id` **必须**与响应 topic 后缀 `vg/{device_id}/ai/response/{req_id}` 一致。
- 主机 stub 可额外带 `"source": "ai_bridge_stub"`；真实 Bridge 可省略。解析端应忽略未知字段。
- AI 输出仅建议；写入配置/控制必须本地确认（ADR 0004）。

## 5. 主机侧无板联调

见 `tools/ai_bridge_stub/README.md`：

1. 本机启动 Mosquitto（或任意 MQTT broker）
2. 运行 `bridge_stub.py` 订阅 `vg/+/ai/request` 并回假诊断
3. 用 `mosquitto_pub` 模拟板端发请求

板插入 RJ45 后：再实现 issue #06/#07 真连接，合同不变。

## 6. 板端后续实现清单（有网线时）

- [ ] 以太网 DHCP + DNS
- [ ] MQTT 客户端（试验明文 / 量产 TLS）
- [ ] 订阅 `ai/response/+`、`config/candidate`、OTA 相关
- [ ] 发布 status / telemetry / alarm
- [ ] UI「网络」从离线 stub 改为真实状态
- [ ] 连接/断开写 `events.jsonl`

## 7. 与当前固件关系

| 现状 | 说明 |
|------|------|
| 采集/告警 | 本地 mock，已独立 |
| 网络 UI | 显示「离线」 |
| 本文件 | 协议冻结，便于并行开发云侧 |
