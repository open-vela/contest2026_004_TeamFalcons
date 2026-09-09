# VelaGuard 扩展板 Bring-up 已知问题与挂账修复

> 状态：2026-09-09 更新。记录 ESP-01S/RS485 bring-up 过程中发现、已固化/未固化的修复项，
> 以及 `velaguard-min` 最小固件预设的使用说明。
> 仓库规则：nuttx / apps / MQTT-C **在对应 git 树上直接修改**（VelaGuard feature 分支 + PR）。
> **不要**再新增或 apply `scripts/openvela-*.patch`。下文若仍提到历史 patch 文件名，只作溯源，以树上源码为准。

## 1. TIM15 CH2 输出引脚编译守卫笔误（NuttX 上游驱动 bug）

| 项 | 内容 |
|---|---|
| 状态 | 已固化为 patch + 幂等 apply 脚本 |
| 文件 | `nuttx/arch/arm/src/stm32h7/stm32_pwm.c`（约 1366 行） |
| 原代码 | `#ifdef CONFIG_STM32H7_TIM12_CH2OUT`（笔误，应为 TIM15） |
| 现象 | TIM15 通道 2 的引脚配置块（`PWM_TIM15_CH2CFG` = PE6/AF4）被错误的宏守卫整体裁掉，`channels[].out1.in_use=0`，驱动不配置 PE6 → 定时器照跑但引脚无方波 → 蜂鸣器不响（命令打印正常、示波器无波形） |
| 已应用修复 | 守卫改为 `#ifdef CONFIG_STM32H7_TIM15_CH2OUT`（直接改 nuttx 树） |
| 验证 | D6(PE6) 见 2.7kHz 方波；`examples/pwm` 与 `vgpwm` 均正常发声 |
| 固化 | `nuttx/arch/arm/src/stm32h7/stm32_pwm.c` TIM15 channel 2 块；不要再 apply patch |

## 2. velaguard-min 最小固件预设

### 2.1 新增预设与切换命令

新增 `stm32h750b-dk:velaguard-min`（减法生成，基座为上游 `lvgl` 预设），
只保留 console/NSH、板载 LED、RS485、ESP-01S、DO(PWM) 与 `null`/`zero`，
去掉 LVGL/LTDC/FB/触摸/urandom/网络栈。系统入口为 Guard 应用
（`velaguard_app_main`），它常驻主循环并托管 NSH 线程，见 2.5。预设切换：

```bash
cd $OPENVELA_ROOT/nuttx
tools/configure.sh -e stm32h750b-dk:velaguard-min   # 最小 bring-up
tools/configure.sh -e stm32h750b-dk:lvgl            # 切回 UI 形态（后续任务）
```

nuttx 侧交付物（在 nuttx 树，不走 patch）：

| 文件 | 作用 |
|---|---|
| `nuttx/boards/.../stm32h750b-dk/configs/velaguard-min/defconfig` | min 预设 |
| 扩展板 pinmux / bringup / TIM15 PWM | `board.h` / `stm32_bringup.c` / `stm32_pwm.c` |
| TIM15 守卫笔误修复 | `arch/arm/src/stm32h7/stm32_pwm.c`（见第 1 节） |

### 2.2 日常入口与 Rebuild 复位

日常按钮（`openvela: Build` / `Rebuild`）固定 `velaguard-net`，不弹预设。
需要最小固件时用命令行 `bash scripts/build.sh min`（**不是** 父目录
openvela 的 `./build.sh`）。

`bash scripts/build.sh min --clean` 会 `configure.sh -E` 复位到
`stm32h750b-dk:velaguard-min`：未写回 defconfig 的本地 `.config` 改动会被
丢掉，这是有意行为。

### 2.3 排除项理由

