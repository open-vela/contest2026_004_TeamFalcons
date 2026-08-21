# STM32H750B-DK 的 QSPI-XIP 构建、烧录与启动机制

本文解释本项目在 STM32H750B-DK 上使用外部 QSPI 作为主镜像、片内
Flash 作为启动 stub 的完整链路。重点不是“执行哪条命令”，而是说明：

- 地址为什么是 0x08000000 和 0x90000000 两套空间；
- Kconfig 如何影响链接脚本，链接脚本如何影响 ELF、HEX 和 BIN；
- 为什么板上需要两个持久化镜像；
- STM32CubeProgrammer 的 External Loader 到底是什么；
- boot stub 如何把 QSPI 变成 CPU 可以直接取指的 XIP 地址；
- 烧录成功但不能启动时，应该从哪里开始定位。

本文对应当前仓库的实现，主要参考：

- [build.sh](../scripts/build.sh)
- [flash.sh](../scripts/flash.sh) / [flash.ps1](../scripts/flash.ps1)
- [openvela-qspi-boot-stm32h750b-dk.patch](../scripts/openvela-qspi-boot-stm32h750b-dk.patch)
- [stm32h750b_qspi_bootstub.c](../scripts/qspi_boot_stub/stm32h750b_qspi_bootstub.c)
- [stm32h750b_qspi_bootstub.ld](../scripts/qspi_boot_stub/stm32h750b_qspi_bootstub.ld)
- [windows_build_debug_setup.md](./windows_build_debug_setup.md)

日常操作入口是参赛仓 `scripts/build.sh` 与 `scripts/flash.sh`，不是父目录
openvela 的 `./build.sh`。

## 1. 先记住整体结论

板上有两个持久化镜像，PC 上还有一个只在烧录期间使用的临时算法：

~~~text
PC / Windows
  └─ STM32_Programmer_CLI.exe
       └─ MT25TL01G_STM32H750B-DISCO.stldr
            └─ 只加载到 CubeProgrammer/目标 RAM 中执行
               不属于用户固件，不负责上电启动

板上
  ├─ 内部 Flash 0x08000000
  │    └─ qspi_bootstub.hex
  │         └─ 复位后首先执行
  │
  └─ 外部 QSPI 的 memory-mapped 窗口 0x90000000
       └─ nuttx.hex
            └─ VelaGuard/NuttX 主镜像，最终从 QSPI XIP 执行
~~~

复位后的实际链路是：

~~~text
复位
  → Cortex-M 从 0x08000000 取 boot stub 向量表
  → 执行 boot stub Reset_Handler
  → 初始化时钟、QSPI GPIO 和 QUADSPI 控制器
  → 将 QSPI 配置为 memory-mapped read 模式
  → 从 0x90000000 读取主镜像初始 MSP
  → 从 0x90000004 读取主镜像 Reset_Handler
  → VTOR = 0x90000000
  → MSP/PSP = 主镜像初始 MSP
  → BX 跳转到主镜像 Reset_Handler
  → NuttX 启动代码清零 .bss、复制 .data、初始化系统
  → VelaGuard 从 QSPI XIP 运行
~~~

这里的 XIP 是 Execute In Place：代码仍然存放在外部 Flash 中，CPU 通过
0x90000000 地址窗口直接读取指令，不需要先把整个 .text 复制到 SRAM。

## 2. 地址空间：0x08000000、0x09000000 和 0x90000000

这是本项目最容易误读的地方。

### 2.1 实际使用的地址范围

| CPU 地址范围 | 大小 | 对应对象 | 是否存放板载持久化镜像 |
|---|---:|---|---|
| 0x08000000 到 0x08020000 | 128 KiB | STM32H750 片内 Flash | 是，boot stub |
| 0x90000000 到 0x98000000 | 128 MiB 地址窗口 | 外部双 QSPI memory-mapped 空间 | 是，主镜像 |
| 0x24000000 到 0x24080000 | 512 KiB | AXI SRAM | 否，运行时 .data、.bss、堆栈 |

0x08000000 是一个地址，不是“128 MB 的容量”。片内 Flash 的容量由
链接脚本中的 LENGTH = 128K 表示：

~~~text
0x08000000 + 0x20000 = 0x08020000
0x20000 = 131072 bytes = 128 KiB
~~~

当前 boot stub 只有约 720 bytes，因此它完全放得进这 128 KiB。

