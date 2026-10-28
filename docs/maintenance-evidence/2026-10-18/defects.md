# 新固件板端缺陷记录（2026-10-18）

本文件记录本轮在板上发现并跟进的四个缺陷，以及一项凭据问题。D-01、D-02、D-04 都已定位根因、修复并板端复验；D-03 是凭据管理问题，只做记录，待人工决定处理方式。

固件版本：首次复现于 `75cbc8eb7a5`（构建 2026-10-18 14:37:55）。D-01 修复后为同提交的
16:09:56 构建，回退栈试验后重新构建为 16:58:34，D-02 修复后为 17:43:24，
D-04 修复后为 `43702275f28` 的 18:41:50 构建。

---

## D-01：候选点表写入恒失败（已修复并复验）

优先级 P0，影响阶段 D 整条点表确认流程。

### 现象

```text
nsh> vgpoint add -i acltest -a 9 -r 0
vgpoint: ERR cmd=add code=io msg=write_fail
nsh> vgpoint set temp -w 29
vgpoint: ERR cmd=set code=io msg=write_fail
nsh> vgpoint del door
vgpoint: ERR cmd=del code=io msg=write_fail
```

修复前 5 次尝试全部失败。已确认表始终为 `committed n=14`，未被破坏。

### 根因

`store_file()`（`app/velaguard/vg_file_store.c`）在替换文件时，会从调用方路径派生三个名字：

| 名字 | 后缀 | `point_table_candidate.json` 的实际长度 |
|---|---|---|
| 新内容临时文件 | `.tmp` | 30 |
| 备份 | `.bak` | 30 |
| 备份临时文件 | `.bak.tmp` | **34** |

板端 vfat 卷以 `CONFIG_FAT_MAXFNAME=32` 构建，文件名单段超过该值即被拒绝。备份临时文件那一步 `open()` 返回 `ENAMETOOLONG`，`store_file` 随即放弃整次替换并返回错误，`vgpoint` 把它显示成 `write_fail`。

板端实测边界：

| 文件名单段长度 | 结果 |
|---|---|
| 29 / 30 / 31 / 32 | 创建成功 |
| 33 | `nsh: echo: open failed: 36`（36 = ENAMETOOLONG） |
| `point_table_candidate.json.bak`（30） | 成功 |
| `point_table_candidate.json.bak.tmp`（34） | `open failed: 36` |

这解释了全部观测：先删掉候选文件再 `add` 时，命令仍报 `write_fail`，但候选文件被 `vg_point_table_ensure_candidate` 用创建路径重建为 14 点（该路径目标不存在，不需要备份步骤）；随后的 `add` 替换已存在的候选，触发备份步骤而失败。

也解释了为什么主机测试和 `vgstoretest` 都没发现：

- 主机文件系统允许 255 字符名字，旧后缀在主机上不会失败，所以主机用例覆盖不到。
- `vgstoretest` 写 `store-probe.bin`（备份临时名 19 字符）、采集快照写 `values.txt.tmp`（14）、Agent 写 `alarm_advice.txt.tmp`（20），都远在预算内。

### 修复

1. `vg_file_store.h` 增加 `VG_STORE_NAME_MAX`（默认 32，可覆盖），并说明它与板端 `CONFIG_FAT_MAXFNAME` 的关系。
2. `vg_file_store.c` 把备份临时后缀从 `.bak.tmp` 缩短为 `.btmp`，使产品路径派生出的最长名字为 31。
3. `store_file` 在动文件系统之前先用 `store_name_fits()` 校验三个派生名，放不下的路径直接返回 `-ENAMETOOLONG`，不再做半次替换。
4. `test_file_store.c` 增加回归：产品候选名（26 字符）能完成替换并保留 `.bak`；32 字符的名单段被拒绝，且不留下任何文件。

对外协议不变：`docs/velaguard-host-nsh-protocol.md` 第 315 行把 `write_fail` 定义为这几处的固定 `msg`，本次没有改动它。

### 板端复验（2026-10-18 16:09:56 构建）

