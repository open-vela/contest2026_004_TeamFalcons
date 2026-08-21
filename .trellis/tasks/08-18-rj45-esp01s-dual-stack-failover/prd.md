# 实现 RJ45/ESP-01S TCP 故障转移（ping 判定，自动运行）

## Goal

开机后自动故障转移，无需 NSH：RJ45 异常时，TCP 业务改走 ESP-01S；RJ45 恢复后 TCP 切回 RJ45。用 eth0 ICMP ping 判定 RJ45 是否异常。策略上同时只有一条活动出口。双挂时离线，本地采集不中断。

用户价值：网线形在但上游不通、或 PHY 掉线时，上云 TCP 仍能走 Wi-Fi；RJ45 一恢复就抢回。

质量由实现侧保证：策略与「该走哪条 TCP」用主机单测。物理拔线不是完成门禁。

## Background

- 手册 §3.4 / §8.1 / §16.5：RJ45 优先，异常用 ESP-01，恢复后经稳定窗口切回。用词是故障转移，不是灾备。
- 切的是 **TCP 业务**（ESP AT 的 `lesp_*` 只有 TCP/UDP，没有 ICMP）。NSH `ping` 不会改走 Wi-Fi。
- ping 只打 eth0，作 **RJ45 异常判定**。见 `research/ping-vs-mqtt-difficulty.md`。
- 已有：`velaguard-net` DHCP、`icmp_ping`、`vgmqtt`（POSIX）、ESP AT 1.7.4 + `vgesp`。未开 `NETUTILS_ESP8266`。
- 不引入 ESP_AT_Lib。ESP UART=`/dev/ttyS1`。RST=PH10，EN=PA3。
- 本轮第一号 TCP 客户端用已有 MQTT-C 连测试 Broker：证明「TCP 已在当前出口上」；不做完整云业务 topic。

## Requirements

- R1 开机自动：`velaguard_app_main` 拉 `net_mgr`。NSH 不是启动条件。
- R2 状态：`NET_DOWN` / `NET_CONNECTING` / `NET_ONLINE_RJ45` / `NET_ONLINE_WIFI` / `NET_DEGRADED`。`vgnet status` 可读出口、ping、TCP 是否已连接、原因。
- R3 单活动出口。RJ45 ping 健康 → TCP 走 POSIX/eth0；否则 ESP 已关联 → TCP 走 `lesp_*`；否则不持有 TCP。
- R4 RJ45 健康 = link ∧ DHCP ∧ eth0 ping（连续失败 &lt; 3 才判挂）。目标：DHCP 网关，Kconfig 可覆盖。
- R5 Wi-Fi 上电即连：`net_mgr` 启动后对 ESP-01S（不是换 ESP32 模组）调用 `lesp_initialize` + `lesp_ap_connect`。测试 AP：SSID `xxx`，PSK `1472583690`（Kconfig 默认，可改）。关联与 RJ45 是否健康无关（热备）；TCP 仍仅在 RJ45 不健康时才走到 `lesp_*`。
- R6 即使 TCP 已在 ESP 上，仍 ping eth0。连续成功满 10s 窗口才把 TCP 关旧开新、切回 RJ45。
- R7 切出口：先关当前 TCP，再开新出口上的一条 TCP。禁止两条同时连。
- R8 退避分 bearer：1s×2，上限 60s，抖动 ±20%；稳定 5 分钟重置。RJ45 link 沿 down→up 立即探测一次。
- R9 ESP：现有 ESP-01S AT + `lesp_*`。关联失败达阈值软复位，GPIO 硬复位可选。`vgnet wifi` 可覆盖 RAM 中凭据。
- R10 第一号 TCP 用户：MQTT-C 连 Kconfig Broker（明文），等 CONNACK；切出口后重连。`vgmqtt` 仍是一次性调试命令。不做 TLS、不做 AI topic。
- R11 离线不拖死主循环 / NSH / `vgmodbus`。
- R12 主机单测覆盖策略与「reconnect 必须带新 egress」。`vgnet inject` 可选。交付在 contest 仓 + 必要 patch。

## Acceptance Criteria

- [ ] AC1 单测：RJ45 ping 健康 → 出口 `rj45`，TCP 请求走 `posix`。
- [ ] AC2 单测：连续 ping 失败且 Wi-Fi 可用 → 出口 `wifi`，TCP 请求走 `lesp`；同时只有一条。
- [ ] AC3 单测：双挂 → `NET_DOWN`，无 TCP。
- [ ] AC4 单测：ping 恢复未满窗口保持 `lesp`；满窗口切回 `posix`；flap 不抖。
- [ ] AC5 单测：分 bearer 退避；link 恢复立即探测。
- [ ] AC6 单测：出口变化产生 `tcp_reconnect`，新 egress 与旧连接已关闭。
- [ ] AC7 单测：Wi-Fi 失败达阈值 → 复位请求。
- [ ] AC8 固件编译通过；上电即跑 net_mgr；不敲 NSH 也会按默认凭据尝试 join `xxx`；`vgnet` 非启动条件。
- [ ] AC9 MQTT-C 在当前出口上等 CONNACK 才算 TCP 在线；切出口后必须重连。status 若发布则 `network=rj45|esp01|none`。
- [ ] AC10 patch 幂等（若有）。

## Out of Scope

- 让 NSH `ping` 走 ESP / AT+PING / PPP/SLIP netdev。
- ESP_AT_Lib、定制 ESP 固件。
- TLS、AI topic、OTA、LVGL、config_store。
- 双 TCP 同时连。
- Agent 物理拔线。
- 把策略打进 `netinit`。

## Key Decisions

| 决策 | 结论 |
|---|---|
| 用词 | 故障转移 |
| 切什么 | TCP 业务（第一用户：MQTT-C） |
| ping 角色 | 只判定 RJ45 异常/恢复 |
| ESP 路径 | `lesp_*` AT，不进 NuttX 栈 |
| 自动运行 | 入口线程拉起 |
| ESP_AT_Lib | 不用 |
| 稳定窗口 | 10s |
| ping 失败阈值 | 3 |
| 切回后 ESP | 上电即 join，保持关联（热备）；TCP 只在活动出口 |
| 测试 AP | SSID `xxx` / PSK `1472583690`（仅测试，Kconfig） |
| 模组 | 仍是 ESP-01S（USART2），不换 ESP32 |

## Risks / Deferred

- POSIX 程序（NSH `ping`、旧 `vgmqtt` 手工跑）不会自己改走 ESP。
- 无 AP/Broker 时靠单测 + inject；现场联调非门禁。
- `lesp` 与 `vgesp` 争 `/dev/ttyS1`。
- pal hook：MQTT-C 默认 `send/recv` 不能吃 `lesp` 句柄，见 `research/esp8266-vs-posix-dual-transport.md`。
- 阶段 1 PUBACK 回程不稳：CONNACK + QoS0 status 即可。
