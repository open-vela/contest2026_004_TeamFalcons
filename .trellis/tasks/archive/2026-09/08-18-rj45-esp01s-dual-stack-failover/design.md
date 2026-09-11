# Design: TCP 故障转移（ping 判定 RJ45）

## 分层

```text
判定：eth0 icmp_ping          ← 不故障转移 ping 命令
策略：vg_net_policy           ← 选 active_egress
承载：vg_tcp_transport        ← posix | lesp，同时只一条
应用：MQTT-C（第一用户）      ← CONNACK = TCP 在该出口上在线
```

```text
velaguard_app_main
 ├─ NSH（vgnet 观察/注入，不启动 mgr）
 ├─ net_mgr：ping → policy → ESP 关联 → TCP 关旧开新
 └─ LED 主循环
```

不引入 ESP_AT_Lib。不改 `vgmqtt` 一次性语义。

## 数据流

```text
eth0 ping ──┐
esp assoc ──┼→ policy → active_egress
inject ─────┘         │
                      ├ rj45: POSIX TCP → Broker
                      ├ wifi: lesp TCP → Broker
                      └ none: 无 socket
```

Wi-Fi 期间恢复探测仍 ping eth0。

## 策略（单测对象）

输入：`rj45_link/has_ip/ping_ok`，`wifi_assoc/has_ip`，inject，`now_ms`。  
输出：`state`，`active_egress`，`tcp_reconnect`，`tcp_backend`（`posix|lesp|none`），`request_esp_reset`。

规则同前：连续 3 次 ping 失败才判 RJ45 挂；从 wifi 抢回需 10s 窗口；切出口先关 TCP 再开；无 MQTT 专用状态，只有 `tcp_reconnect`。

## TCP 传输

`vg_tcp_open(egress, host, port)` / `vg_tcp_send` / `vg_tcp_recv` / `vg_tcp_close`。

- `rj45` → POSIX `socket/connect`，可设 RCVTIMEO。
- `wifi` → `lesp_socket/lesp_connect`。
- MQTT-C：contest patch 给 pal 加回调，指向当前 transport。未注册时 `vgmqtt` 仍 POSIX。

切出口：close → 清 pal → open 新 backend → `mqtt_reinit` + CONNECT。CONNACK 前不算在线。status QoS0 retained，`network` 随 egress。

## Ping / ESP / 线程

- `icmp_ping` count=1，目标 DHCP 网关。
- 模组仍是 ESP-01S（USART2 `/dev/ttyS1`），不换 ESP32。
- 上电即 `lesp_initialize` + `lesp_ap_connect("xxx", "1472583690")`，与 RJ45 无关（热备）。失败退避重试。
- 切回 RJ45 后保持 Wi-Fi 关联；TCP 只挂活动出口。
- `net_mgr` 由 `velaguard_app_main` 创建，不依赖 NSH。

## 权衡

| 选项 | 结论 |
|---|---|
| 切 ping 命令 | 否，比 TCP 难且 lesp 无 ICMP |
| 切 TCP | 是，与「用 ESP 的网络」一致 |
| 第一用户 | MQTT-C，复用已有 Broker |
| 测试 AP | 上电 join `xxx` / `1472583690` |
