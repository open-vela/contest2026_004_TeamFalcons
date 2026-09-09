# 从插上网线到云端收到"设备在线"：velaguard-net 最小网络闭环拆解

> 面向初学者的垂直技术博客。本文以仓库里一个真实完成的任务为蓝本：
> Trellis 任务 `08-14-stage1-net-mqtt-min-loop`（阶段 1 最小网络闭环：
> RJ45 以太网 → MQTT → LWT 遗嘱消息），对应提交
> `4954841 feat(net): stage-1 RJ45+MQTT min loop`。
>
> **现行规则（2026-09）：** 公共仓改动在 nuttx/apps/MQTT-C 树上直接修改，
> **不要**再 apply 文中提到的 `scripts/openvela-*.patch`。下文 patch 文件名仅作当时溯源。
>
> 本文不打算教你"照着敲就能跑"，而是想讲清楚：每一条改动**为什么存在**、
> 它涉及的**接口是什么**、调用时要**传什么参数**。所有结论都可以在仓库
> 源码里翻到，文末附了完整阅读清单。

## 0. 先看全貌：这个任务到底改了什么

### 0.1 一句话目标

让 STM32H750B-DK 开发板通过 RJ45 网线获得 IP，然后用一个 MQTT 客户端连上
部署在云端的 Broker，发布一条 retained（保留）的 `status` 消息；再验证一个
关键机制：**拔掉网线后，云端能在十几秒内收到设备"掉线"的遗嘱消息（LWT）**。

整条链路可以画成一句话：

~~~text
板子 RJ45 ── TCP/IP ──> 云 Broker ──> 你的手机/PC 上的订阅端
              │
              └── 拔线瞬间：Broker 替设备发布 {"online":false}
~~~

任务拆成两个里程碑，刻意隔离变量：

| 里程碑 | 内容 | 验收方式 | 为什么这么拆 |
|---|---|---|---|
| M1 | 以太网链路打通 | NSH 里 `ifconfig` 拿到 IP、`ping` 通网关、`ping` 通公网 IP | 先证明"板子的网口硬件 + 驱动 + DHCP 环境"可用 |
| M2 | MQTT 客户端打通 | 板端发布 status，云侧订阅端看到；拔线后看到 LWT | 再证明"应用层协议 + 云 Broker"可用 |

M1 失败时问题在本地，M2 失败时问题可能在板端也可能在云端。两阶段分开，
排查范围立刻缩小一半——这是嵌入式联调里最重要的习惯之一。

### 0.2 改动清单（9 个文件，+726/-24）

| 交付物 | 文件 | 一句话作用 |
|---|---|---|
| 新配置预设 | [openvela-velaguard-net-defconfig.patch](../../scripts/openvela-velaguard-net-defconfig.patch) | 把"以太网需要哪些 menuconfig 选项"固化成可重复应用的补丁 |
| 幂等安装脚本 | [apply-openvela-velaguard-net-defconfig-patch.sh](../../scripts/apply-openvela-velaguard-net-defconfig-patch.sh) | 一个命令把配置打上去，重复执行也安全 |
| 新应用 | [velaguard_mqtt.c](../../app/velaguard/velaguard_mqtt.c) | `vgmqtt`：NSH 里的 MQTT 调试命令 |
| 应用挂载 | [Makefile/CMakeLists.txt/Kconfig](../../app/velaguard/Makefile) | 让新工具进固件、进 NSH 命令表 |
| 构建脚本 | [build.sh](../../scripts/build.sh) | 从单目标变多目标（net/min/lvgl），加防呆。现用名 `scripts/build.sh`（旧名 `build_minimal.sh`） |
| 编辑器任务 | [tasks.json](../../.vscode/tasks.json) | VS Code 构建时弹窗选目标 |
| 文档 | [velaguard-bringup-known-issues.md](../velaguard-bringup-known-issues.md) §5 | 预设用法、验收方法、实测踩坑记录 |

### 0.3 为什么"先打通再优化"

任务的完整愿景是 Modbus 采集、AI 诊断、断线自动重连、TLS 加密。但第一版
只做"最小闭环"：明文 MQTT、无重连、无 UI、无存储。理由很朴素：**任何一个
环节没打通，后面所有功能都无从验证**。网口驱动没跑通就去写重连状态机，
你根本分不清失败是网口问题还是重连逻辑问题。先让一条最简链路完整跑通，
再往上叠加复杂度，每一层都有明确的可验收基准。