### 2.2 0x08000000 到 0x09000000 不是一段连续 Flash

如果把两个地址相减：

~~~text
0x09000000 - 0x08000000 = 0x01000000 = 16 MiB
~~~

这只说明 CPU 地址编号之间隔着 16 MiB 的地址空间，不能说明板上存在
16 MiB 的片内 Flash。当前实现没有使用从 0x08000000 延伸到
0x09000000 的连续存储区。

而且主镜像的地址是：

~~~text
0x90000000
~~~

不是：

~~~text
0x09000000
~~~

两者少了一个十六进制数字，含义完全不同。0x90000000 是 STM32H7
为外部 QSPI memory-mapped 访问保留的 CPU 地址窗口起点。

因此，片内 Flash 和外部 QSPI 之间存在很大的地址空洞是正常的：

~~~text
CPU 地址空间

0x08000000  ┌─────────────────────────────┐
            │ 片内 Flash，实际只有 128 KiB │
0x08020000  └─────────────────────────────┘
            │ 未使用的地址空间             │
0x90000000  ┌─────────────────────────────┐
            │ 外部 QSPI 映射窗口，128 MiB  │
0x98000000  └─────────────────────────────┘
~~~

链接器只关心 CPU 运行时看到的地址。它不要求两个物理存储器在地址上
连续，也不要求 0x08020000 后面必须紧接着出现下一个 Flash 区域。

### 2.3 为什么链接器声明 QSPI 为 128 MiB

当前 QSPI 链接脚本中有：

~~~text
flash (rx) : ORIGIN = 0x90000000, LENGTH = 128M
~~~

这表示“允许主镜像使用的 CPU 地址窗口上限”，不是说每次构建都会生成
128 MiB 文件，也不是说当前 HEX 会填满整段地址空间。

当前示例主镜像的有效代码大约只有 160 KiB，实际只占用：

~~~text
0x90000000 ... 0x90027b38
~~~

其余 QSPI 地址没有出现在 HEX 数据记录中，不会被写入。烧录脚本只校验
镜像是否落在 0x90000000 到 0x98000000 的合法窗口内。

## 3. 为什么必须有两个板载镜像

STM32 复位时，Cortex-M 会立即从当前可执行的启动地址读取向量表。
此时外部 QSPI 还没有被配置成 memory-mapped 模式，CPU 不能直接把
0x90000000 当作普通指令地址使用。

所以不能把完整 NuttX 镜像只放到 QSPI 后直接复位。必须先有一小段能在
片内 Flash 执行的代码完成硬件准备：

| 镜像 | 执行位置 | 主要职责 |
|---|---|---|
| qspi_bootstub | 片内 Flash 0x08000000 | 初始化时钟、QSPI 引脚、QUADSPI 控制器，检查并跳转主镜像 |
| nuttx | 外部 QSPI 映射到 0x90000000 | NuttX、VelaGuard、驱动、应用和只读数据 |

boot stub 不是完整的 BootROM，也不是第二套操作系统。它只做“让主镜像
可执行并把控制权交出去”这件事。

## 4. 构建链路：从 build.sh 到两个 ELF

日常在 Remote - WSL 执行（不是父目录 openvela 的 `./build.sh`）：

~~~bash
bash scripts/build.sh lvgl --clean
~~~

真正的 openvela 编译在 WSL 中完成。大致链路如下：

~~~text
scripts/build.sh
  → 设置 ARM GCC、Kconfig、Python 和构建工具 PATH
  → ensure-openvela-links.sh
  → 重新生成 packages/demos/Kconfig
  → 按目标幂等 apply（QSPI / ETH / UI / defconfig）
  → 缺少 bootstub 或 --clean 时编译 qspi_bootstub
  → 清空 .debug 旧主镜像
  → 必要时 configure.sh -E -e stm32h750b-dk:<preset>
  → make -j
  → 复制 nuttx.{hex,bin,elf} 到 .debug
~~~

### 4.1 QSPI 补丁做了什么

apply-openvela-qspi-patch.sh 会把
openvela-qspi-boot-stm32h750b-dk.patch 应用到当前 NuttX checkout。
它会先检查反向补丁和关键标记，因此重复构建不会反复应用同一补丁。

补丁包含几类改动：

