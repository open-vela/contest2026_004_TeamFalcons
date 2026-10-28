# D-02 根因定位：NSH 控制台线程栈耗尽（2026-10-18）

本文记录 D-02（`/data/velaguard/discover/` 目录元数据改写触发 HardFault 并导致整机失联）
的根因、定位方法和板端实测数据。结论：**不是 vfat 或 eMMC 的问题，是 NSH 控制台线程的
栈不够用**，溢出写坏了 NuttX 放在栈底的 `tls_info_s`，随后 `getpid()` 解引用坏指针而崩溃。

## 一、调试链路

`ST-LINK_gdbserver` 在这台机器上始终报 `Target no device found`（`-g`、`-k`、降频都试过），
而 `STM32_Programmer_CLI -c port=SWD mode=UR` 能连上。改用 **OpenOCD 0.12 的 st-link
适配器驱动（dapdirect）** 即可复位下连接，配置文件见 `.debug/gdb/stlink-h7.cfg`：

```tcl
source [find interface/stlink.cfg]
transport select swd
adapter serial 0043001D3137511133333639
adapter speed 1000
source [find target/stm32h7x.cfg]
reset_config srst_only srst_nogate connect_assert_srst
```

连接结果：`SWD DPIDR 0x6ba02477`，`Cortex-M7 r1p1`，8 个硬件断点、4 个硬件监视点。

注意：这份配置带 `connect_assert_srst`，**每次 GDB attach 都会复位板子**，所以填充栈和
回读高水位必须在同一个 GDB 会话里完成。

## 二、故障链条

`getpid()` 的反汇编（`.debug/nuttx.elf`，`9009608c` 起）：

```asm
9009608c <getpid>:
  9009608c: ldr  r3, [pc, #12]     @ r3 = &g_readytorun
  9009608e: ldr  r3, [r3, #0]      @ r3 = g_readytorun.head  (当前 TCB)
  90096090: ldr  r0, [r3, #112]    @ r0 = tcb->stack_alloc_ptr      (偏移 0x70)
  90096092: cbz  r0, ...
  90096094: ldr  r0, [r0, #0]      @ r0 = tls_info_s->tl_task
  90096096: cbz  r0, ...
  90096098: ldr  r0, [r0, #48]     @ r0 = task_info_s->ta_pid       (偏移 0x30)
  9009609a: bx   lr
```

来源：`libs/libc/sched/task_getpid.c` 的 `task_get_info()` 与
`include/nuttx/tls.h` 的 `tls_get_info()`（flat build 下展开为
`this_task()->stack_alloc_ptr`）。

故障现场（`arm_hardfault` 入口断点）：

```
CFSR = 0x00008200   (BFARVALID | PRECISERR)
HFSR = 0x40000000   (FORCED，由 BusFault 升级)
BFAR = 0x0000003b
```

`BFAR = r0 + 0x30` 推出 `tl_task = 0x0b`，即 `stack_alloc_ptr` 指向的
`tls_info_s` 第一个字被写成了 `0x0b`。

## 三、NuttX 的栈布局

`arch/arm/src/common/arm_stackframe.c` 的 `up_stack_frame()` 把 `tls_info_s`
放在栈分配的**最低地址**，然后把 `stack_base_ptr` 抬高、扣减 `adj_stack_size`：

```c
ret = tcb->stack_base_ptr;
memset(ret, 0, frame_size);
tcb->stack_base_ptr  = (uint8_t *)tcb->stack_base_ptr + frame_size;
tcb->adj_stack_size -= frame_size;
```

`sched/tls/tls_initinfo.c` 里有 `DEBUGASSERT(info == tcb->stack_alloc_ptr)`，
确认 `tls_info_s` 就落在 `stack_alloc_ptr` 上。

所以栈是**向下增长、直冲 `tls_info_s`** 的：溢出第一个打坏的就是
`tl_task`（偏移 0），接着是 `tl_elem`、`tl_size` 等。

板端实测布局（修复前，pthread 默认栈）：

| 项 | 值 |
|---|---|
| 控制台线程 TCB | `0x38002af0`（pid=4，entry=`nsh_thread`） |
| `stack_alloc_ptr` | `0x38002be0` |
| 总分配 | 2048 字节（`CONFIG_PTHREAD_STACK_DEFAULT`） |
| `tls_info_s` | `[0x38002be0, 0x38002bf8)`，24 字节 |
| 可用栈 | 2024 字节 |

## 四、抓到写入者

在 `0x38002be0`（`tl_task` 所在字）下硬件监视点，再从 COM3 发复现命令：

```
Old value = 939525504 (0x38000580)   New value = 11 (0x0b)
0x90001684 in arm_svcall ()          sp = 0x38002be0
```

`sp` 正好落在栈分配的最低地址，也就是 `tls_info_s` 上。当时的调用链：