## 1. 配置层：menuconfig 里到底勾了哪些开关

### 1.1 先分清三个概念：Kconfig / menuconfig / defconfig

- **Kconfig** 是 NuttX（以及 Linux 内核）用的配置描述语言。每个模块的
  `Kconfig` 文件里用 `config XXX` 声明一个开关、它的依赖（`depends on`）、
  它的默认值。
- **menuconfig** 是 `make menuconfig` 弹出的交互式菜单，本质是个"图形化填表
  工具"，把你勾选的选项写进 `nuttx/.config`。
- **defconfig** 是 `.config` 的"精简快照"：`make savedefconfig` 会把当前
  `.config` 里**所有非默认值**整理成一小份文件（默认值就不用写了）。以后
  `tools/configure.sh -e <board>:<preset>` 就能用这份快照重建整个配置。

任务里新增的 `stm32h750b-dk:velaguard-net` 预设，就是这么来的：在
velaguard-min 的基础上进 menuconfig 勾选网络选项，`make savedefconfig`
固化成 101 行的 defconfig，再做成补丁交付。

### 1.2 配置清单：按层拆开看

#### 第一层：硬件 / MAC（这层管"网口芯片怎么通电工作"）

| 配置项 | 是什么 | 有什么用 | 为什么必须设 |
|---|---|---|---|
| `CONFIG_STM32H7_ETHMAC=y` | 编译 STM32H7 片内以太网 MAC 驱动 | 没有它，芯片的以太网外设完全不初始化 | 网口的"灵魂" |
| `CONFIG_STM32H7_MII=y` | 选择 MII 接口模式（另一个选项是 RMII） | 决定 MAC 和 PHY 之间走多少根信号线 | 板载 LAN8740A 物理上就是按全 MII 接线的 |
| `CONFIG_STM32H7_PHYADDR=1` | PHY 在 MDIO 管理总线上的地址 | 驱动通过 MDIO 读写 PHY 寄存器时，用这个地址选设备 | 由硬件布线决定（板子原理图），不能拍脑袋 |
| `CONFIG_STM32H7_PHYSR=31` 及 `PHYSR_100FD/100HD/10FD/10HD/ALTCONFIG/ALTMODE` | PHY 状态寄存器的"位布局" | 告诉驱动"速度/双工状态在寄存器的哪几个位"，自协商结果才能被读出来 | LAN8740A 的寄存器布局和常见 PHY 不一样，必须显式声明 |
| `CONFIG_ETH0_PHY_LAN8740A=y` | 声明板载 PHY 型号 | 驱动据此选择 PHY 初始化流程（同时必须**关掉** LAN8742A 选项） | PHY 型号不对，链路状态读出来全是错的 |
| `CONFIG_NET_ETH_PKTSIZE=1500` | 以太网帧最大长度 | 决定网络缓冲大小 | 标准 MTU |

这一段暴露了嵌入式网络的一个现实：**PHY 不是"插上就能用"的**。MAC 在片内，
PHY 在板子上，两者之间靠 MII 信号线和 MDIO 管理总线通信。驱动必须知道
PHY 的地址、它的状态寄存器布局、它的型号，才能在开机时完成"复位 PHY →
自协商 → 读取协商结果 → 同步 MAC 速度/双工"这一串动作。

#### 第二层：网络协议栈（这层管"数据包怎么走"）

| 配置项 | 是什么 | 有什么用 |
|---|---|---|
| `CONFIG_NET=y` | 总开关，编译 NuttX 网络子系统 | 所有 socket、协议、网卡驱动的"地基" |
| `CONFIG_NET_IPv4=y` | IPv4 协议 | 我们的网络是 IPv4 |
| `CONFIG_NET_ARP=y` | 地址解析协议 | 把 IP 地址映射成 MAC 地址，局域网通信必需 |
| `CONFIG_NET_TCP=y` | TCP 协议 | MQTT 跑在 TCP 上，没有它就没有 MQTT |
| `CONFIG_NET_UDP=y` | UDP 协议 | DHCP 是 UDP 应用，没有它就拿不到 IP |
| `CONFIG_NET_BROADCAST=y` | 广播支持 | DHCP 的前两步（DISCOVER/OFFER）就是广播 |
| `CONFIG_NET_UDP_CHECKSUMS=y` | UDP 校验和 | 数据完整性，DHCP 服务器一般要求 |
| `CONFIG_NET_ICMP_SOCKET=y` | 允许创建 ICMP socket | `ping` 命令的本质就是发 ICMP Echo |
| `CONFIG_NET_SOCKOPTS=y` | socket 选项 | `vgmqtt` 要设收发超时（`SO_RCVTIMEO`），靠它 |