1. 在板级 Kconfig 增加 CONFIG_STM32H750B_DK_QSPI_BOOT；
2. 在 Make.defs 和 CMakeLists 中根据该配置选择 qspi_flash.ld；
3. 在 STM32H7 MPU 初始化中把 0x90000000 到 0x98000000
   标记为可执行的 Flash 区域；
4. 增加 qspi_flash.ld；
5. 保留项目当前需要的 FT5X06 触摸能力补丁。

如果补丁没有应用，可能出现两种典型问题：

- Kconfig 中没有 QSPI 选项；
- 主镜像仍使用普通 flash.ld，链接地址落在 0x0800xxxx。

### 4.2 Kconfig 如何切换链接脚本

PowerShell 生成的 WSL 构建脚本会强制启用：

~~~text
CONFIG_STM32H750B_DK_QSPI_BOOT=y
~~~

随后板级构建文件做选择：

~~~text
CONFIG_STM32H750B_DK_QSPI_BOOT = y
  → LDSCRIPT = qspi_flash.ld

CONFIG_STM32H750B_DK_QSPI_BOOT != y
  → LDSCRIPT = flash.ld
~~~

这一步是整个机制的分水岭。它不是在烧录阶段把一个普通 BIN
“搬到 QSPI”，而是在链接阶段就让所有代码地址、函数指针、向量表和
只读数据地址变成 0x9000xxxx。

### 4.3 qspi_flash.ld 的关键含义

简化后的核心内容是：

~~~text
MEMORY
{
  flash (rx) : ORIGIN = 0x90000000, LENGTH = 128M
  sram  (rwx): ORIGIN = 0x24000000, LENGTH = 512K
}

.text :   { ... } > flash
.data :   { ... } > sram AT > flash
.bss  :   { ... } > sram
~~~

这里的 flash 只是链接脚本里的区域名字。在 boot stub 的链接脚本里，
同名的 flash 指的是 0x08000000 的片内 Flash；在主镜像链接脚本里，
同名的 flash 指的是 0x90000000 的 QSPI 映射窗口。区域名字相同，
地址和物理对象并不相同。

#### VMA 与 LMA

AT > flash 是理解 QSPI 主镜像的关键：

- VMA（Virtual Memory Address）：程序运行时访问变量的地址；
- LMA（Load Memory Address）：初始数据在镜像中保存的位置。

主镜像的典型布局如下：

| 段 | 运行地址 VMA | 镜像保存地址 LMA | 运行方式 |
|---|---|---|---|
| .text、.rodata、向量表 | 0x9000xxxx | 0x9000xxxx | 直接从 QSPI XIP |
| .data | 0x2400xxxx | .text/.rodata 之后的 QSPI 地址 | 启动时复制到 SRAM |
| .bss | 0x2400xxxx | 无初始化数据 | 启动时清零 |

链接脚本定义的 _eronly 是只读区域末尾，也是 .data 初始值在
镜像中的起点。STM32H7 启动代码会把：

~~~text
[_eronly, _eronly + sizeof(.data))
  → [_sdata, _edata)
~~~

复制到 SRAM，然后把 [_sbss, _ebss) 清零。这个复制动作发生在 boot
stub 已经把 QSPI 设为 memory-mapped 之后，所以 _eronly 可以直接指向
0x9000xxxx。

当前 .debug/nuttx.elf 的实际符号可以用来验证这个关系：

~~~text
_stext   = 0x90000000
_eronly  = 0x90027b38
_sdata   = 0x24000000
_edata   = 0x24000794
_sbss    = 0x240007a0
_ebss    = 0x2400bd34
~~~

因此，.data 的“初始值”在 QSPI，但程序运行时访问的变量地址仍然是
0x24000000 附近的 SRAM 地址。

### 4.4 MPU 为什么也必须修改

NuttX 启动阶段会启用 Cortex-M7 MPU。如果 MPU 没有把 QSPI 映射窗口
设置为可执行区域，即使 boot stub 已经成功跳到 0x9000xxxx，主程序
也可能在取指时产生 MemManage/BusFault。

补丁中的逻辑等价于：

~~~text
mpu_priv_flash(0x90000000, 128 * 1024 * 1024)
~~~

因此，QSPI 机制需要同时满足：

1. 链接地址位于 0x9000xxxx；
2. boot stub 把 QSPI 配成 memory-mapped；
3. VTOR 指向外部向量表；
4. MPU 允许该窗口执行；
5. .data 的初始值能够从 QSPI 读取。