| 排除项 | 理由 |
|---|---|
| LVGL/LVX demo/LV perf/sysmon | 最小 bring-up 不跑 UI；LVX demo 符号保留给未来 UI 应用 |
| LTDC/VIDEO_FB/EXAMPLES_FB | 无显示需求；`/dev/fb0` 消失 |
| INPUT/FT5X06/EXAMPLES_TOUCHSCREEN | 无触摸；`/dev/input0` 消失；I2C4 一并关闭（扩展板文档标为禁用） |
| DEV_URANDOM | 无随机源需求；`/dev/urandom` 消失 |
| NET/NETINIT/STM32H7_ETHMAC/SYSTEM_PING/SYSTEM_DHCPC_RENEW | 无网络需求（以太网按决策排除）；`NETUTILS_CJSON` 顺带关闭 |
| EXAMPLES_PWM / EXAMPLES_SERIALBLASTER / EXAMPLES_SERIALRX | bring-up 期测试示例，不属于最小基线；PWM/RS485 验证由 `vgpwm`/`vgrs485` 承担 |

`/dev` 契约（硬件验证后应恰为）：

```text
console null pwm0 rs485 ttyS0 ttyS1 ttyS2 zero
```

### 2.4 `VG_BRINGUP_TOOLS` 说明

`app/velaguard/Kconfig` 新增 bool `VG_BRINGUP_TOOLS`：

```kconfig
config VG_BRINGUP_TOOLS
	bool "VelaGuard bring-up tools (vgpwm/vgrs485/vgesp/velaguard_app)"
	default LVX_USE_DEMO_CONTEST2026_004_VELAGUARD_APP
	select ARCH_HAVE_LEDS
```

- 默认跟随 LVX demo 符号：`lvgl` 预设（LVX demo=y）下工具默认编译。
- 最小预设显式 `VG_BRINGUP_TOOLS=y`，与 LVGL 完全解绑。
- `app/velaguard/Makefile` 的 `MODULE`、`Make.defs` 的 `CONFIGURED_APPS`、
  `CMakeLists.txt` 的构建条件全部改挂 `CONFIG_VG_BRINGUP_TOOLS`。
- `velaguard_app` 注册为 NSH 命令（入口符号经 `-Dmain=` 重命名而来），
  shell 里再敲该命令时防重护栏直接返回，见 2.5。

### 2.5 入口与 NSH 托管（单一形态）

`velaguard_app_main` 是系统入口（`CONFIG_INIT_ENTRYPOINT`），主线程常驻
运行主循环（LED 演示，后续挂 LVGL 等业务），同时拉一个 NSH 线程
（`nsh_initialize()` + `nsh_consolemain()`）提供 shell，供调试 vgpwm/
vgrs485/vgesp 等小工具。两个任务并行，互不阻塞：

```text
velaguard_app_main（入口，常驻）
 ├─ NSH 线程：nsh_initialize + nsh_consolemain（shell，可跑 vg* 工具）
 └─ 主循环：LED / 后续 LVGL 等业务
```

- 板级初始化由 NSH 线程内的 NSH_ARCHINIT 触发一次（BOARDIOC_INIT），
  入口线程不重复初始化 → **不需要在 board 层做幂等标志**。
- 入口符号 `velaguard_app_main` 由 `PROGNAME` 经 `-Dmain=` 重命名而来，
  声明由 `include/sys/types.h` 依 `CONFIG_INIT_ENTRYPOINT` 宏生成，
  不依赖 builtin 注册；`velaguard_app` 同时作为 NSH 命令存在，若在
  shell 里重复启动，`g_app_running` 防重护栏直接退出。
- **NSH 前台命令由 NSH 任务 `waitpid` 阻塞等待**（`apps/nshlib/nsh_builtin.c`，
  `CONFIG_SCHED_WAITPID=y`）：前台命令若进入死循环，shell 会卡死，后续
  输入只堆在串口接收缓冲区，等命令退出后才被读出——与优先级/时间片无关
  （`usleep` 照常让出 CPU）。因此常驻业务绝不能写成 NSH 前台死循环，应
  走系统入口常驻（本形态）或独立后台任务（`&`/线程）；NSH 只跑返回型小工具。