注意 `NET_ICMP_SOCKET` 和 `SYSTEM_PING` 是两个东西：前者是内核的"允许发
ICMP 包"能力，后者是 NSH 里的 `ping` 命令。M1 验收要 `ping`，所以两个都开。

#### 第三层：系统服务（这层管"开机后谁去干活"）

| 配置项 | 是什么 | 有什么用 |
|---|---|---|
| `CONFIG_NETUTILS_NETINIT=y` | netinit 网络初始化库 | 开机自动把网口配好，不用人敲命令 |
| `CONFIG_NETINIT_THREAD=y` | netinit 跑在独立线程里 | **没插网线时系统照常启动**，不会卡在等 IP |
| `CONFIG_NETINIT_DHCPC=y` / `CONFIG_NETUTILS_DHCPC=y` | DHCP 客户端 | 自动向路由器申请 IP/网关/DNS |
| `CONFIG_NETINIT_DNS=y` / `CONFIG_NETDB_DNSCLIENT=y` | DNS 客户端 | `vgmqtt` 里 `getaddrinfo()` 解析域名靠它；DHCP 下发的 DNS 地址也靠它生效 |
| `CONFIG_NETINIT_CARRIER_POLL=y` | 链路状态监控（补丁新增选项） | 拔线后清掉旧 IP，重新插线自动再走 DHCP |
| `CONFIG_SYSTEM_PING=y` | NSH 的 `ping` 命令 | M1 验收工具 |
| `CONFIG_NETUTILS_MQTTC=y` | MQTT-C 客户端库 | `vgmqtt` 链接的就是它 |
| `CONFIG_NSH_ARCHINIT=y` | NSH 启动时执行板级初始化 | 把上面的 netinit 串起来的"起跑枪" |

### 1.3 两个设计取舍，比勾选项本身更重要

**取舍一：增量而不是重写。** `velaguard-net` 不是从零写的配置，而是以
velaguard-min 为底座（入口 `velaguard_app_main`、工具、串口全保留），只往
上加网络符号。而且这些网络符号不是设计者自己发明的，是从当时的
Windows 构建脚本（现已删除；日常入口是 `scripts/build.sh`）里抄的"**已经被验证过**的网络配置块"，
再按最小化裁剪。工程上这叫"站在已验证路径上扩展"：配置组合爆炸是嵌入式
开发最常见的坑，能复用一份跑通的配置，就绝不自创一套。

**取舍二：最小化而不是求全。** 明确不引入 LVGL、触摸、`urandom`；`vgmqtt`
的 JSON 消息用手写 `snprintf` 拼，不引入 cjson 库。每少一个组件，就少一个
"编译不过 / 内存不够 / 行为诡异"的潜在来源。

### 1.4 交付形态：patch + 幂等脚本（接口 1）

#### 为什么要这样做

仓库有一条铁规矩：**nuttx 公共仓零直改**。所有对 nuttx 的修改必须固化成
`scripts/` 下的 `.patch` 补丁 + 一个幂等的 apply 脚本。理由：nuttx 是上游
仓库，直接改会污染公共代码、无法回滚、别人同步时会冲突。patch + 脚本的
形式让"改动"变成了仓库里一份可审计、可逆、可重复应用的资产。

#### 接口是什么

一个 shell 脚本，用法：

~~~bash
bash scripts/apply-openvela-velaguard-net-defconfig-patch.sh [OPENVELA_ROOT]
~~~

（脚本位于仓库 `scripts/` 目录，完整路径
`../../scripts/apply-openvela-velaguard-net-defconfig-patch.sh`。）

#### 传什么参数

| 参数 | 是否必填 | 作用 | 默认值 |
|---|---|---|---|
| `OPENVELA_ROOT`（位置参数 1） | 否 | openvela 源码根目录（`nuttx/` 的父目录） | 脚本自动推断：`scripts/` 的上一级的上一级 |

脚本输出三种结果之一：

~~~text
VelaGuard net defconfig patch is already applied.      ← 重复执行，安全退出
VelaGuard net defconfig patch is already applied (with later overlapping edits).
                                                       ← 补丁打过且之后有改动，靠符号校验识别
Applied stm32h750b-dk:velaguard-net defconfig patch.   ← 首次成功应用
~~~

