# VelaGuard MQTT 看板上报合同（v2）

| 项 | 内容 |
|----|------|
| 状态 | 板端四主题已落地（2026-09-14）；云看板消费契约见 mimo2mqtt `docs/dashboard-api.md` |
| 依据 | 手册 §8.3、§16.1–16.3；看板 `dashboard-api.md` 第 1–2 节 |
| 冲突 | 载荷与 QoS 以看板契约为准；本文件记录板端实现与未做项 |

## 1. 架构

```text
VelaGuard 板端 ──MQTT──▶ Broker 8.148.67.174:1883 ◀──只读订阅── 云看板
```

- LLM 走板载 `ai_agent` HTTPS 直连 MiMo，不走 MQTT（手册 §8.2、V4）。
- MQTT 只承载 status / telemetry / alarm / point_table。OTA 主题未实现。
- 本地安全环不依赖 MQTT。云端只读，不向 `vg/{device_id}/...` 发布。

## 2. 连接

| 项 | 现行实现 |
|----|----------|
| Broker | `CONFIG_VG_MQTT_BROKER_HOST` 默认 `8.148.67.174`，端口 1883，明文 |
| Username / Password | Kconfig 测试凭据 `velaguard` / 对应口令；赛后轮换。日志不打印口令 |
| `device_id` / `client_id` | 同一字符串：`vg-` + STM32 96 位 UID 的 24 位小写 hex。空的 `CONFIG_VG_MQTT_DEVICE_ID` 表示自动派生；非空则覆盖（host/测试） |
| Clean session | `true` |
| Keepalive | 60 s |
| LWT | `vg/{id}/status` retained，`{"device_id":"...","online":false}` |
| 下行订阅 | 无 |

量产 HMAC token、MQTTS、每设备 ACL 仍属 C2，本跳不做。

## 3. 板端已发布主题

根：`vg/{device_id}/...`，无环境前缀。

| Topic | 方向 | QoS | retained | 板端行为 |
|-------|------|----:|----------|----------|
| `vg/{id}/status` | 设备→云 | 0 | 是 | CONNACK、切网、每 30 s |
| `vg/{id}/telemetry` | 设备→云 | 0 | 否 | 每 30 s 读 `values.txt`；有值点每次都带；空值只在有值→没值时带一次，连续空值省略；过滤后为空则本周期不发 |
| `vg/{id}/alarm` | 设备→云 | 1 | 否 | `vg_runtime` raise/clear 边沿；RAM 8 条，恢复后补发；断电不补；不等 PUBACK |
| `vg/{id}/point_table` | 设备→云 | 1 | 是 | 已确认表非空时：CONNACK / 周期补发 / `apply --confirm` |

未实现：`event`、`diagnosis`、`ai/*`、`tts/*`、`config/candidate`、`ota/*`。v1 合同里的 AI Bridge 主题已从产品移除。

## 4. 载荷

与看板 `dashboard-api.md` §2 对齐。

- status：`device_id`、`online`、`firmware`、`build_mode`、`network`（`rj45\|esp01\|none`）、`uptime_ms`、`ts_ms`、`time_quality`。
- telemetry：顶层数组 `{id,value,ok,age_ms}`。读失败时 `value` 为 JSON `null`，`ok` 为 false。同一点位连续空值不重复上报：仅当上次成功发出去的是有值时才带一次 null。从未有值的空点省略。过滤后数组为空则本周期不发 telemetry。
- alarm：`state` 为 `raised`/`cleared`；`kind` 为 `offline` / `threshold_high` / `threshold_low`；另加 `level`（`warn`/`crit`/`offline`）。`alarm_id` 为 `{device_id}-{boot_id}-{seq}`，`boot_id`/`seq` 仅 RAM。
- point_table：已确认 `points.json` 原文。`points` 为空则不发。

发布只在 `vg_net_mgr` 线程。采集/HMI 只入队。`g_tx` 10 KiB BSS。队列满丢 telemetry、保 alarm。

## 5. 联调

板上 `vgprovision uid` 打出 12 字节 hex，看板设备名为 `vg-` 加上同一串小写 hex。

常驻会话由 `vg_net_mgr` 自动建连。一次性调试仍可用 `vgmqtt`（默认连同一 Broker 与凭据；正常退出不触发 LWT）。
