# T09 OTA 的 RJ45 物理链路检查调查

调查日期：2026-10-29。选手仓基线 `4d4aa93`，NuttX 基线 `6edcdecfcec`；工作区有其他任务的未提交修改，本文不改代码、驱动、引脚或既有遥测路由，不占 COM3，也未在真机读取 PHY。本报告为实现依据，不能作为链路检查已通过的板测证据。

## 结论

OTA 应独立读取 `eth0` 对应 PHY 的 BMSR，用 `SIOCGMIIPHY` 获取实际驱动配置的 PHY 地址，再对 `MII_MSR` 连续读取两次，以第二次的 `MII_MSR_LINKSTATUS` 判断当前物理链路。任何 socket/ioctl 失败、无 PHY 支持、地址不合法或无效寄存器值均拒绝 OTA 联网；不得回退到 `IFF_RUNNING`、旧网络状态、缓存 ping 或 ESP8266。链路通过后仍须执行 IP 检查、OTA socket 的 `SO_BINDTODEVICE=eth0` 和正常的严格 TLS 检查。

现有 NuttX 已有只读 PHY ioctl，无需为了获得基本链路状态新增 PHY 驱动或改变引脚。当前构建缺少 `CONFIG_NETDEV_PHY_IOCTL=y`，必须先打开此选项并构建验证。还必须处理 MDIO 与现有 PHY 轮询的并发：当前 ioctl 的设备锁和轮询的网络锁不是同一把锁，不能直接宣称任意线程的 ioctl 已和轮询串行化。完整调用链确认后，最小方案是在 NuttX 驱动 `stm32_ioctl()` 的 `SIOCGMIIREG` / `SIOCSMIIREG` 案例中使用 `net_trylock()`，成功时才做寄存器事务并解锁，失败立即返回；应用只用普通 ioctl。不得直接增加阻塞的 `net_lock()`，也不应从 OTA 应用获取内核网络锁。具体理由和调用上下文见末节，本次没有实现修复。

## 本地官方电路证据

资料：`F:/Project/Embeded/H750B-DK/BOARD INFO/H750B-DK/mb1381-h750xb-b01-schematic.pdf`，MB1381 B01，H750XB 变体，2018-06-11。已实际渲染并检查第 5 页，页码与图框 Sheet 一致。

- 第 3 页 STM32 microcontroller IOs 标注 U6 为 `STM32H750XBH6`，Ethernet 总线包含 `MII_MDC`、`MII_MDIO`。
- 第 5 页 Ethernet：U9 明确为 `LAN8740Ai-EN-TR`，采用完整 MII；MDIO 和 MDC 管理线路分别连到 PHY 的 16、17 脚。图中直接注明 `PHY Address: 01h`，不需要从相似板卡推测。
- 第 5 页 PHY 收发差分对进入 PoE 层级；第 6 页 PoE 显示这些线路经 T3 到 CN1 `KRJ-CB4.2GYZNL` RJ45。
- 同目录 `bsp/` 有 34 个文件/目录条目，包含 `stm32h750b_discovery*` 的音频、LCD、MMC 等模块，没有 ETH 或 LAN8740 专用实现，不能把未包含的 HAL/PHY 初始化视作本地证据。此问题由原理图和实际 NuttX 源码即可确定，无需猜测接线或修改 PHY 地址。

原理图显示默认硬件地址为 1；应用实现仍应从 `SIOCGMIIPHY` 取得地址，不硬编码 1，不扫描 MDIO 地址，不写 PHY。若未来固件或板卡地址与上述基线不同，应重新核对而不是自动探测或修改。

## 为什么 IFF_RUNNING 不能作为 OTA 的唯一依据

`app/velaguard/vg_eth.c:98-120` 在 `vg_eth_sample()` 中通过 `SIOCGIFFLAGS` 得到 `IFF_RUNNING` 后直接填入 `out->link`。没有读 PHY，也没有在本函数中验证读取时间；`:169-176` 还允许复用上次 ping 结果。现有函数服务普通网络健康/路由策略，本次应保持原行为。