#### 幂等是怎么做到的：三段式检查

~~~text
第 1 段：git apply --reverse --check
   ├─ 能反向打上？→ 说明补丁已经在工作树里 → 输出 "already applied" 退出
第 2 段：defconfig_markers_present
   ├─ 文件存在且包含关键符号（ARCH_BOARD / NET=y / ETHMAC=y /
   │    INIT_ENTRYPOINT / VG_BRINGUP_TOOLS）？→ 补丁已应用且被后续编辑
   │    覆盖过 → 输出 "with later overlapping edits" 退出
第 3 段：git apply --check
   ├─ 打不上？→ 报错并提示检查重叠改动（绝不强行应用）
   └─ 能打上？→ git apply 应用，输出成功
~~~

为什么不用一句 `git apply` 完事？因为脚本的调用者是人，人会重复执行、会
在应用后继续改配置。三段检查分别处理"重复执行"、"应用后又被改过"、"与
现有改动冲突"三种真实场景。一个只跑一次没问题的脚本不是好脚本，一个
**跑一百次都安全**的脚本才是。

## 2. 初始化链路：从复位到 eth0 拿到 IP

配置只是"静态声明"，真正有意思的是运行时的初始化顺序。整条链如下：

~~~text
上电复位
  → nx_start()（NuttX 内核启动）
      → arm_netinitialize()          [内核层：注册 eth0]
          → stm32_ethinitialize(0)
              ├─ 用芯片 UID 生成 MAC 地址
              ├─ 配置 MII 全部 GPIO 引脚
              ├─ 挂以太网中断
              └─ netdev_register(eth0)
  → velaguard_app_main（应用入口，常驻）
      → 拉起 NSH 线程
          → nsh_archinit()            [NSH 层：启动网络初始化]
              → netinit_bringup()
                  → 创建 netinit 线程（异步）
                      → netlib_ifup("eth0")   [触发驱动 stm32_ifup]
                      ├─ PHY 复位 + 自协商
                      ├─ 建 DMA 描述符环、开中断
                      └─ DHCP：DISCOVER → OFFER → REQUEST → ACK
  → eth0 拿到 IP，DNS/网关就绪
      → NSH 敲 vgmqtt → 应用层 socket 上云
~~~

下面按段拆开讲。

### 2.1 第 1 段：MAC 驱动注册（内核层）

`nx_start` 启动时，因为配置了 `CONFIG_NET`，会调用 `arm_netinitialize()`，
实现在 [stm32_ethernet.c](../../../nuttx/arch/arm/src/stm32h7/stm32_ethernet.c)（约 4488 行），内部调用
`stm32_ethinitialize(0)`。它做四件事：

1. **生成 MAC 地址**：读芯片 96 位唯一 ID（UID），算 CRC64，取前 6 字节做
   MAC；再把第一个字节设为 `0x02`（本地管理位）。这样每块板子的 MAC 都
   不同，且不会和厂商分配的地址冲突——没有外挂 EEPROM 存 MAC 的板子都
   这么干。
2. **配置 MII 引脚**：`stm32_ethgpioconfig()` 把所有以太网引脚设为复用功能
   （MDIO/MDC、RXD/TXD 数据线、时钟、控制线），见
   [board.h](../../../nuttx/boards/arm/stm32h7/stm32h750b-dk/include/board.h) 的引脚表。
3. **挂中断**：`irq_attach(STM32_IRQ_ETH, ...)`，收包、发包完成都靠它。
4. **注册网卡**：`netdev_register(&dev, NET_LL_ETHERNET)`，从此系统里有
   一张叫 `eth0` 的网卡。

注意：这一步只"注册"，网口还是 down 的，PHY 也没初始化。真正的"开机"要
等 ifup。

### 2.2 第 2 段：NSH archinit → netinit 线程

固件入口是 `velaguard_app_main`，它常驻运行主循环，并托管一个 NSH shell
线程。NSH 初始化时（`CONFIG_NSH_ARCHINIT=y`）调用 `nsh_archinit()`，其中在
`CONFIG_NSH_NETINIT` 下调用 `netinit_bringup()`（[nsh_init.c](../../../apps/nshlib/nsh_init.c)）。

`netinit_bringup()` 不阻塞，它**创建一个独立线程**（`netinit`），线程里才
真正干活。这就是 `CONFIG_NETINIT_THREAD=y` 的意义：没有网线、DHCP 拿不到
地址时，系统该启动还启动，shell 该响应还响应，只有 netinit 线程在后台等。

