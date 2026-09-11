# ESP-01S 不进 NuttX 协议栈，能否当灾备

## 短结论

**当「灾备」= 板端到 MQTT Broker / AI Bridge 的云通道：合适。**  
**当「灾备」= NuttX 上任意 POSIX 程序（`ping`、`getaddrinfo`、现有 `vgmqtt`）自动改走 Wi-Fi：不合适。**

VelaGuard 手册把 ESP-01 写成 UART AT 备用网，云业务只有 MQTT。按这个产品定义，AT 旁路是对的，不必强行做成 `wlan0`。

## 两条路分别保什么

```text
RJ45  ── STM32 ETHMAC ── NuttX TCP/IP ── socket() ── ping / DNS / vgmqtt
ESP-01S ── USART2 AT ── 模组内 lwIP ── lesp_socket() ── 只有走 lesp_* 的代码
```

切换时 **默认路由不会变**。`icmp_ping` 永远打 eth0。Wi-Fi 关联成功只表示模组拿到了 AP 地址，NuttX `ifconfig` 看不到它，现有 MQTT-C pal 也不会用它。

| 能力 | RJ45 | ESP AT 灾备 |
|---|---|---|
| 本地安全环（采集/告警） | 不依赖网 | 不依赖网（V2，灾备对象本来就不是它） |
| 云 MQTT | POSIX 可直连 | 必须自写 transport，走 `lesp_*` |
| NSH `ping` / DNS | 有 | 无（除非 AT+PING，且不进协议栈） |
| 手机访问板端 LAN 服务 | 有 | 基本没有（MCU 不是 Wi-Fi 上的 IP 主机） |
| OTA/TTS 分片（将来走 MQTT） | 有 | 有，只要 MQTT 走 lesp |

## 为什么对 VelaGuard 仍合适

1. 断网时必须活的是本地环，不经过 ESP。
2. 需要保住的增强能力是 MQTT，不是「整机变成 Wi-Fi 主机」。
3. 现有 AT 1.7.4 硬件已通；做成 netdev 要 PPP/SLIP 或刷定制固件，UART 带宽、模组 RAM、双栈路由都更重。
4. 手册允许「AT 或定制固件」。AT 旁路是明确选项，不是缺陷。

代价：所有上云代码必须认 `active_egress`。漏网的 POSIX 调用在 RJ45 挂了之后会一直失败。本轮若只 ping + join AP，**证明不了云通道**，只证明「探测到 RJ45 挂了并连上 AP」。

## 不合适的情形（若你要这个，应改方案）

- 希望 `ping 8.8.8.8` 在拔网线后仍从 NSH 成功。
- 希望现有 `vgmqtt` 一行不改就能走 Wi-Fi。
- 希望板子在 Wi-Fi 上提供 HTTP/LAN 配置入口。

那就要把 ESP 做成 NuttX 网卡（PPP/SLIP 或定制固件），工作量和风险都高于本轮 ping 策略。openvela 有 `NET_SLIP` / `netutils/pppd`，ESP 股票 AT 并不是现成 PPP 调制解调器。

## 建议

竞赛主路径：**保留 AT 旁路当 MQTT 灾备**；故障转移策略自动跑；POSIX 流量不假装已经切换。  
本轮 ping：只负责 RJ45 健康与自动 join/抢回；MQTT transport 单独立项。  
不推荐为「看起来像双网卡」去刷 ESP 固件。