NuttX `include/net/if.h:51-52` 将 `IFF_UP` 定义为接口启用、`IFF_RUNNING` 定义为 carrier available；`net/netdev/netdev_carrier.c:61-66` 和 `:82-87` 实际只设置/清除 `dev->d_flags` 的位。它是驱动维护的状态，不是调用 ioctl 当下的一次 MDIO 采样。

实际 STM32H7 驱动 `arch/arm/src/stm32h7/stm32_ethernet.c:2468-2493`：存在板级轮询时在启动阶段采样，否则 `:2492` 无条件 carrier on。当前 `boards/arm/stm32h7/stm32h750b-dk/include/board.h:332-335` 在 `CONFIG_STM32H750B_DK_QSPI_BOOT` 下定义 `BOARD_ETH_PHY_POLL`，而当前 `.config:670` 的 QSPI_BOOT 已启用，因此不能用无轮询分支直接断言本次运行板卡的误判原因。

当前轮询 `stm32_ethernet.c:3684-3693` 在 PHY 读取失败时仅记录错误并重排任务，保留原 carrier；`:3702-3733` 有多次采样去抖和状态缓存。故即使轮询开启，`IFF_RUNNING` 仍不等于实时读取成功。用户明确未接 RJ45，应先保留这个事实，将此次 RUNNING 标为待复核，不能据此声称网线已接入或 OTA 网络已验收。

## ioctl 实际路径与配置

| 证据位置 | 已核对行为 |
|---|---|
| `nuttx/net/netdev/Kconfig:20-25` | `NETDEV_PHY_IOCTL` 默认关闭，选中后自动选择 `NETDEV_IOCTL`。 |
| `nuttx/.config:1471` | 当前为 `# CONFIG_NETDEV_PHY_IOCTL is not set`；选手仓 `scripts/configs/velaguard-lvgl.defconfig` 尚无此配置。 |
| `scripts/configs/velaguard-lvgl.defconfig:58,184-192` | 当前为 LAN8740A、MII、PHYADDR=1，速度/双工状态寄存器为 31；BMSR 链路检查应使用标准寄存器 1，不误用速度/双工寄存器。 |
| `nuttx/arch/arm/src/stm32h7/stm32_ethernet.c:4413-4415` | 仅启用 `CONFIG_NETDEV_PHY_IOCTL` 时注册 `dev.d_ioctl=stm32_ioctl`。 |
| `nuttx/net/netdev/netdev_ioctl.c:897-913,923,1238-1251,1364` | 根据 `ifreq.ifr_name` 找设备，持 `netdev_lock(dev)` 后，将内嵌的 `mii_ioctl_data_s` 转交驱动；缺少驱动回调返回 `-ENOSYS`。外部传参必须是完整 `struct ifreq *`。 |
| `nuttx/include/net/if.h:171-176,340-343` | 字段为 `ifr_mii_phy_id`、`ifr_mii_reg_num`、`ifr_mii_val_out`，不能套用 Linux 的指针覆盖写法。 |
| `nuttx/arch/arm/src/stm32h7/stm32_ethernet.c:3025-3038` | `SIOCGMIIPHY` 返回 `CONFIG_STM32H7_PHYADDR`，`SIOCGMIIREG` 调用 `stm32_phyread(phy_id,reg_num,&val_out)`。前者只是取得配置地址，不证明 PHY 在线。 |
| 同文件 `:2996-2999,3056-3061` | 没启用 PHY ioctl 则不编译此函数；`CONFIG_STM32H7_NO_PHY` 时返回 `-EIO`。 |
| `nuttx/include/nuttx/net/mii.h:48,271` | `MII_MSR=0x01`，`MII_MSR_LINKSTATUS=(1<<2)`。 |
| `nuttx/arch/arm/src/stm32h7/stm32_ethernet.c:3601-3628` | 已有板级 `stm32_phylinkread()` 明确说明 link status latch-low，双读 BMSR 并使用第二次结果。可借鉴协议步骤，无需暴露其 static 函数。 |