```text
nsh> uname -a
NuttX  0.0.0 75cbc8eb7a5 Oct 18 2026 16:09:56 arm stm32h750b-dk
nsh> vgpoint add -i acltest -a 9 -r 0
vgpoint: OK cmd=add table=candidate n=15
nsh> vgpoint del acltest
vgpoint: OK cmd=del table=candidate n=14
nsh> vgpoint list
vgpoint: OK cmd=list table=committed n=14
```

同时确认候选编辑不触碰已确认表：`add` 之后 `list` 仍是 `committed n=14`，`list -c` 才是 `n=15`；落盘后 `point_table_candidate.json` 为 2280 字节，旧内容保留在 `point_table_candidate.json.bak`（2167 字节）。

### 原始日志

- `nsh-candidate-add.log`：修复前首次失败 + 两张表对照。
- `nsh-candidate-add-fresh.log`：先删候选再 `add`，候选被重建而命令仍报错。
- `nsh-lfntest.log`：`open failed: 36` 与 30 字符对照。
- `nsh-lfnbound2.log`：29/30/31/32 成功、33 失败的边界。
- `nsh-fix-verify.log` / `nsh-fix-verify2.log` / `nsh-fix-verify3.log`：修复后 add / 隔离 / del。

---

## D-02：`discover` 目录的元数据改写导致整机崩溃（**根因已定位并修复**）

优先级 P0。表现是整机失联，必须 SWD 复位。

### 现象

在 `/data/velaguard/discover/` 内执行任何会改写该目录条目的操作都会崩溃。最小复现：

```text
nsh> echo AAA > /data/velaguard/discover/r1.txt
nsh> echo BBB > /data/velaguard/discover/r2.txt
nsh> mv /data/velaguard/discover/r1.txt /data/velaguard/discover/r2.txt
HARDFAULT: CFSR=00008200 HFSR=40000000 BFAR=0000003b MMFAR=0000003b
HARDFAULT: stacked PC=90096060 LR=9007c02d
dump_assert_info: Assertion failed panic: at file: :0 task: velaguard_app_main process: velaguard_app_main 0x9000caad
```

把目录本身改名（`mv /data/velaguard/discover /data/velaguard/discover_bad`）触发同样的崩溃。

崩溃后整机失联：COM3 无回显，`192.168.137.172` ping 无响应，只能用 SWD 复位恢复。本次共复现 4 次，逐次复位记录见 `reset-log.txt`。

### 故障定位

用 `arm-none-eabi-addr2line` 和 `nm` 解析 `.debug/nuttx.elf`：

- 故障指令 `0x90096060` 落在 `getpid()` 内，是 `ldr r0, [r0, #48]`。
- `0x9007c02d` 落在 `nsh_command`，正是 `ret = handler(...); vtbl->np.np_lastpid = getpid();` 那一行（`apps/nshlib/nsh_command.c:1321`）。
- `getpid()` 第一步读的全局地址是 `0x24000264`，符号为 `g_readytorun`。
- `BFAR=0x3b` 对应 `r0 = 0x0b` 时执行 `[r0 + 0x30]`，即指针链中一个本应是地址的位置上放着 `0x0b`。

结论：**`cmd_mv` 返回之后，调度器相关内存已被破坏**，`getpid()` 顺着 `g_readytorun` 解引用到坏指针而精确数据访问出错（`CFSR` 的 `BFARVALID|PRECISERR`），升级成 HardFault。

`rename` 的数据操作本身是完成的：崩溃前 `cat r2.txt` 能得到源文件内容 `AAA`；目录改名也实际生效（复位后看到的是 `discover_bad/`）。崩溃发生在操作之后的收尾路径。

### 已排除的范围

| 试验 | 结果 |
|---|---|
| `mv` 覆盖已存在文件，在 `/data/velaguard/diagnostics/` | 不崩溃，`cat` 得到 `AAA` |
| 同上，在新建空目录 `/data/velaguard/scratchmvd/` | 不崩溃 |
| 同上，在仿造目录（放同名 `point_table_candidate.json`、`discover_state.bin` 及 `r1/r2.txt`） | 不崩溃 |
| 同上，在新建目录里放入**逐字节相同**的候选与状态文件副本 | 不崩溃 |
| 同上，在 `discover/` 内，且目录只剩原本那两个文件 | **仍崩溃** |

