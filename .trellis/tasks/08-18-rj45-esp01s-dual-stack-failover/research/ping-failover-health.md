# PING 故障转移：健康探测与自动恢复

## 要解决的问题

「RJ45 可用」不能只看 PHY link / DHCP。网线插着但上游不通时，仍应切到 ESP-01S。恢复时用同一探测：eth0 上连续 ping 成功满稳定窗口再切回。

## 探测落在哪一层

- **ICMP 发送**：NuttX `netutils` 的 `icmp_ping()`（`SYSTEM_PING` / `NETUTILS_PING` 已在 `velaguard-net`）。这是 POSIX 网络栈能力，只走 **eth0**。
- **策略与自动循环**：contest 应用线程，开机由 `velaguard_app_main` 拉起，**不需要 NSH 调用**。
- **不** 把策略打进 `apps/netutils/netinit`：那是公共仓，难做主机单测，也违反「交付在 contest 仓」。

`netlib_check_ipconnectivity()` 可复用回调形态，但默认 `devname=NULL`。本轮封装 `vg_eth_ping`：目标优先 DHCP 网关，Kconfig 可覆盖；需要绑 eth0 时再开 `NET_BINDTODEVICE`（当前 velaguard-net 未开，单网卡时可先不绑）。

## ESP 侧没有 NuttX ICMP

`lesp_*` 无 ping。ESP 上的 ICMP 只能 `AT+PING`，数据不进 NuttX 路由表。

因此：

- **RJ45 是否健康**：只 ping eth0（网关或覆盖地址）。
- **Wi-Fi 是否可用**：`lesp_ap_is_connected` + 有 IP。本轮不做 AT+PING。
- 切在 Wi-Fi 上时，**恢复探测仍 ping eth0**——这正是「RJ45 是否回来了」。

## 判定

- 一次探测：count=1，timeout 可配（默认 1s）。
- `rj45_ping_ok`：本拍 `nreplies>=1`。
- RJ45 健康：link ∧ has_ip ∧ 连续失败次数 &lt; N（默认 3）。
- 不健康 → 若 Wi-Fi 可关联则活动出口 `wifi`；否则 `none`。
- 恢复：RJ45 连续 ping 成功满稳定窗口（默认 10s）才从 `wifi` 抢回。

## 自动运行

`velaguard_app_main` 创建 `net_mgr` 后即开始采样/ping/策略/拉起或拆除 ESP 关联。NSH `vgnet` 只查询与注入，不是启动条件。