只读 ioctl 不依赖 `CONFIG_ARCH_PHY_INTERRUPT`，不需要开启 PHY 中断或修改 LAN8740 引脚；`SIOCMIINOTIFY` 才有该中断配置分支（`stm32_ethernet.c:3007-3023`）。`CONFIG_STM32H7_PHY_POLLING` 是另一条网络监控选项，不是获取本次只读 ioctl 的必要条件，不能为本任务无关开启或替换现有路由策略。

## OTA 最小实现步骤

1. 编译时要求 PHY ioctl、网络设备和 eth0 的真实 PHY 支持；缺少这些条件时明确返回不可验证链路。配置改动放选手仓 `scripts/configs/velaguard-lvgl.defconfig`，由正常构建脚本复制到板级配置。
2. 创建临时 `socket(AF_INET, SOCK_DGRAM, 0)`。清零 `struct ifreq`，把接口名固定为 `eth0`。可先通过 `SIOCGIFFLAGS` 要求 `IFF_UP`，但不能把 RUNNING 当最终结论。
3. `ioctl(fd,SIOCGMIIPHY,(unsigned long)&ifr)`，检查成功及 `ifr_mii_phy_id<=31`。不要自行替换 ioctl 返回的地址。
4. 保留该地址，将 `ifr_mii_reg_num=MII_MSR`，执行两次 `SIOCGMIIREG`；每次必须成功，任何失败立即返回不可验证并关闭 fd。第一读用于清除 latch-low，第二读给出当前链路。建议对每次返回的 `0xffff` 或 `0x0000` 作无效 PHY 应答拒绝；只针对当前 LAN8740A 的保守 OTA 闸门，不把此规则泛化为所有 PHY 的标准定义。
5. 只有第二读包含 `MII_MSR_LINKSTATUS` 才允许进入后续 OTA 网络步骤。第二读无 link 则返回 `eth_link_down`；读失败或值异常则返回 `eth_link_unverified`。这两个结果都不得落入 Wi-Fi、旧快照或应用注入状态的放行分支。
6. 无论成功失败都关闭查询 socket。TLS/MQTTS/HTTPS socket 仍在 connect 之前强制绑定 eth0；PHY 检查本身不产生外部网络流量，也不证明 DNS、IP、网关、TLS 服务可用。
7. 在新建 OTA 连接前重读，并在长下载的接收循环/服务轮询中按有限频率重读。检测断链/无法读取立即终止本轮网络会话；不要把一次读到 link up 当作整个下载阶段的永久授权。最终是否仍能通信由实际收发和超时共同约束。

## 串行化和时间限制

`stm32_phyread()`（`stm32_ethernet.c:3110-3148`）直接写 MACMDIOAR 后忙轮询，没有独立 MDIO 锁；循环上限为 `PHY_READ_TIMEOUT=0x0004ffff`（`:291`），返回 `-ETIMEDOUT`，属于有限迭代数，并非毫秒级截止时间保证。不能把 OTA socket 的 `SO_RCVTIMEO` 说成限制这次同步 ioctl。

ioctl 路径持设备递归锁 `d_lock`（`nuttx/include/nuttx/net/net.h:100-104`、`netdev_ioctl.c:923`），现有 `stm32_link_work()` 却持 `net_lock()`（`stm32_ethernet.c:3677-3684,3748`）。这两个锁不是同一个对象（全局网络锁定义见 `nuttx/net/utils/net_lock.c:56-57`）。因此必须明确序列化安排。完整调用链表明不能直接在 ioctl 案例内阻塞获取网络锁；末节给出驱动内 `net_trylock()` 的最小修复位置。仅增加一个 OTA 私有 mutex 只能约束 OTA 自己的线程，无法排除 LPWORK 的 MDIO 访问。