## 5. ELF、HEX、BIN：同一份程序的三种表现

### 5.1 ELF 是分析和调试的主文件

nuttx.elf 保存：

- section 和 segment；
- 绝对地址；
- 符号表；
- 调试信息；
- ELF entry point。

GDB 通过 ELF 找到源文件、函数和变量。ELF 不一定直接拿来给
CubeProgrammer 烧录，但它是判断链接是否正确的最佳文件。

### 5.2 HEX 保存了地址

arm-none-eabi-objcopy -O ihex 会把 ELF 的可加载内容转换为 Intel HEX。
HEX 通过扩展线性地址记录携带高位地址。

当前主镜像开头是：

~~~text
:0200000490006A
~~~

记录类型 04 表示 Extended Linear Address，数据 9000 表示后续数据
的高 16 位为 0x9000，因此后面的 offset 0x0000 实际落在：

~~~text
0x9000 << 16 | 0x0000 = 0x90000000
~~~

boot stub 的 HEX 开头是：

~~~text
:020000040800F2
~~~

所以它的后续数据落在 0x08000000。

这就是为什么当前烧录脚本直接使用 HEX：文件自身已经携带目标地址，
CubeProgrammer 不需要猜测它应该写到片内 Flash 还是外部 QSPI。

### 5.3 BIN 是无地址裸数据

arm-none-eabi-objcopy -O binary 会删除 ELF/HEX 的地址记录，只留下
连续字节。nuttx.bin 和 qspi_bootstub.bin 适合需要“文件加起始地址”
的工具，但当前 scripts/flash.ps1 不使用它们。

不要把 nuttx.bin 当成可以直接替代 nuttx.hex 的文件。若使用 BIN，
烧录工具必须另外知道目标地址和对应的 External Loader 参数。

## 6. 从当前 ELF 检查主镜像布局

当前仓库已有 .debug/nuttx.elf 时，可以执行：

~~~bash
readelf -h .debug/nuttx.elf
readelf -S .debug/nuttx.elf
readelf -s .debug/nuttx.elf
~~~

在 WSL 中若 ARM 工具链没有进入 PATH，可以使用 openvela 自带工具链下的
arm-none-eabi-readelf。

当前示例结果：

| section | 地址 | 大小 | 含义 |
|---|---:|---:|---|
| .text | 0x90000000 | 0x27b30 | 向量表、代码、只读数据 |
| .ARM.exidx | 0x90027b30 | 0x8 | ARM 异常索引信息 |
| .data | 0x24000000 | 0x794 | SRAM 运行区，初始值来自 QSPI |
| .bss | 0x240007a0 | 0xb594 | SRAM 清零区 |

主镜像向量表前 8 个字节当前为：

~~~text
34 c1 00 24 19 0b 00 90
~~~

按小端序解释：

~~~text
*(uint32_t *)0x90000000 = 0x2400c134  初始 MSP
*(uint32_t *)0x90000004 = 0x90000b19  Reset_Handler | Thumb bit
~~~

0x90000b19 的最低位为 1，表示 Cortex-M Thumb 状态。最低位不是代码
实际对齐地址的一部分，而是异常/分支入口的状态标记。

当前 ELF 的 entry point 是 0x90000299。调试时不要只看 ELF header 的
entry point；Cortex-M 复位跳转的权威来源是向量表第二个 word，即
0x90000004。两者都应位于 QSPI 代码区，且向量表中的入口必须带
Thumb bit。

## 7. boot stub 是如何初始化 QSPI 并跳转的

### 7.1 boot stub 的构建

build_bootstub.sh 使用独立的裸机编译命令：

~~~text
arm-none-eabi-gcc
  -mcpu=cortex-m7
  -mthumb
  -mfpu=fpv5-d16
  -mfloat-abi=hard
  -Os
  -ffreestanding
  -nostartfiles
  -nostdlib
  -Wl,--gc-sections
  -T stm32h750b_qspi_bootstub.ld
  stm32h750b_qspi_bootstub.c
~~~

它不链接 NuttX，也不依赖 C 运行库。这样做的目的就是让它足够小、
足够早、足够独立。

boot stub 链接脚本声明：