### 2.3 第 3 段：ifup 与 PHY 自协商

netinit 线程第一步是 `netlib_ifup("eth0")`，这条 ioctl 触发驱动的
`stm32_ifup()`：

- 打开 MAC 的收发使能；
- **初始化 PHY**（`stm32_phyinit`）：通过 MDIO 复位 PHY、配置基本寄存器、
  启动**自协商**（auto-negotiation，PHY 和交换机互相商量用 100M 还是 10M、
  全双工还是半双工）；
- 建立 DMA 描述符环（收发缓冲）；
- 使能接收中断。

自协商完成后的速度/双工信息，驱动靠 1.2 节那组 `PHYSR` 配置从 PHY 状态
寄存器里读出来，再写回 MAC 的配置寄存器，两边速率对齐。

### 2.4 第 4 段：DHCP 四步

ifup 之后，`CONFIG_NETINIT_DHCPC` 让 netinit 启动 DHCP 客户端，走经典四步：

~~~text
板子 --广播--> DISCOVER（"谁家有地址？"）
路由器 <--单播-- OFFER（"我有，192.168.137.28 给你"）
板子 --广播--> REQUEST（"我要 192.168.137.28"）
路由器 <--单播-- ACK（"成交，网关 192.168.137.1，DNS ..."）
~~~

拿到 ACK 后，IP、网关、DNS 全部就位，M1 验收里 `ifconfig` 看到的
`192.168.137.28` 就是这么来的。`CONFIG_NETUTILS_DHCPC_BOOTP_FLAGS=0x8000`
表示 DHCP 回复用广播发（某些网络环境单播到不了）。

### 2.5 第 5 段：链路监控（PHY 轮询 + carrier）

这一段是两个补丁的核心，也是整个任务里最"硬件"的部分。

**为什么不能靠中断，而要轮询？** 板子走 QSPI-XIP 启动，QSPI 的 bank2 引脚
和 MII 的 CRS/COL 信号（PH2/PH3）**物理共用**。CRS/COL 本来是"链路状态变化
通知"的信号线，被占用了，驱动只能换一种方式感知链路：定期去问 PHY。

补丁 `openvela-eth-mii-stm32h750b-dk.patch` 在驱动里加了一套 `BOARD_ETH_PHY_POLL`
机制：

- 低优先级工作队列（LPWORK）里每 500ms 干一次活；
- 通过 MDIO 读 PHY 的 `MII_MSR` 状态寄存器；
- **要连续读两遍**：MII_MSR 的链路位是 latch-low（低电平锁存），第一次读
  到的是历史状态，第二次才是当前状态；
- **连续 2 次采样一致**才算数（防抖），避免插拔瞬间误判；
- 链路 up：重新初始化 PHY、同步 MAC 速度/双工，然后
  `netdev_carrier_on()`；链路 down：`netdev_carrier_off()`。

配套补丁 `openvela-netinit-carrier-poll.patch` 让 netinit 监听 carrier 状态：
**拔线 → 清掉旧 IP；重新插线 → 自动再走一遍 DHCP**。这让"拔线 → 云端收到
LWT"的演示变得干净：断线是被系统感知并清理的，而不是默默挂着一个死 IP。

### 2.6 接口速查：这条链上的几个"接口"

| 接口 | 位置 | 是什么 | 传什么参数 |
|---|---|---|---|
| `netdev_register(dev, lltype)` | `netdev/`，内核层 | 把驱动注册成系统网卡 | 网卡结构体 `dev`；链路类型 `NET_LL_ETHERNET` |
| `netdev_carrier_on/off(dev)` | 同上 | 向协议栈广播"物理链路通/断" | 网卡结构体 `dev` |
| `netinit_bringup()` | `apps/netutils/netinit/` | 启动网络初始化（线程） | 无参数；行为由 Kconfig 决定 |
| `stm32_ethinitialize(intf)` | `arch/arm/src/stm32h7/stm32_ethernet.c` | 初始化一个以太网接口 | 接口号 `intf`（单网卡传 0） |

## 3. 应用层：vgmqtt 一个命令把消息送上云

### 3.1 接口：NSH 命令行工具

`vgmqtt` 是一个 NSH 命令，用法：

~~~text
vgmqtt -h <broker> [-p <port>] [-u <user>] [-P <pass>]
       [-t <topic>] [-m <json>] [-q <0|1|2>] [-r] [-w <secs>]