- 早期的双形态切换开关 `VG_APP_HOSTS_NSH` 已删除：产品/开发统一为
  app 入口 + 托管 NSH，不需要再切换。
- 注意字面值 `CONFIG_INIT_ENTRYPOINT="nsh"` 会链接失败
  （`undefined reference to 'nsh'`）；用 `"nsh_main"` 可回到纯 NSH 入口
  （不跑 Guard 主循环的调试姿态）。

## 3. 全量构建缺 `CONFIG_ARCH_LEDS` 显式关闭

| 项 | 内容 |
|---|---|
| 状态 | 已固化进 `velaguard-min` / `velaguard-net` defconfig；日常 Rebuild 走预设，不再用 kconfig-tweak |
| 现象 | 全量构建报 `undefined reference to 'board_userled_initialize'` / `board_userled`（链接错误） |
| 根因 | velaguard app 的 Kconfig `select ARCH_HAVE_LEDS`，而 `CONFIG_ARCH_LEDS` 默认 y → 板级编译 `stm32_autoleds.c`（只有 `board_autoled_*`），不再编译 `stm32_userleds.c`（`board_userled_*` 在这里）→ velaguard.c 链接不到符号 |
| 已应用修复 | 对应 defconfig 补丁含 `# CONFIG_ARCH_LEDS is not set` |
| 待办 | 若 `scripts/build.sh` Rebuild 后再出现，检查 defconfig 补丁是否被覆盖 |

## 4. RS485 发送尾部被切断（tcdrain 不等 TC + close 释放 DIR）

| 项 | 内容 |
|---|---|
| 状态 | **驱动已修（2026-08-29）**：RS485 关 FIFO；`txempty` 等 **TXE\|TC** 后切 DIR=RX；TC ISR 不再切 DIR；`vgrs485` 去掉 `usleep(50ms)` |
| 板测教训 | LA 证 DI/DIR 正确时，A/B 乱码多为 **USB-RS485 收发器/共地**；换适配器后误码大降 |
| 现象（旧） | 对端偶发缺尾/中段花码；`close` 过早拆 DIR 会切尾 |
| 根因（软件） | `.txempty` 绑 TXE（FIFO 下为 TXFNF），`tcdrain` 早返回 |
| 验收 | 对端 Hex 连续 `61…7A`（26 字节）；偶发单字节误码可归适配器 |

## 已完成验证记录（2026-08-13）

- ESP-01S：USART2 115200 8N1；`vgesp at` → `OK`；`vgesp cmd AT+GMR` →
  `AT version:1.7.4.0` / `SDK version:3.0.5-dev`（AT 固件默认开回显，
  响应为 10 字节 `AT\r\n\r\nOK\r\n`）
- RS485 回归：`/dev/rs485` → `/dev/ttyS2`；`vgrs485 tx` 26 字节 ×3 干净；
  `vgrs485 rx` PASS/FAIL 内容校验正常
- 串口编号（NuttX 规则：console 固定 `ttyS0`，其余按外设顺序编号）：
  `ttyS0`=USART3（console）、`ttyS1`=USART2（ESP）、`ttyS2`=UART7（RS485）
- 测试工具：`vgpwm`（蜂鸣器）、`vgrs485`（RS485 收发+校验）、`vgesp`（ESP AT）

## 5. velaguard-net 网络预设 + vgmqtt（阶段 1，2026-08-14）

### 5.1 预设说明与切换

`stm32h750b-dk:velaguard-net` = velaguard-min 底座 + RJ45 网络栈：
`NET`/`STM32H7_ETHMAC`/LAN8740A(MII)/DHCP/`NETINIT_THREAD`/`NETINIT_CARRIER_POLL`/
DNS/`SYSTEM_PING`/`NETUTILS_MQTTC`；不含 LVGL。入口保持 `velaguard_app_main`。

切换命令（需全量重编）：

