# ping 实现故障转移，会不会比 MQTT 难

需求拆开后是两件事，难度相反。

## 1. 用 ping 判断「RJ45 是否异常」（探测）

**比 MQTT 容易。**

NuttX 已有 `icmp_ping()`，只打 eth0。连续失败 → RJ45 异常；连续成功满窗口 → 恢复。不经过 ESP，也不需要 `lesp_*`。

这是正确的触发器，不是「使用 ESP 网络」本身。

## 2. 让 NSH `ping` 在故障转移后走 ESP-01S（把 ping 当业务流量）

**比 MQTT 难，而且难一截。**

| | MQTT | NSH `ping` 走 ESP |
|---|---|---|
| 协议 | TCP | ICMP |
| `lesp_*` | 有 `lesp_socket/connect/send/recv` | **没有** ICMP |
| 现成客户端 | 已有 MQTT-C + `vgmqtt` | `ping` 绑死 POSIX 栈 |
| 做法 | pal 按出口分发 | 要么 AT+PING（NSH `ping` 仍失败），要么 PPP/SLIP 网卡 |

所以「在 ping 命令下实现使用 ESP 网络」不是捷径。

## 3. 故障转移后真正「使用 ESP 的网络」

需求是：RJ45 异常 → 用 ESP-01S 的网络；正常 → 回到 RJ45。

ESP AT 能载的是 **模组内 TCP**。MQTT 正是 TCP，比 ICMP 贴合。

推荐组合（由易到难）：

1. ping 只负责判定 RJ45 异常/恢复（易）
2. 切出口后，上云流量走 MQTT/`lesp_*`（中）
3. 不要做 NSH `ping` 透明走 Wi-Fi（难，等于整栈网卡）

只做 1 而不做 2：只能证明「连上了 AP」，不能证明「用上了 ESP 的网络」。