~~~

完整参数表：

| 参数 | 含义 | 默认值 | 说明 |
|---|---|---|---|
| `-h <host>` | Broker 地址（IP 或域名） | **必填** | 内部走 `getaddrinfo`，两种都支持 |
| `-p <port>` | Broker 端口 | `1883` | 明文 MQTT 标准端口 |
| `-u <user>` / `-P <pass>` | 用户名/密码 | 匿名 | 云测试端口匿名即可 |
| `-t <topic>` | 发布主题 | `vg/{DEVID}/status` | `DEVID` 编译期定 |
| `-m <json>` | 消息内容 | 自动生成合同 status JSON | 不传就按合同 §4.1 手拼 |
| `-q <0\|1\|2>` | QoS 等级 | `0` | 默认 0 是**合同规定**；1/2 仅调试用 |
| `-r` | 保留消息 | 默认开启 | retained：新订阅者也能立刻看到最后一条 |
| `-w <secs>` | 发布后保持连接秒数 | `0` | LWT 演示窗口：等待期间拔线 |

还有一个"隐藏参数"：`DEVID` 设备号是**编译期**宏，默认 `vg-test-01`，构建时
可用 `-DDEVID="xxx"` 覆盖。为什么是编译期而不是运行时？因为嵌入式设备没有
配置文件系统，设备身份属于"出厂固化"信息，编译期定最省事（对应项目手册
§16.1 的 test 模式）。

### 3.2 关键实现逐个拆

#### (1) 建 TCP 连接：`getaddrinfo` + `socket` + `connect`

~~~c
ret = getaddrinfo(host, port, &hints, &res);
~~~

`getaddrinfo` 是这个工具的第一个"接口"：把"人类可读的地址 + 端口"翻译成
内核能用的 `sockaddr` 列表。`hints` 里声明 `AF_INET`（只要 IPv4）、
`SOCK_STREAM`（只要 TCP）。它会自动处理两种输入：`107.174.123.74`（IP
字面量）和域名（这时就需要 `CONFIG_NETDB_DNSCLIENT` 的 DNS 客户端去查）。

拿到结果后遍历 `res` 链表逐个 `socket()` + `connect()`，第一个成功就停。

#### (2) socket 超时：为什么必须设

~~~c
tv.tv_sec  = 2;
setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
~~~

这是三轮回调里最贵的一条教训。MQTT-C 库在 NuttX 上的 `mqtt_pal_recvall` 是
**阻塞式 recv**：如果 Broker 一直不回 CONNACK/PUBACK，`mqtt_sync()` 会无限
卡在 recv 里。设了 2 秒超时后，任何一次网络"静默"最多等 2 秒就会返回，
程序才能按自己的逻辑判断超时、报错退出。没有这行，工具会表现为"卡死"。

#### (3) `mqtt_connect`：连接 + 遗嘱一起交出去

~~~c
mqtt_connect(&client, DEVID, cfg.topic, VGMQTT_WILL_MSG,
             strlen(VGMQTT_WILL_MSG), cfg.user, cfg.pass,
             connflags, 15);
~~~

8 个参数逐一解释：

| 参数 | 传什么 | 含义 |
|---|---|---|
| `client` | `&client` | MQTT 客户端对象（内存状态） |
| `client_id` | `DEVID` | 设备在 Broker 上的唯一身份 |
| `will_topic` | `cfg.topic`（同 status 主题） | **遗嘱主题**：异常掉线时 Broker 往这里发消息 |
| `will_message` | `{"online":false}` | 遗嘱内容：告诉订阅者"我下线了" |
| `will_message_length` | `strlen(...)` | 遗嘱长度 |
| `username` / `password` | `cfg.user` / `cfg.pass` | 鉴权（匿名时传 NULL） |
| `connect_flags` | `connflags` | 连接选项位 |
| `keep_alive` | `15` | 心跳秒数：Broker 约 1.5 倍周期内没收到心跳就判掉线 |

`connflags` 的组合是 LWT 的关键：

~~~c
connflags = MQTT_CONNECT_CLEAN_SESSION |   // 全新会话，不恢复历史
            MQTT_CONNECT_WILL_QOS_0 |       // 遗嘱用 QoS0（与 status 一致）
            MQTT_CONNECT_WILL_RETAIN;       // 遗嘱也保留
~~~

