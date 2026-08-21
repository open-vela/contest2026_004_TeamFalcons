# ESP8266 AT 并行 socket 与 POSIX MQTT-C 不能共用 pal

## 问题

手册要求 RJ45 与 ESP-01S 双出口，活动出口变化后 MQTT 自动重连。

现状：

- RJ45：NuttX ETHMAC + POSIX `socket`/`send`/`recv`。`vgmqtt` 把 POSIX fd 传给 `mqtt_init`。
- ESP-01S：`apps/netutils/esp8266` 的 `lesp_socket` / `lesp_send` / `lesp_recv`。这是模组侧 TCP，数据走 UART AT（`AT+CIPSTART` / `+IPD`），**不是** NuttX netdev，也不是 POSIX fd。
- MQTT-C NuttX pal（`apps/netutils/mqttc/MQTT-C/src/mqtt_pal.c`）对 `mqtt_pal_socket_handle`（`int`）调用 POSIX `send`/`recv`。把 `lesp_socket` 返回值丢进去会发到错误的内核 fd。

`CONFIG_NETUTILS_ESP8266` 默认串口 `/dev/ttyS1`、115200，与已验证的 ESP-01S 接线一致。

## 不做的路

- 把 ESP 做成 NuttX netdev / SLIP/PPP / 定制固件：手册允许但超出本轮，且会扰动已验收的 RJ45 栈。
- 在 contest 应用里重链一份 MQTT-C pal：`mqttc` 已作为 `apps` 库编译，覆盖 pal 不可靠。
- 双链路同时 `mqtt_init`：违反单活动出口。

## 采用的路

Contest 仓对 `mqtt_pal_sendall` / `mqtt_pal_recvall` 打最小 hook patch：

- 若应用注册了发送/接收回调，则走回调（RJ45 → POSIX，Wi-Fi → `lesp_send`/`lesp_recv`）。
- 未注册时保持原 POSIX 行为，`vgmqtt` 一次性工具不回归。
- Patch 放 `scripts/`，幂等 apply，禁止直接改 `apps/` 工作树当提交物。

应用层 `vg_mqtt_transport`：

- `open(egress)` 只打开当前活动出口的一条 TCP。
- 切出口时先 close 旧连接，再 open 新连接，再 `mqtt_reinit` / reconnect callback。
- 使用 MQTT-C 已有的 `mqtt_init_reconnect` / `mqtt_reinit`（见 `MQTT-C/examples/reconnect_subscriber.c`），不要自写第二套 MQTT 状态机。

## 验证含义

主机单测覆盖「该用哪条 egress、何时 reconnect、status.network 填什么」。
pal hook 与 `lesp_*` 路径用编译门禁 + 注入命令保证接到正确后端；不要求 agent 拔线。