~~~text
flash (rx) : ORIGIN = 0x08000000, LENGTH = 128K
ram   (rwx): ORIGIN = 0x24000000, LENGTH = 512K
~~~

当前 boot stub 的关键布局：

~~~text
g_vectors     = 0x08000000
Reset_Handler = 0x08000090 附近
_estack       = 0x24080000
~~~

由于 Cortex-M 函数入口需要 Thumb 状态，向量表中保存的 Reset_Handler
地址会是奇地址，例如当前产物中的 0x08000091。

### 7.2 向量表

boot stub 的向量表位于片内 Flash 起点：

~~~text
*(uint32_t *)0x08000000 = 0x24080000  boot stub 初始 MSP
*(uint32_t *)0x08000004 = 0x08000091  boot stub Reset_Handler | Thumb bit
~~~

向量表由 g_vectors 定义，并按 512 字节对齐。对当前启动链来说，最重要
的是前两个 word；其余异常入口暂时都指向 Default_Handler。

### 7.3 时钟和 GPIO

clock_init() 做最小的系统时钟准备，包括：

- 设置 Flash 访问等待周期；
- 配置 RCC 时钟分频；
- 配置 PLL1；
- 等待 PLL1 ready；
- 切换系统时钟源。

qspi_gpio_init() 打开 GPIO 和 QUADSPI 外设时钟，并为 D、F、G、H
端口设置：

- Alternate Function；
- 输出速度；
- 复用选择；
- QSPI 相关片选、时钟、数据线功能。

这些寄存器是直接按芯片地址访问的，不经过 HAL，原因是 boot stub 要
尽量小，并且必须在 NuttX 尚未启动时工作。

### 7.4 QUADSPI memory-mapped 配置

qspi_enter_memory_mapped() 的高层顺序是：

~~~text
1. 初始化 QSPI GPIO
2. 配置 QSPI_CR / QSPI_DCR / QSPI_LPTR
3. 配置一次命令模式并使能 QSPI
4. ABORT，等待 BUSY 清零
5. 发送退出 QPI 的命令，避免上一次镜像留下异常协议状态
6. 再次 ABORT
7. 将 CCR 设置为 memory-mapped READ
8. 从此 CPU 访问 0x90000000 就会触发 QSPI 读事务
~~~

当前实现使用的是双 QSPI 芯片的 SPI 访问路径，而不是 QPI 路径。源码
注释记录了本板验证结果：

- CCR = 0x0d003513 的 SPI memory-mapped 路径能够在
  0x90000000 读到有效向量表；
- QPI 试验值 0x0f283fec 读出的结果无效，不能启动；
- 该序列与 OpenOCD 板级配置中的 qspi_init 0 路径保持一致。

这里的关键不是记住一个“魔法常量”，而是理解 CCR 配置必须与板上
MT25TL01G 的连接方式、SPI/QPI 协议、地址宽度、dummy cycles 和
memory-mapped 模式相匹配。更换板型、Flash 器件或接线后，不能直接照搬
这些数值。

### 7.5 检查主镜像并跳转

boot stub 从 QSPI 取出两个 word：

~~~c
app_stack = *(uint32_t *)0x90000000;
app_entry = *(uint32_t *)0x90000004;
~~~

然后检查：

- app_stack 位于合法 SRAM 地址范围；
- app_stack 至少 4 字节对齐；
- app_entry 位于 0x90000000 到 0x98000000；
- app_entry 的最低位为 1，表示 Thumb。

如果检查失败，boot stub 进入 Default_Handler()，只执行 WFI，不会
跳到一个随机地址。

检查成功后，代码做四件事：

~~~text
SCB->VTOR = 0x90000000
DSB / ISB
MSP = app_stack
PSP = app_stack
CONTROL = 0
BX app_entry
~~~

设置 VTOR 很重要。即使 PC 已经跳到 QSPI，如果 VTOR 仍然指向
0x08000000，主程序产生异常时仍可能使用 boot stub 的异常向量表。

## 8. 烧录脚本到底做了什么

执行：

~~~bash
bash scripts/flash.sh
~~~

不想重新构建、只烧录已有 .debug 产物时，Download 本来就不编译。

### 8.1 烧录前的地址验证

scripts/flash.ps1 中的 Get-IntelHexAddressRange 会解析每个 HEX
数据记录，并处理：

- 类型 00：实际数据；
- 类型 02：Extended Segment Address；
- 类型 04：Extended Linear Address。