LWT（Last Will and Testament，遗嘱）是 MQTT 最优雅的机制之一：**设备自己
提前把"遗言"存在 Broker 上**，正常断开（发 DISCONNECT）不触发；一旦异常
断开（拔线、断电、网线断），Broker 在 keepalive 超时后替设备把遗言发出去。
这正是"云端感知设备掉线"的实现方式，也是本任务要验证的核心机制。

#### (4) 等 CONNACK：连接"真成功"的信号

~~~c
while (!client.event_connect && client.error == MQTT_OK &&
       now_ms() - start_ms < 10000)
  {
    mqtt_sync(&client);
    usleep(50000);
  }
~~~

`mqtt_connect()` 返回 OK 只代表**CONNECT 报文排队发出去了**，不代表 Broker
接受了连接。真正的"连接成功"是收到 CONNACK 后 `client.event_connect` 被置
位。这个循环等它最多 10 秒。超时计时用 `CLOCK_MONOTONIC`（单调时钟），
而不是 `time()`——系统时间可能被 NTP 校正，单调时钟不会跳变。

#### (5) `mqtt_publish`：发布 + 确认

~~~c
pubflags = (uint8_t)(cfg.qos << 1);        // QoS 数字 → MQTT 协议位
if (cfg.retain)  pubflags |= MQTT_PUBLISH_RETAIN;
mqtt_publish(&client, cfg.topic, cfg.msg, strlen(cfg.msg), pubflags);
~~~

参数：`client`、主题、消息、长度、发布标志。发布标志的编码是个经典坑：
MQTT 报文头里 QoS 占 2 个 bit，编码值是 `qos << 1`。任务调试时就发现过
"打印 QoS=2，实际是 QoS1"的显示 bug——因为程序存的是数字 1，按协议编码后
变成 2，又把编码值直接打印出来了。

确认逻辑用的是 `mqtt_mq_find`：

~~~c
mqtt_mq_find(&client.mq, MQTT_CONTROL_PUBLISH, NULL) != NULL
~~~

这个接口的语义：在客户端的发送队列里找"还没完成的 PUBLISH 消息"。**找得到
= 还没确认；找不到 = 已经完成**。为什么不能用 `mqtt_mq_length`（队列长度）？
因为 MQTT-C 的消息发完（收到 PUBACK）后**不会自动出队**，`mqtt_mq_clean`
只在发送缓冲不足时被调用——队列长度在消息完成后依然是 1，用它判断必然
误报。这是阅读第三方库源码才能发现的细节，也是"确认语义"这个主题最典型
的例子。

#### (6) `-w` 保持窗口：LWT 演示的"舞台"

发布成功后，`-w 30` 让程序保持连接 30 秒不退出，并打印提示。此时拔网线：
TCP 连接断开，Broker 在约 15×1.5=22.5 秒后发布遗嘱。正常退出则先发
DISCONNECT 再关 socket，**不会**触发 LWT——优雅离线和异常离线必须区分。

### 3.3 三轮回调：一次"假确认"引发的血案

这个任务最值钱的部分是 M2 的三轮现场调试，每轮都改进了"确认语义"：

| 轮次 | 现象 | 根因 | 修复 |
|---|---|---|---|
| 第 1 轮 | 打印 connected 后 31.5 秒才打印 published，云侧没收到 | `recv` 阻塞；`mqtt_connect` 返回 OK≠CONNACK；打印的 published 是假确认 | 设 2s socket 超时；等 `event_connect`；等 `mqtt_mq_find` 确认 |
| 第 2 轮 | PUBACK 没回来，确认循环空转 33 秒报错；QoS 显示为 2 | QoS 编码值被当显示值；超时被阻塞式 sync 稀释 | 修正 QoS 显示；改用单调时钟真实计时 |
| 第 3 轮 | QoS1 的 PUBACK 回程在代理环境下不稳定，但消息实际已到云 | 合同规定 status 用 **QoS0**，默认 QoS1 属设计偏离 | 默认改 QoS0 + retained；QoS1 留作调试参数 |

第 3 轮尤其值得记住：**不是技术做不到，而是设计偏离了合同**。回到文档核对
约定，比在错误的路上继续优化更重要。任务把"QoS1 回程不稳"记为了阶段 3 的
风险——因为 alarm/ai/request 那些 QoS1 主题以后要用，必须回头复查。

## 4. 构建基建：build_minimal.sh 从单目标到多目标

### 4.1 为什么改：一次"烧旧固件"事故