```
arm_svcall <- arm_doirq <- exception_common <- nxsched_switch
  <- nxsem_wait_slow <- stm32_eventwait <- mmcsd_eventwait <- mmcsd_read
  <- fat_hwread <- fat_fscacheread <- fat_findsfnentry <- fat_putlfname
  <- fat_dirnamewrite <- fat_rename <- rename <- cmd_mv <- nsh_command
  <- nsh_execute <- nsh_parse_command <- nsh_session <- nsh_consolemain
  <- nsh_thread <- pthread_startup
```

共 24 层。栈底往上能看到多组 `0x90000ee9`（`arm_doirq` 返回地址）、
`0x900010cf`（`exception_common` 返回地址）和 `0x24000264`（`&g_readytorun`
字面量）的异常现场，说明 SVC 上下文保存一路压到了分配底部。

## 五、对照实验

同样的 `mv` 在 `/data/velaguard/diagnostics/` 里执行，**监视点不触发**，命令正常完成。
所以溢出深度与具体路径相关：`discover_bad/` 那条 vfat 路径刚好越界，
`diagnostics/` 没有。这解释了此前观察到的"只有这一个目录会崩"。

## 六、量化：需要 2048 字节，可用只有 2024 字节

修复后用 8192 字节栈，把未使用部分填成 `0xdeadbeef`，跑完同一条 `mv` 再回读：

```
stack alloc = [0x38002be0, 0x38004be0)  adj_stack_size = 8168
filled 7192 bytes below sp
high-water mark = 0x380043e0
deepest use     = 2048 bytes of 8168 available
headroom left   = 6120 bytes
tls_info_s->tl_task after = 0x38000580     (未变)
```

即这条路径需要 **2048 字节**，而修复前可用 **2024 字节**，差 **24 字节**。
24 字节正好是 `tls_info_s` 的大小，所以溢出的部分不多不少压在 `tl_task` 上。
这解释了为什么现象是"数据操作已经完成、收尾时才崩"：溢出发生在 `mv` 期间，
`mv` 本身正常结束，紧随其后的 `getpid()` 才踩到坏指针。

## 七、修复

| 文件 | 改动 |
|---|---|
| `app/velaguard/Kconfig` | 新增 `CONFIG_VG_NSH_STACKSIZE`，默认 8192 |
| `app/velaguard/velaguard.c` | 控制台线程改用 `pthread_attr_setstacksize`；加 `#error` 下限检查 |
| `scripts/configs/velaguard-lvgl.defconfig` | 增加 `CONFIG_VG_NSH_STACKSIZE=8192` |

板端确认：`ps` 里 PID 4 的 STACK 从 `0002024` 变为 `0008168`。

## 八、板端复验（2026-10-18 17:43:24 构建）

```
nsh> echo AAA > /data/velaguard/discover_bad/r1.txt
nsh> echo BBB > /data/velaguard/discover_bad/r2.txt
nsh> mv /data/velaguard/discover_bad/r1.txt /data/velaguard/discover_bad/r2.txt
nsh> cat /data/velaguard/discover_bad/r2.txt
AAA
nsh> ls /data/velaguard/discover_bad
/data/velaguard/discover_bad:
 z2.txt
 x2.txt
 discover_state.bin
 point_table_candidate.json
 y2.txt
 r2.txt
nsh> uname -a
NuttX  0.0.0 75cbc8eb7a5-dirty Oct 18 2026 17:43:24 arm stm32h750b-dk
```

修复前同一串命令 4/4 次导致 HardFault 和整机失联；修复后命令完成、`cat` 内容正确、
板子继续响应。`discover_bad/` 作为现场证据保留。

## 九、与上一轮否定结论的关系

上一轮曾把线程栈提到 8192 并烧录实测，记录为"崩溃签名不变，假设不成立"。本轮按同样思路
重新实现后，板端复验通过，且直接量出需求 2048 字节对可用 2024 字节。上一轮的否定结果
本轮无法重现。

可能的原因是上一轮那次构建没有真正把栈改上去。本仓库确实存在同类陷阱：
增量构建复用 `nuttx/.config`，新 Kconfig 符号不会自动加入，本轮构建前就因此触发过
`#error "VelaGuard console shell requires CONFIG_VG_NSH_STACKSIZE >= 8192"`。
这只是可能解释，本轮没有留下上一轮那次构建的 `.config` 或 `ps` 输出可以核对，
不把它当成已证实的结论。

## 十、未覆盖的部分

- 只验证了 `discover_bad/` 这条复现路径。`config/`、`reports/`、`live/` 等其他目录下
  更深的操作没有逐条量过栈。
- 8192 字节是按 4 倍余量选的，高水位 2048 字节，余量 6120 字节。
- 其他线程的栈没有跟着调整：PID 5/12 是 12264，PID 7/8 是 2024，PID 6/13 是 4072。
  其中 PID 7（ESP8266）和 PID 8（netinit）仍是 2024 字节，但本轮没有证据说明它们会走到
  同样深的 vfat/SDMMC 路径。