```bash
cd $OPENVELA_ROOT/nuttx
tools/configure.sh -e stm32h750b-dk:velaguard-net   # 阶段 1：最小 + 网络 + MQTT
tools/configure.sh -e stm32h750b-dk:velaguard-min   # 最小 bring-up（无网络）
tools/configure.sh -e stm32h750b-dk:lvgl            # 上游 UI 形态（无网络）
```

nuttx 侧交付物（在树上，不走 patch）：

| 文件 | 作用 |
|---|---|
| `nuttx/boards/.../stm32h750b-dk/configs/velaguard-net/defconfig` | velaguard-net 预设 |
| `BOARD_ETH_PHY_POLL`（`board.h` + ethernet 驱动） | MII/PHY 链路轮询 |
| USART2 / RS485 DIR / ESP GPIO | `board.h` / `stm32_bringup.c` |

### 5.2 构建脚本多目标

`scripts/build.sh [net|min|lvgl] [--clean]`（默认 net）：

- 按目标校验 `.config` 形态，不匹配自动 distclean；
- `--clean`（任务 `openvela: Rebuild`）会复位到所选预设，未保存的
  `.config` 改动会被丢掉；
- 构建前清空 `.debug` 旧主镜像，失败时 Download 直接报缺文件，杜绝烧旧固件；
- `.vscode/tasks.json` 的 Build/Rebuild 固定 velaguard-net，不弹选择器；Download 固定烧 `.debug/nuttx.hex`。

### 5.3 vgmqtt 用法与验收

```bash
# M2 现场（Broker=107.174.123.74:1883 匿名明文；DEVID 默认 vg-test-01）
vgmqtt -h 107.174.123.74 -p 1883 -w 30
```

- 默认 QoS0 + retained（`docs/velaguard-mqtt-contract.md` §3：status 用 QoS0；
  alarm/ai/request 等 QoS1 由 `-q` 覆盖，供阶段 3 用）；
- `-w <secs>` 发布后保持连接（LWT 演示窗口）；正常退出发 DISCONNECT，拔线才触发 LWT；
- 确认语义：`connected`=CONNACK 收到，`published`=PUBLISH 完成
  （`mqtt_mq_find` 查状态：QoS0 发出即完成、QoS1 等 PUBACK）。

验收方法：

- M1：`ifconfig eth0` 拿 DHCP IP → `ping <网关>` 0% 丢包 → `ping 1.1.1.1`（或 8.8.8.8）通；
- M2：板端 `vgmqtt -w 30` + 云侧订阅 `vg/+/status`，先见 retained `online:true`，
  保持窗口内拔线，约 20-30s 后见 LWT `online:false`（retained 覆盖）。

### 5.4 已知坑（2026-08-14 实测）

1. `make olddefconfig`/`savedefconfig` 的 kconfig-conf 对 apps/tests Kconfig 的跨文件
   menu/endmenu 报错；PATH 必须含 `prebuilts/tools/python/bin` 让 make 走 kconfiglib。
2. Windows 网桥必须同时包含 WiFi 与 USB 以太网口；只含其一 = 无上游孤桥，板子拿不到 IP。
   （实测切 ICS/移动热点后 DHCP 正常，`192.168.137.x`。）
3. QoS1 PUBACK 回程在 ICS/代理（如 Mihomo TUN）下可能延迟/丢失：CONNACK 正常但 PUBACK 10s 不回。
   status 按合同走 QoS0 不受影响；阶段 3 的 alarm/ai/request（QoS1）需在云链路稳定后复查。
4. MQTT-C 的消息发完（COMPLETE）后不会自动出队（`mqtt_mq_clean` 只在发送缓冲不足时调用），
   `mqtt_mq_length` 不能作为"已确认"信号；确认请用 `mqtt_mq_find(control_type)` 查状态。

### 5.5 阶段 1 验收记录（2026-08-14）