任务进行到一半时，现场发现：VS Code 的 Build & Flash 任务硬编码了
velaguard-min，而校验函数还显式拒绝 `CONFIG_NET`。于是点一下 Build & Flash
会发生：脚本 distclean 回 minimal → 构建目标不一致失败 → 但 `.debug/` 里
还躺着**上一次构建的旧 hex** → Flash 任务不管三七二十一把它烧进板子。
板子跑的还是旧固件，人却以为烧了新的——这是嵌入式开发最危险的一类事故：
**失败被"静默"了**。

### 4.2 接口：参数表

~~~text
bash scripts/build_minimal.sh [TARGET] [--clean]
~~~

| 参数 | 取值 | 含义 | 默认 |
|---|---|---|---|
| `TARGET`（位置 1） | `net` / `min` / `lvgl` | 构建哪个预设 | `net` |
| `MODE`（位置 2） | `--clean` | distclean 后全量重建 | 增量构建 |

兼容性处理：老用法 `bash build_minimal.sh --clean` 会被识别为
`TARGET=net MODE=--clean`，老脚本的肌肉记忆不失效。

### 4.3 防呆设计

- 按目标校验 `.config` 形态（`expect_dev_config` 里逐符号 grep：入口、
  LVGL、NET、MQTTC），不匹配就自动 distclean 重配；
- **构建开始前清空 `.debug/` 旧产物**——构建失败时 Flash 任务因为找不到
  `nuttx.hex` 直接报错，而不是烧旧固件。把"失败"从静默变成响亮；
- `.vscode/tasks.json` 加 `vg_target` 选择器，Build/Rebuild 弹窗选目标，
  Flash 固定烧 `.debug/nuttx.hex`。

这部分的工程哲学一句话总结：**防呆设计要防的是"静默的错"**。出错不可怕，
可怕的是出了错系统还假装成功。

## 5. 五条可以带走的经验

1. **站在已验证路径上扩展**：新配置以旧配置为底座，符号集抄已验证块，
   不发明新组合。
2. **最小闭环先打通**：M1 网络、M2 MQTT，每步都有可验收基准，失败范围
   立刻减半。
3. **每层都要有"确认语义"**：驱动层有 carrier 状态机，netinit 有 DHCP 重连，
   `vgmqtt` 有 CONNACK/PUBACK 确认。"我以为成功了"和"确实成功了"之间的
   差距，是嵌入式调试里 80% 的 bug 来源。
4. **公共仓零直改，patch + 幂等脚本交付**：改动可审计、可回滚、可重复，
   这是团队协作的底线。
5. **防呆防"静默失败"**：清空旧产物让失败显形，比事后排查"为什么烧了旧
   固件"便宜一百倍。

## 6. 继续学习的阅读清单

按顺序读，正好覆盖"配置 → 驱动 → 协议 → 工具"：

- 任务三件套（PRD / 设计 / 实现记录）：
  [08-14-stage1-net-mqtt-min-loop/](../../.trellis/tasks/archive/2026-08/08-14-stage1-net-mqtt-min-loop/)
- [velaguard_mqtt.c](../../app/velaguard/velaguard_mqtt.c)：`vgmqtt` 全文（396 行，推荐精读）
- [netinit.c](../../../apps/netutils/netinit/netinit.c)：netinit 线程、ifup、DHCP、carrier 监控
- [stm32_ethernet.c](../../../nuttx/arch/arm/src/stm32h7/stm32_ethernet.c)：MAC 驱动、PHY 初始化、
  链路轮询（重点看 `stm32_ifup` / `stm32_phyinit` / `stm32_link_work`）
- [board.h](../../../nuttx/boards/arm/stm32h7/stm32h750b-dk/include/board.h)：MII 引脚表、
  `BOARD_ETH_PHY_POLL` 的定义
- [openvela-eth-mii-stm32h750b-dk.patch](../../scripts/openvela-eth-mii-stm32h750b-dk.patch) 与
  [openvela-netinit-carrier-poll.patch](../../scripts/openvela-netinit-carrier-poll.patch)：两个补丁全文
- [velaguard-bringup-known-issues.md](../velaguard-bringup-known-issues.md) §5：实测验收记录与踩坑
- [velaguard-mqtt-contract.md](../velaguard-mqtt-contract.md)：status/LWT 主题合同（QoS0 的依据）

再往下，这个项目的阶段 2/3 是 Modbus 采集和 AI 诊断，重连状态机、TLS、
QoS1 回程稳定性都在路上——这些正是从本文每个"遗留风险"里长出来的下一课。