所以既不是 `cmd_mv` 或 `rename` 的通用缺陷，也不是文件名、文件大小或文件内容的问题，而是**这个目录自身的磁盘状态**。

### 根因：NSH 控制台线程栈耗尽，溢出打坏栈底的 `tls_info_s`

用 OpenOCD（`ST-LINK_gdbserver` 在这台机器上始终报 `Target no device found`）复位下连上核心后，
在 `arm_hardfault` 入口下硬件断点、在 `tls_info_s->tl_task` 那个字下硬件监视点，
抓到了写入者：

```text
Old value = 939525504 (0x38000580)   New value = 11 (0x0b)
0x90001684 in arm_svcall ()          sp = 0x38002be0
bt: arm_svcall <- arm_doirq <- exception_common <- nxsched_switch
    <- nxsem_wait_slow <- stm32_eventwait <- mmcsd_eventwait <- mmcsd_read
    <- fat_hwread <- fat_fscacheread <- fat_findsfnentry <- fat_putlfname
    <- fat_dirnamewrite <- fat_rename <- rename <- cmd_mv <- nsh_command
```

`sp` 正好落在栈分配的最低地址，也就是 `tls_info_s` 所在处。NuttX 把 `tls_info_s`
放在栈分配底部（`arm_stackframe.c` 的 `up_stack_frame`），所以栈一溢出就先打坏
`tl_task`；随后 `getpid()` 解引用 `0x0b`，产生 `BFAR=0x3b` 的精确数据访问错误，
升级成 HardFault。

板端实测：这条 `mv` 路径需要 **2048 字节**栈，而修复前可用 **2024 字节**
（pthread 默认 2048 减去 24 字节 `tls_info_s`），差 24 字节。对照实验：同样的 `mv`
在 `diagnostics/` 里执行时监视点不触发，命令正常完成，所以溢出深度与具体路径相关。

完整定位过程、反汇编、栈布局和量化数据见 [d02-root-cause.md](d02-root-cause.md)。

### 修复与复验

| 文件 | 改动 |
|---|---|
| `app/velaguard/Kconfig` | 新增 `CONFIG_VG_NSH_STACKSIZE`，默认 8192 |
| `app/velaguard/velaguard.c` | 控制台线程改用 `pthread_attr_setstacksize`，加下限 `#error` |
| `scripts/configs/velaguard-lvgl.defconfig` | 增加 `CONFIG_VG_NSH_STACKSIZE=8192` |

板端确认 `ps` 里 PID 4 的 STACK 从 `0002024` 变为 `0008168`。修复后同一串命令通过：

```text
nsh> mv /data/velaguard/discover_bad/r1.txt /data/velaguard/discover_bad/r2.txt
nsh> cat /data/velaguard/discover_bad/r2.txt
AAA
nsh> uname -a
NuttX  0.0.0 75cbc8eb7a5-dirty Oct 18 2026 17:43:24 arm stm32h750b-dk
```

填充法量出的高水位是 2048 字节 / 可用 8168 字节，余量 6120 字节；
`tls_info_s->tl_task` 在整条命令前后都是 `0x38000580`，未被破坏。

受损目录保留在 `/data/velaguard/discover_bad/` 作为现场证据，未删除。
它现在只是一组普通文件，不再具有"触发崩溃"的特殊性。

### 与上一轮否定结论的关系

上一轮把线程栈提到 8192 后记录为"签名不变，假设不成立"，本轮按同样思路重新实现后
板端复验通过。上一轮的否定结果本轮无法重现。可能原因是那次增量构建没有真正把栈改上去
（本仓库存在复用旧 `nuttx/.config` 的陷阱，本轮构建前就因此触发过一次 `#error`），
但本轮没有留下那次构建的 `.config` 或 `ps` 输出可以核对，不把它当作已证实的结论。

### 仍未证实

- 与 ST-02 记录的历史 eMMC 目录损坏是否同源。时间线上吻合，但本次没有证据把两者
  认定为同一原因，不据此关闭 ST-02。
- 其他线程的栈没有跟着调整。PID 7（ESP8266）、PID 8（netinit）仍是 2024 字节，
  本轮没有证据说明它们会走到同样深的 vfat/SDMMC 路径。

