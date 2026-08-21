# Design: 阶段 1 最小网络闭环

## 架构与边界

`velaguard-net` 固件 = velaguard-min 底座（console/NSH、LED、RS485、ESP-01S、PWM、
`velaguard_app_main` 入口 + NSH 线程）+ 网络栈 + `vgmqtt` 工具。
M1 不写业务代码（NSH `ifconfig`/`ping` 即验收）；M2 新增 `vgmqtt`。

## 配置设计（nuttx 侧，patch 交付）

符号集来源：`scripts/windows_build_openvela.ps1` 已验证网络块，按最小化裁剪：

```text
NET / NET_ETHERNET / NET_IPv4 / NET_ARP / NET_TCP / NET_UDP / NET_BROADCAST /
NET_UDP_CHECKSUMS / NET_ICMP / NET_ICMP_SOCKET / NET_SOCKOPTS / NET_ETH_PKTSIZE=1500
STM32H7_ETHMAC / ETH0_PHY_LAN8740A（禁用 ETH0_PHY_LAN8742A）
NETUTILS_NETINIT / NETINIT_THREAD / NETINIT_DHCPC / NETUTILS_DHCPC
SYSTEM_PING（select NETUTILS_PING）
```

- 不引入 `NETUTILS_CJSON`：`vgmqtt` 的 status JSON 用 `snprintf` 手拼固定字段。
- 启用 `NETDB_DNSCLIENT` + `NETINIT_DNS`：`vgmqtt` 经 `getaddrinfo` 解析 broker
  （链接期依赖），DHCP 同时下发 DNS 服务器，IP 字面量与域名均可。
- `NETINIT_THREAD=y`：无网线启动不阻塞，符合 ps1 注释意图。
- 入口保持 `INIT_ENTRYPOINT="velaguard_app_main"`，与 velaguard-min 单一形态一致。
- 交付：`scripts/openvela-velaguard-net-defconfig.patch` +
  `scripts/apply-openvela-velaguard-net-defconfig-patch.sh`（幂等，仿
  `apply-openvela-velaguard-min-defconfig-patch.sh`：reverse-check → apply-check → apply）。

## vgmqtt 设计（app 层）

- 位置：`app/velaguard/vgmqtt.c`；Kconfig 挂 `VG_BRINGUP_TOOLS`（与现有 vg* 工具一致）。
- 用法（NSH）：`vgmqtt -h <host> [-p <port>] [-u <user>] [-P <pass>] -t <topic> -m <json>`；
  支持 `--retain`；LWT 固定为 `vg/{device_id}/status` + `{"online":false}` retained。
- MQTT-C 流程：`mqtt_init` → `mqtt_connect`（明文 1883；LWT 参数齐备）→
  用 `event_connect`/返回码确认连接 → `mqtt_publish`（QoS 1，retained）→ `mqtt_sync` 收尾。
- `device_id`：test 模式 `DEVID` 宏覆盖（缺省如 `vg-test-01`），编译期定。
- JSON 手拼：`{"device_id":..., "online":true, "firmware":"0.1.0", "build_mode":"TEST",
  "network":"rj45", "uptime_ms":..., "ts_ms":...}`。

## 数据流 / 合同

```text
vgmqtt → MQTT-C → 云 Broker（明文测试端口）
  ├─ publish vg/{device_id}/status (retained, QoS 1)   ← 合同 §4.1
  └─ LWT: 同 topic, {"online":false}, retained          ← 合同 §4.1 建议
```

联调顺序（隔离"云"与"板"变量）：

```text
PC: mosquitto_pub/sub 直连云 Broker 验证 topic 权限
  → 板端 vgmqtt 连接 + 发布 status
  → 强制断线，云侧订阅端观察 LWT online:false
```

## 兼容性与回滚

- 不动 `windows_build_openvela.ps1`；ps1 跑一次会把本地配置切回 UI 形态，文档中保留警告。
- `VG_BRINGUP_TOOLS` 默认链不变（LVX demo=y 时默认 y），LVX demo 符号保留。
- nuttx 侧仅新增 defconfig patch，可 `git apply -R` 回滚；app 层改动在队伍仓 git 内可回退。

## 关键权衡

- 明文 MQTT：M2 最小闭环的必要裁剪；TLS 留到下轮，避免把 mbedTLS 调试混进"打通"判定。
- 无自动重连：重连状态机（指数退避）是独立工作块，下轮完整阶段 1 再做。
- DNS 客户端体积小，且是 `getaddrinfo` 链接依赖，不视为范围膨胀。