- M1（AC3）：DHCP `192.168.137.28`（ICS）；ping 网关 10/10 0% 丢包 RTT20ms；ping 1.1.1.1 连通。
- M2（AC4）：板端 status（QoS0 retained）→ 云侧收到 `online:true`；保持窗口内拔线 →
  约 23s 后云侧收到 LWT `online:false`（观察端 [151.3s]/[174.0s] 两事件，间隔 22.7s≈keepalive15s×1.5）。
- 构建（AC1/AC5）：容器内全量 make 通过（Flash≈199KB / SRAM≈51KB）；defconfig patch 幂等验证通过。

## 6. RJ45 / ESP-01S TCP 故障转移（2026-08-18）

故障转移，不是灾备。切的是 **TCP 业务**（第一用户：MQTT-C），不是 NSH `ping`。

| 项 | 行为 |
|---|---|
| 判定 | 只 ping `eth0`（DHCP 网关；空则 `CONFIG_VG_NET_PING_HOST`）。连续 3 次失败才判 RJ45 挂 |
| 切什么 | POSIX TCP ↔ ESP-01S `lesp_*` TCP。同时只一条活动出口 |
| Wi-Fi | 上电即 join，与 RJ45 是否健康无关（热备）。测试 AP：SSID `xxx` / PSK `1472583690` |
| 模组 | 仍是 ESP-01S，USART2 `/dev/ttyS1`，AT 1.7.4。不换 ESP32 |
| 自动运行 | `velaguard_app_main` 拉 `net_mgr` 线程。不需要敲 NSH |
| `vgnet` | 只观察/注入：`status`、`inject rj45\|wifi down\|up\|auto`、`wifi <ssid> <psk>` |
| NSH `ping` | 永远走内核 `eth0`，不会改走 ESP。ESP AT 没有 ICMP |
| MQTT | CONNACK 才算该出口在线。status QoS0 retained，`network=rj45\|esp01\|none` |
| `vgmqtt` | 仍是一次性 POSIX 调试命令，不参与故障转移 |
| `vgesp` | `net_mgr` 占用 UART 时拒绝（busy） |
| 关联判定 | 本树 `lesp_ap_is_connected()` 只有头文件声明、没有实现；采样用 `lesp_get_net()` 的 STA IP |
| ESP_AT_Lib | 不用（已归档 Arduino C++，与 `lesp_*` 同构） |
| 不做 | 不把 ESP 做成 NuttX netdev / PPP / SLIP |

nuttx / apps / MQTT-C 侧交付物（在对应 git 树上，不走 patch）：

| 位置 | 作用 |
|---|---|
| nuttx `velaguard-net` defconfig | `NETUTILS_ESP8266` ttyS1 115200 + `VG_NET_FAILOVER` |
| MQTT-C `mqtt_pal.c` | pal 弱符号 hook：tagged `lesp` fd 走 `lesp_send/recv` |
| apps `esp8266.c` | `LESP_*` → `lespSSID_SIZE` / `lesp_eMODE_*` 映射 |

主机单测：`make -C app/velaguard/host_tests test`。物理拔线不是完成门禁。

## 7. eMMC / SDMMC1（2026-08-29）

| 项 | 内容 |
|---|---|
| 状态 | Stage0 bring-up **通过**（`08-29-stage0-emmc`） |
| 预设 | `bash scripts/build.sh emmc` → `velaguard-emmc` |
| 挂载 | `/mnt/emmc`（vfat）；首次需 `mkfatfs /dev/mmcsd0` |
| LFN | `CONFIG_FAT_LFN=y`（长名如 `vg_emmc_probe.txt`） |
| 总线 | 当前 NuttX H7 路径为 **1-bit MMC**；硬件 8-bit 已接，宽总线待驱动增强 |
| IDMA | 关闭（`MM_REGIONS=6`） |
| 板资 | BOUNDARY V11 本地官方包；细节见 `.trellis/tasks/08-29-stage0-emmc/research/emmc-bringup-notes.md` |
| 不做 | 不自动格式化；`/data` 正式布局留给阶段 1 / powerfail-store |