### 原始日志

- `nsh-rename-hardfault.log`：首次完整故障转储。
- `nsh-mv-repro2.log`、`nsh-mv-discover3.log`、`nsh-mv-discover4.log`：三次复现。
- `nsh-mv-freshdir.log`、`nsh-mv-mimic.log`、`nsh-mv-copyfiles.log`：三个不崩溃的对照。
- `nsh-rebuild-dir.log`、`nsh-newdiscover.log`：目录改名崩溃与重建后复验。
- `gdb-catch-hardfault.log`：GDB 在 `arm_hardfault` 入口抓到的完整故障现场与调用栈。
- `gdb-watch-tl_task.log`：监视点抓到写入 `tl_task` 的指令，`sp` 落在栈底。
- `gdb-watch-diagnostics.log`：对照组，`diagnostics/` 目录同操作下监视点未触发。
- `gdb-measure-stack.log`：填充法量出的栈高水位 2048 / 可用 8168 字节。
- `d02-root-cause.md`：根因定位的完整推导与量化数据。
- `build-d02-fix.log`、`flash-d02-fix.log`：修复后的构建与烧录。
- `reset-log.txt`：复位的时间与原因。

---

## D-03：MQTT 口令明文存在于仓库与固件镜像

优先级 P1。属于凭据管理问题，不是运行缺陷。

`scripts/configs/velaguard-lvgl.defconfig` 受版本控制，其中含一个字面量键 `CONFIG_VG_MQTT_PASSWORD`，值为明文口令。同一份 defconfig 会被复制成 NuttX 板级配置并参与编译，因此该字符串也会进入固件镜像。

这与 AGENTS.md 的 V4（密钥经 `vgprovision` 加密存于 eMMC，不得写进固件镜像）以及"完整 API key、MQTT token、产品密钥不得出现在 UI、日志、串口输出或 MQTT payload"的要求冲突。

本轮只做记录：没有修改该值，没有在证据文件中复制口令正文，也没有把镜像与 defconfig 做逐字节比对（该比对会读取口令值，已被权限策略拒绝，未尝试绕过）。

`docs/backend-api.md` 里 `MQTT_PASSWORD` 被描述为"可选密码"，没有说明板端 defconfig 为何需要内置一个具体口令。需要人工决定处理方式：改走 `vgprovision` 加密下发，或至少把该值移出仓库改为构建期注入。

---

## 附：一次瞬时 MQTT 报错

在 2026-10-18 的一次 `vgmqtt` 无参调用（一次性连到 `8.148.67.174:1883` 发布 status）之后，串口出现：

```text
vgnet: mqtt error MQTT_ERROR_SOCKET_ERROR
ERROR: publish failed: MQTT_ERROR_SOCKET_ERROR
```

随后重新执行 `vgnet` 显示 `mqtt=up`，会话自行恢复。未能确认是这次额外连接造成的干扰还是一次网络抖动。记为观察，不作为缺陷。同一时段还观察到板端 IP 由 `192.168.137.172` 变为 `192.168.137.123`（DHCP 续租），不影响结论。

---

## D-04：从站回复过快时板子丢帧（**根因已定位并修复**）

优先级 P1。这条正是 BUS-03 关闭条件里点名的"剩余驱动窗口"。2026-10-18 傍晚换新收发器
后第一次用快速从站实测到，当天完成定位与修复。

### 环境

用户更换了 USB-RS485 收发器。新设备实际枚举为 **COM7**（`USB-SERIAL CH340`，
`VID_1A86&PID_7523`），不是 COM6。COM3 仍是 ST-LINK 虚拟串口（NSH 控制台）。
从站模拟器 `scripts/maintenance_rtu_slave.ps1` 用 `-ComPort COM7` 启动。

链路本身是通的：从站在 20 s 内收到 69 个请求并全部应答，
`crc_scan=0`、`suppressed=0`，请求帧格式正确（例如 `06-03-00-00-00-01-85-BD`）。

### 现象（修复前）

同一份固件（`75cbc8eb7a5`，构建 2026-10-18 17:43:24），只改从站的回复延迟：

