# 阶段 1 最小网络闭环：velaguard-net 预设 + vgmqtt（RJ45→MQTT→LWT）

## Goal

打通推进方案 §11.1 的"第一关键门槛"：让 H750B-DK 通过 RJ45 以太网获得 IP，
并用 MQTT-C 客户端连上已部署的云 Broker，发布 retained `status` 并验证 LWT 离线消息。
交付一个干净、可复现、可演示的 `stm32h750b-dk:velaguard-net` 固件基线，
为阶段 2（Modbus）与阶段 3（AI 诊断）铺路。

## Background / confirmed facts

- 上游 `lvgl` defconfig 不含任何网络符号；网络仅由 `scripts/windows_build_openvela.ps1`
  构建时用 kconfig-tweak 现场开启（RJ45 + LAN8740A PHY + DHCPC + NETINIT_THREAD）。
- nuttx/apps 工作树已应用以太网补丁：`scripts/openvela-eth-mii-stm32h750b-dk.patch`
  （PHY 链路轮询）与 `scripts/openvela-netinit-carrier-poll.patch`（carrier/DHCP 重连），
  但 RJ45 实测未在任何文档记录（`docs/velaguard-mqtt-contract.md` 的"以太网 DHCP+DNS"未勾选）。
- 仓库规矩：nuttx 公共仓零直改，所有 nuttx 侧改动以 `scripts/*.patch` + 幂等 apply 脚本交付。
- `apps/netutils/mqttc`（MQTT-C）支持 QoS 0/1、LWT（`will_topic/will_message/will_qos/will_retain`）、
  mbedTLS 开关，且已带 `event_connect` 连接事件补丁。
- `docs/velaguard-mqtt-contract.md` §4.1 定义 `status` 消息与 LWT 建议（同一 topic、`online:false`、retained）。
- 项目手册 §16.1：测试阶段允许 `DEVID` 编译期覆盖 `device_id`。
- 当前 `nuttx/.config` = velaguard-min（`INIT_ENTRYPOINT="velaguard_app_main"`、`VG_BRINGUP_TOOLS=y`、无 NET 栈）。
- 云 Broker 已部署并开放明文 MQTT 测试端口（用户确认）；SSH/账号信息执行阶段经安全渠道提供。
- vgmqtt 使用 `getaddrinfo` 解析 broker 地址，链接期即需要 DNS 客户端，
  故 `velaguard-net` 预设已启用 `NETDB_DNSCLIENT` + `NETINIT_DNS`（DHCP 下发 DNS）。

## Requirements

- R1 新建 `stm32h750b-dk:velaguard-net` defconfig（patch + 幂等 apply 脚本交付）：
  以 velaguard-min 为底，增量加网络符号集（抄 ps1 已验证块）：
  `NET` 栈（IPv4/ARP/TCP/UDP/ICMP/BROADCAST/UDP_CHECKSUMS/SOCKOPTS/`NET_ETH_PKTSIZE=1500`）、
  `STM32H7_ETHMAC`、`ETH0_PHY_LAN8740A`（禁用 LAN8742A）、
  `NETUTILS_NETINIT`+`NETINIT_THREAD`+`NETINIT_DHCPC`、`NETUTILS_DHCPC`、`SYSTEM_PING`。
  不引入 LVGL/触摸/urandom；`INIT_ENTRYPOINT="velaguard_app_main"` 保持。
- R2 M1 现场验收：插网线（PC 共享 WiFi，桥接优先/ICS 备用）→ NSH `ifconfig` 拿到 IP →
  `ping` 通网关 → `ping 8.8.8.8` 通过。
- R3 M2 代码：`app/velaguard` 新增 `vgmqtt` NSH 工具（挂 `VG_BRINGUP_TOOLS`，基于 MQTT-C），
  命令行参数化 broker 地址/端口、topic、payload；支持明文连接云 Broker 测试端口。
- R4 `vgmqtt` 发布 retained `status`（合同 §4.1 格式；`device_id` 用 test 模式 `DEVID` 覆盖；
  `network=rj45`），连接时带 LWT（同一 topic、`online:false`、retained）。
- R5 联调顺序隔离变量：先 PC 端 `mosquitto_pub/sub` 验证云 Broker 与 topic 权限，
  再上板端 `vgmqtt`。
- R6 文档交付：known-issues/README 记录 `velaguard-net` 预设、切换命令、ps1 警告与验收方法。

## Acceptance Criteria

- [x] AC1 容器内 `tools/configure.sh -e stm32h750b-dk:velaguard-net` + `make olddefconfig` + 全量 `make` 通过。
- [x] AC2 defconfig 网络 keep 清单齐全（NET/ETHMAC/LAN8740A/DHCPC/NETINIT/PING），
  无 LVGL/触摸/urandom 符号；`INIT_ENTRYPOINT="velaguard_app_main"`。
- [x] AC3 现场 M1：DHCP 拿到 IP，ping 通网关，ping 通 1.1.1.1。
- [x] AC4 现场 M2：`vgmqtt` 明文连云 Broker 成功 → retained `status` 云侧可见 →
  拔线 → 云侧看到 LWT `online:false`（观察端 [151.3s]/[174.0s] 双事件确认）。
- [x] AC5 patch 幂等：apply 脚本连续执行两次，第二次输出 already applied。

## Out of Scope

- ESP-01 Wi-Fi 备用链路、LVGL 首页/系统状态页、`config_store`、指数退避自动重连、TLS/MQTTS。
- AI 请求/响应闭环（阶段 3）、Modbus 采集（阶段 2）、手机远程配置。

## Key Decisions

| 决策 | 结论 |
|---|---|
| 范围 | 最小网络闭环（RJ45→MQTT），不做双模/UI/存储 |
| 里程碑 | M1 以太网链路；M2 MQTT 客户端（`vgmqtt`） |
| 配置形态 | 新预设 `velaguard-net`，patch + 幂等 apply 交付，无 LVGL |
| 拓扑 | PC 共享 WiFi 给板端网口；桥接优先、ICS 备用（实测现场定，交叉验证分锅） |
| Broker | 已部署云 AI Bridge 的明文 MQTT 测试端口；TLS 后续 |
| 客户端栈 | `netutils/mqttc`（MQTT-C） |
| 代码形态 | `app/velaguard` 新增 `vgmqtt`（`VG_BRINGUP_TOOLS` 下，NSH 命令式） |
| M2 验收 | 连接 + retained status + LWT 离线验证（断线重连下轮） |
| 分工 | 容器出固件 + 编译；用户现场验收；SSH 联调云侧日志我来核对 |

## Risks / Deferred

- RJ45 驱动/PHY 是本轮最大未知数（补丁已应用但实测未记录）；M1 失败时用"换设备插同一根线"交叉验证分锅。
- 桥接 vs ICS 未最终选定，默认桥接优先。
- Broker 地址（IP 或域名均可，DNS 已启用）、测试账号/匿名方式、`DEVID` 具体值：
  执行阶段向用户确认（外部事实，非设计决策）。