不在 LVGL 线程、Modbus 采集线程或安全环回调内执行这些同步读取；在 OTA worker 中执行。若产品要求严格毫秒上限或受限锁等待，则还需公共树把 MDIO 和锁等待改成单一有限截止时间；本次不对现有循环实际持续时间作未测量承诺。

## 待验收

- 未接 RJ45：本机原始双读 BMSR 的返回值和最终 OTA 拒绝原因；即使 IFF_RUNNING 保持 true 仍须拒绝。
- 正常接 RJ45：PHY 地址来源、双读成功、link up、eth0 IP/网关、SO_BINDTODEVICE，以及实际 MQTTS/HTTPS 出口。
- 拔插、启动时未接网线、链路在查询后立即消失，均有明确拒绝/中止；不影响原遥测与 ESP 策略。
- MDIO 超时、无 ioctl 支持、错误接口、异常返回值以及与既有 PHY 轮询并发，均拒绝，且无资源泄漏、长期阻塞或状态沿用。
- 本文仅阅读和写报告，未执行上述真机验收。

## 补充：最小 MDIO 同步修复位置和锁顺序核对

推荐修改归属为实际 `../nuttx` checkout 的 VelaGuard 分支，仅限 `arch/arm/src/stm32h7/stm32_ethernet.c` 的 `stm32_ioctl()`（实际函数名不是 `stm32_ethioctl`）：

- `SIOCGMIIPHY` 只返回配置常量，不需要访问 MDIO，也无需获取网络锁。
- 在 `SIOCGMIIREG` 的实际 `stm32_phyread()` 前调用 `net_trylock()`，返回负值则直接结束该案例，不访问 MAC 寄存器；成功后执行读并 `net_unlock()`。
- 对 `SIOCSMIIREG` 的 `stm32_phywrite()` 同样处理，保证整次内部 read-modify-write 也处于同一保护区。OTA 本身始终只发读命令，修复原有写案例是为了统一这个 ioctl 入口的并发约束，不构成 OTA 可写 PHY 的新功能。
- 所有成功获取锁后的错误路径都要释放锁。保留底层 `-ETIMEDOUT` 等返回值；trylock 忙返回 `-EAGAIN`。OTA 可立即拒绝本轮链路检查，或在自己的 worker 中做有总次数/总时限的退避；不能在仍持设备锁的驱动案例中循环等待。

可采用以下局部结构，本文只展示方案，没有写入驱动：

```c
ret = net_trylock();
if (ret < 0)
  {
    break;
  }

ret = stm32_phyread(req->phy_id, req->reg_num, &req->val_out);
net_unlock();
break;
```

写案例使用同样结构包围 `stm32_phywrite()`。应用继续用完整 `struct ifreq` 调用普通 `ioctl()`；不在应用调用 `net_lock()`、`net_trylock()` 或操作 `dev->d_lock`。

### 完整入口链与反转证据