| 从站回复延迟 | 总线请求 | 成功 | 超时 | other | CRC 错 | 在线点位 |
|---|---|---|---|---|---|---|
| 0 ms（第 1 次） | 274 | 38 | 223 | 13 | 0 | 4 / 14 |
| 0 ms（第 2 次） | 131 | 88 | 29 | 14 | 0 | 12 / 14 |
| 5 ms | 1183 | 1183 | 0 | 0 | 0 | 14 / 14 |
| 20 ms | 805 | 795 | 10 | 0 | 0 | 14 / 14 |
| 150 ms | 265 | 265 | 0 | 0 | 0 | 14 / 14 |

`boot_crc=0`、`boot_echo=0` 说明板子不是收到坏帧，而是整帧没收到。

### 根因：DIR 释放挂在 tcdrain 的轮询上，而轮询被 10 ms 系统 tick 拉长

方向控制在这一版 NuttX 里是改过的（`../nuttx/arch/src/stm32h7/stm32_serial.c`）：

- `up_send()` / `up_txint(enable)` 把 DIR 置为发送；
- **接收方向的切换只在 `up_txempty()` 里做**，`up_txempty()` 由 `tcdrain()` 调用；
- `up_txempty()` 等到 TXE\|TC 之后再 `up_udelay(400)`，才把 DIR 切回接收；
- TX 完成中断里**故意不切** DIR（注释写明 "Do NOT drop DIR to RX here"）。

而 `tcdrain()` 的等待循环是（`../nuttx/drivers/serial/serial.c`）：

```c
      while (!uart_txempty(dev))
        {
          nxsig_usleep(POLL_DELAY_USEC);   /* POLL_DELAY_USEC = 1000 us */
          ...
        }
```

`nxsig_usleep(1000)` 会向上取整到系统 tick，而本板 `CONFIG_USEC_PER_TICK=10000`，
tick 是 10 ms。

板端实测（用 DWT 周期计数器，CPUCLK = 400 MHz；断点设在 `up_txempty()` 的
RS485 分支里，所以不会混进控制台 UART3）：

```
POLL     203936122  isr_mask=0xc0  since_last=5228225 cyc = 13071 us
DIRDROP  203936157  since_last_poll=35 cyc = 0 us
POLL     459950887  isr_mask=0xc0  since_last=5259113 cyc = 13148 us
POLL     695849786  isr_mask=0xc0  since_last=5108647 cyc = 12772 us
```

一次轮询间隔 **12.7-13.8 ms**，不是 `POLL_DELAY_USEC` 写的 1 ms。
也就是说 TC 置位后 DIR 最多要等约 13 ms 才释放。

同时量了 `up_udelay(400)` 本身：三次采样分别 353.9 / 420.2 / 357.1 µs，
`CONFIG_BOARD_LOOPSPERMSEC=43103` 的标定是准的，写 GPIO 只要 6-15 µs。
**问题不在那 400 µs 的保持，在轮询粒度。**

这解释了全部观测：

- 0 ms 从站的回复（约 1-2 ms 到达）必然落在约 13 ms 的窗口里，整帧被丢掉；
- Windows 的 `Thread.Sleep(5)` 实际约 5-16 ms，所以 5 ms 那组刚好躲开；
- 20 ms 那 10 次超时来自从站进程自身抖动；
- 150 ms 远离窗口，全通。

### 修复

把 DIR 释放放回 TX 完成中断，与上游 NuttX 的 STM32 驱动一致，也等同 STM32 硬件
自己的 DE 输出在 DEDT = 0 时的行为：

```c
  if ((priv->sr & USART_ISR_TC) != 0 &&
      (priv->ie & USART_CR1_TCIE) != 0 &&
      (priv->ie & USART_CR1_TXEIE) == 0)
    {
      if (priv->rs485_dir_gpio != 0)
        {
          stm32_gpiowrite(priv->rs485_dir_gpio, !priv->rs485_dir_polarity);
        }

      up_restoreusartint(priv, priv->ie & ~USART_CR1_TCIE);
    }
```