它维护一个当前高位基地址，把每个数据记录的 offset 加上基地址，计算
整个 HEX 的最小地址和最大结束地址。

随后执行两个范围检查：

~~~text
主 QSPI 镜像：
  0x90000000 <= address < 0x98000000

片内 boot stub：
  0x08000000 <= address < 0x08020000
~~~

如果主镜像仍然落在 0x0800xxxx，脚本会在接触硬件前拒绝烧录。这是
一个很有价值的安全闸门：它避免把普通内部 Flash 镜像误当成 QSPI 镜像。

### 8.2 External Loader 的真实角色

第一条 CubeProgrammer 命令等价于：

~~~text
STM32_Programmer_CLI.exe
  -c port=SWD mode=UR
  -el MT25TL01G_STM32H750B-DISCO.stldr
  -d .debug/nuttx.hex
  -v
~~~

-el 指定的是 STM32CubeProgrammer 的外部存储器编程算法。它通常会被
下载/运行在目标 RAM 或由 Programmer 控制执行，用来完成：

- 识别外部 Flash；
- 解锁；
- 擦除；
- 写入；
- 读取回校验。

它不会在复位后留在板上，也不会替代 boot stub。即使外部 Loader 文件
叫 .stldr，它也不是第三份需要长期保存的固件。

### 8.3 两阶段写入和复位

脚本实际顺序是：

~~~text
1/3  External Loader 写入并校验 nuttx.hex 到外部 QSPI
2/3  普通 SWD 写入并校验 qspi_bootstub.hex 到片内 Flash
3/3  复位目标
~~~

先写主镜像、后写 boot stub 是有意的。这样直到 QSPI 主镜像准备好之前，
不会让新 boot stub 在复位后立即跳到一个尚未写完的外部地址。

连接参数是 port=SWD，因此当前流程使用的是 SWD，不要求 JTAG。
ST-LINK 负责通过 SWD 访问片内 Flash 和控制外部 Loader；串口 CTS、
RS485、ESP-01S 等业务引脚不参与 QSPI 烧录链路。

### 8.4 为什么当前流程不使用 OpenOCD 下载 QSPI

本项目已经验证：当前板上双 MT25TL01G 的 OpenOCD stmqspi 路径无法
稳定完成 JEDEC probe/write。CubeProgrammer 的
MT25TL01G_STM32H750B-DISCO.stldr 可以正常擦写和校验，因此职责被
明确分开：

~~~text
CubeProgrammer + External Loader → QSPI 下载
CubeProgrammer 普通 SWD         → 片内 boot stub 下载
OpenOCD                         → 仅提供 GDB server 调试
~~~

## 9. 推荐的验证步骤

### 9.1 构建

~~~bash
bash scripts/build.sh lvgl --clean
~~~

确认 .debug 至少包含：

~~~text
nuttx.elf
nuttx.hex
nuttx.bin
qspi_bootstub.hex
~~~

### 9.2 不接板只检查地址

~~~bash
bash scripts/flash.sh -ValidateOnly
~~~

期望日志类似：

~~~text
Main QSPI image : 0x90000000..0x900xxxxx
Internal stub   : 0x08000000..0x08000xxx
Validation completed; hardware was not accessed.
~~~

### 9.3 烧录

~~~bash
bash scripts/flash.sh
~~~

烧录结束后脚本会自动复位。若只使用现有产物，必须确认 .debug 中的
ELF 与 HEX 来自同一次构建，避免 GDB 符号与板上代码不一致。

### 9.4 GDB/调试

OpenOCD attach 时应加载与板上镜像匹配的 nuttx.elf 符号，但不要让
GDB 再次执行 load。当前工程用 `request: attach`（只 halt），不要写
`loadFiles: []`：Cortex-Debug 会因此跳过 `file-exec-and-symbols`，
GDB 报 `No symbol table is loaded`，源码断点一直空心。

~~~text
CubeProgrammer 负责真实烧录
GDB 只负责符号、断点、单步和寄存器观察
~~~

进入调试后优先观察：

~~~text
PC
MSP
SCB->VTOR
0x90000000
0x90000004
~~~

如果能访问目标内存，可以检查：

~~~text
x/8wx 0x90000000
x/8wx 0x08000000
info registers pc msp
~~~

期望看到：

