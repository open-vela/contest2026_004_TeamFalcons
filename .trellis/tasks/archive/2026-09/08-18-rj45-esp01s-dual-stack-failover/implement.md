# Implement: TCP 故障转移

## 有序清单

1. `vg_net_policy.c`：ping 判定 + `tcp_reconnect`/`tcp_backend`。无 NuttX。
2. `host_tests`：AC1–AC7。全绿再写胶水。
3. `vg_eth_ping` / `vg_eth_bearer`。
4. defconfig：`NETUTILS_ESP8266` ttyS1 115200。幂等 apply。
5. `vg_esp_bearer`：`lesp_*`，与 `vgesp` 互斥。
6. `vg_tcp_transport`：posix | lesp，同时一条。
7. mqtt pal hook patch；MQTT-C 常驻于 net_mgr，CONNACK 才在线。
8. `vg_net_mgr` + `velaguard.c` 自动 `pthread_create`。
9. `vgnet status/inject/wifi`。
10. Makefile / CMakeLists / Kconfig：SSID 默认 `xxx`，PSK 默认 `1472583690`，Broker、ping 主机。
11. `bash scripts/build.sh net`。
12. known-issues：故障转移、ping 只作判定、NSH ping 不切 ESP、ESP_AT_Lib 不用。
13. trellis-check。

## 验证

```bash
make -C app/velaguard/host_tests test
bash scripts/build.sh net
```

## 风险文件

`vg_net_policy.c`，`vg_tcp_transport.c`，mqtt pal patch，`velaguard.c`，`/dev/ttyS1`。

## start 前

- [x] 需求已收口为 TCP 故障转移，ping 仅判定
- [x] 用户批准本摘要后才 `task.py start`

## 回滚

单测红停胶水。pal 搞坏 `vgmqtt` 则卸回调并回滚 patch。ESP worker 卡死则停 lesp，RJ45 TCP 仍须可用。