`up_txempty()` 改回纯查询语义：TXE\|TC 都置位才算空闲，并保留一次 DIR 写入作为兜底
（覆盖从未使能 TCIE 的发送路径），但不再在那里做 400 µs 的忙等。原来那句
`up_udelay(400)` 一并删掉，理由是它的保护作用靠不住：它只把总线悬空推迟 400 µs，
并不能消除悬空；真正需要的是总线偏置，而不是在接收路径上压一个固定的延迟。

### 板端复验（`43702275f28`，构建 2026-10-18 18:41:50）

| 从站回复延迟 | 总线请求 | 成功 | 超时 | CRC | echo | other | 在线点位 | 从站 crc_scan |
|---|---|---|---|---|---|---|---|---|
| 0 ms | 649 | 649 | 0 | 0 | 0 | 0 | 14 / 14 | 0 |
| 5 ms | 525 | 525 | 0 | 0 | 0 | 0 | 14 / 14 | 0 |
| 20 ms | 471 | 471 | 0 | 0 | 0 | 0 | 14 / 14 | 1（早期一次性） |
| 150 ms | 454+ | 454+ | 0 | 0 | 0 | 0 | 14 / 14 | 2（早期一次性） |

0 ms 从站由修复前的 14% / 67% 变为 **649/649 全通**。四组 `boot_timeout`、
`boot_crc`、`boot_echo`、`boot_other` 全部为 0。

D-02 的栈修复在同一版固件上复测仍有效：`mv` 在 `discover_bad/` 里通过、`cat` 得到
`AAA`、`ps` 里 PID 4 栈仍是 `0008168`、`vgpoint list` 仍为 `committed n=14`。

### 未关闭的部分

- 从站侧的 `crc_scan` 在 20 ms 与 150 ms 两组各出现 1 次和 2 次，都发生在开始后
  20-90 秒内，之后 600 多个请求不再增加；0 ms 与 5 ms 两组全程为 0。
  目前按**启动瞬态**处理，没有把它当成稳定现象，也没有证据说它与本次改动相关。
  要确认需要在更长时间的长测里继续观察，本轮不据此下结论。
- 0 ms 与 5 ms 两组全程 `crc_scan=0`，说明旧的"尾部杂字节"症状没有回来。
  但这一结论只对当前这支收发器成立，没有覆盖历史上出问题的那支适配器。
- 修复删掉了 400 µs 的保持。如果以后换回会在总线悬空时误读的适配器，
  这个症状可能重现；届时应从总线偏置入手，而不是在接收路径上加延迟。

### 复现步骤

```powershell
# 从站：COM7，9600 8N1，回复延迟 0 ms
powershell -File scripts/maintenance_rtu_slave.ps1 -ComPort COM7 -ReplyDelayMs 0
# 板子复位后等 60 秒
# COM3: vgruntime dump
```

修复前：`bus boot_timeout` 与 `bus boot_total` 同量级，只有 3-4 个点位 `online=1`。
修复后：`boot_timeout=0`、`boot_crc=0`、`boot_other=0`，14 个点位全部在线。

### 原始日志

- `slave-com7.log`、`slave-com7-0ms2.log`、`slave-com7-5ms.log`、`slave-com7-20ms.log`、
  `slave-com7-150ms.log`、`slave-com7-150ms2.log`：修复前的从站日志。
- `vgruntime-0ms.log`、`vgruntime-5ms.log`、`vgruntime-150ms.log`：修复前的板端 dump。
- `d04-fixed-0ms-slave.log`、`d04-fixed-5ms-slave.log`、`d04-fixed-20ms-slave.log`、
  `d04-fixed-150ms-slave.log`：修复后的从站日志。
- `d04-fixed-vgruntime-0ms.log`、`d04-fixed-vgruntime-5ms.log`、
  `d04-fixed-vgruntime-20ms.log`、`d04-fixed-vgruntime-150ms.log`：修复后的板端 dump。
- `d04-fix-d02-recheck.log`：同一版固件上 D-02 复测。
- `build-d04-fix.log`、`flash-d04-fix.log`：修复后的构建与烧录。
- `.debug/gdb/measure-dir-delay.cmd`、`.debug/gdb/measure-tc-to-dir.cmd`：时序测量脚本。