~~~text
0x90000000: 0x2400xxxx
0x90000004: 0x9000xxxx   且最低位为 1
PC:         0x9000xxxx
VTOR:       0x90000000
~~~

## 10. 故障定位树

| 现象 | 先观察 | 常见原因 | 处理 |
|---|---|---|---|
| 构建成功但主 HEX 地址是 0x0800xxxx | readelf -S nuttx.elf | QSPI Kconfig 没开、补丁未应用、仍用了 flash.ld | 重新执行完整构建，确认 CONFIG_STM32H750B_DK_QSPI_BOOT=y |
| Main QSPI image address range is invalid | scripts/flash.sh -ValidateOnly | 主镜像没有链接到 0x90000000 | 不要强行烧录，先修复链接配置 |
| QSPI 写入/校验失败 | CubeProgrammer 输出、External Loader 路径 | Loader 不存在、型号不匹配、外部 Flash 连接或供电异常 | 检查 .stldr、板型和供电，关闭占用 ST-LINK 的程序 |
| 复位后 PC 仍在 0x0800xxxx | PC、片内 Flash 前 8 字节 | boot stub 未写入、没有复位、烧录的是旧 stub | 重新烧录 stub 并复位 |
| PC 停在 boot stub，主镜像不运行 | 0x90000000 和 0x90000004 | QSPI 未进入映射模式、主 HEX 未正确写入、向量无效 | 先检查 External Loader 和 HEX 前两项 |
| boot stub 进入 WFI | 主向量两个 word | MSP 不在 SRAM、入口不在 QSPI、Thumb bit 丢失 | 检查主镜像向量表和地址范围 |
| PC 已到 0x9000xxxx 后立即 Fault | VTOR、MPU、Fault 状态寄存器 | MPU 不允许执行、QSPI 读配置错误、VTOR 未切换 | 确认 QSPI 补丁、VTOR 和 CCR 配置 |
| 主程序启动但全局变量异常 | _eronly、_sdata、_edata | .data 的 LMA/VMA 或启动复制错误 | 检查 qspi_flash.ld 与 STM32H7 启动代码 |
| 能运行但断点位置不对 | ELF 与 build-info.txt | 板上固件和 GDB 使用了不同构建产物 | 重新构建、Cube 烧录并加载同一份 ELF |

### 10.1 最小判断顺序

遇到“烧录成功但不启动”，按下面顺序排查，效率最高：

~~~text
1. 主 HEX 是否真的包含 0x9000 的扩展线性地址记录
2. boot stub HEX 是否真的包含 0x0800 的扩展线性地址记录
3. QSPI External Loader 是否完成 verify
4. 片内 Flash 前两个 word 是否是 0x2400xxxx / 0x0800xxxx
5. 外部 QSPI 前两个 word 是否是合法 MSP / 0x9000xxxx|1
6. 复位后 PC 是否离开 0x0800xxxx
7. VTOR 是否为 0x90000000
8. PC 到 0x9000xxxx 后是否发生 MPU/BusFault
~~~

## 11. 当前产物的可复核样例

当前仓库 .debug 中的一组产物可以作为地址 sanity check：

~~~text
主镜像：
  ELF entry point       0x90000299
  .text                 0x90000000, size 0x27b30
  .data VMA             0x24000000, size 0x794
  .bss                  0x240007a0, size 0xb594
  初始 MSP              0x2400c134
  Reset_Handler vector  0x90000b19

boot stub：
  ELF entry point       0x08000091
  向量表                 0x08000000
  初始 MSP              0x24080000
  Reset_Handler vector  0x08000091
  qspi_bootstub.bin      720 bytes
~~~

这些数值会随代码和配置改变；稳定不变的是布局不变量：

~~~text
主镜像向量表和代码       → 0x90000000 地址空间
boot stub 向量表和代码    → 0x08000000 地址空间
主镜像运行时 .data/.bss   → SRAM
boot stub 先初始化 QSPI   → 主镜像才能执行
~~~

## 12. 一句话理解整个机制

链接器先把 NuttX 主程序“写成运行在 0x90000000 的程序”，
CubeProgrammer 再按 HEX 地址把它写入外部 QSPI；复位时，片内
0x08000000 的 boot stub 把外部 Flash 映射到这个地址、切换 VTOR、
装载主镜像栈指针，然后跳到 0x9000xxxx，从而实现 QSPI XIP。