1. `nuttx/fs/socket/socket.c:145-148` 的 `sock_file_ioctl()` 转 `psock_ioctl()`；`nuttx/net/netdev/netdev_ioctl.c:2050-2066` 转 `psock_vioctl()`，本层没有网络锁。
2. `netdev_ioctl.c:1949-1955` 先处理 socket 专用 ioctl，未识别时进入标准 `netdev_ifr_ioctl()`；`:897-913` 根据 `ifr_name` 找设备，`:923` 获取 `netdev_lock(dev)`。
3. `:1238-1246` 在这个设备锁内调用 `dev->d_ioctl`，`:1364` 才解锁。因此在 STM32 PHY ioctl 案例处已经持有 `d_lock`。
4. `stm32_ethernet.c:2569-2585` 的 `stm32_txavail_work()` 先取 `net_lock()`，再执行 `stm32_dopoll()`；`:1431-1433` 调到 `devif_poll()`。`nuttx/net/devif/devif_poll.c:1129-1142` 明确假设网络已锁，并获取 `netdev_lock(dev)`。超时 worker `stm32_ethernet.c:2359-2372` 同样在 `net_lock()` 内执行 ifdown/ifup 和 `stm32_dopoll()`。
5. 所以阻塞式修复会形成 ioctl 的 `d_lock -> net_lock` 与发送 worker 的 `net_lock -> d_lock`。这是真正存在的相反等待顺序；即使当前单核，也可由线程抢占形成互相等待，不能因 CPU 数为 1 排除。
6. 网络锁确实可递归：`net/utils/net_lock.c:56-57` 是 `rmutex_t`，`:150-152` 使用 `nxrmutex_lock()`，`:172-174` 使用 `nxrmutex_trylock()`。递归只保证同一线程重入成功，不能解除上述两个线程的反向持锁。
7. `include/nuttx/mutex.h:527-545,553-568` 明确 trylock 在其它线程持锁时不阻塞并返回 `-EAGAIN`，同线程已持锁则增加递归计数。故局部 trylock 不增加 `d_lock -> 阻塞 net_lock` 等待边：若发送 worker 已占网络锁等待设备锁，ioctl 立即退出并释放设备锁，让 worker 继续。它也保持已持网络锁的内核调用者重入行为。

### 其它 PHY 访问上下文

| 调用者 | 实际路径和保护情况 |
|---|---|
| `stm32_link_work()` | `stm32_ethernet.c:3677-3684` 在 `net_lock()` 内双读；`:3717-3718` 重新初始化和再次读 link，直到 `:3748` 才解锁。局部 trylock 会与它互斥。 |
| `stm32_txtimeout_work()` | `:2365-2367` 持网络锁重置/启动，进 `stm32_ifup -> stm32_ethconfig -> stm32_phyinit`；局部 trylock 会与这段 MAC/PHY 重置互斥。 |
| 外部 `SIOCSIFFLAGS` 的 ifup/ifdown | `netdev_ioctl.c:923,1057-1074` 持设备锁，经 `netdev_ifup:2091` 转驱动；驱动 `stm32_ifup:2437-2439` 已启动时直接返回，新启动则 `:2456` 进配置。与同设备 PHY ioctl 由现有 `d_lock` 串行。无需为了本次方案在这些入口追加反向网络锁。 |
| `stm32_phyinit()` | `stm32_ethernet.c:3372-3379` 配置 MACMDIOAR 时钟并写 reset；`:3392,3426,3439,3466,3478,3500,3575` 读写均经上面配置/轮询调用路径。不存在独立无上下文的应用入口。 |
| `stm32_phyregdump()` / `stm32_dm9161()` | 定义 `:3317-3336` / `:3251-3301`，实际仅在 `stm32_phyinit()` 的 `:3407` / `:3413` 中调用，继承其上下文；DM9161 分支在当前 LAN8740A 构建未启用。 |
| PHY 中断通知 | `stm32_phyintenable:3083-3088` 当前只是返回 `-ENOSYS` 的未实现分支，不能声称存在其它中断 MDIO 读写或依赖该入口做同步。 |

两个 BMSR ioctl 之间允许现有 PHY poll 执行；第二次成功读到的 link 位仍是第二次采样时刻的状态。此同步方案保护每个 MAC MDIO 硬件事务，不声称两次 ioctl 是不可分割的完整快照，更不声称链路在检查后不会变化。若中间发生重置/异常/忙而使第二次读失败，OTA 必须拒绝。

以上结论限当前源码和构建。应用无需内核锁，遥测、路由和 pinmux 不需要改动；公共树局部同步修复须编译及与既有 PHY poll/发包 worker 并发板测后才可认定可靠，本文未执行这些操作。
