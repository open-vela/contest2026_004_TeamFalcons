# 从空白工程到点亮 STM32H750B-DK 第一颗 LED

本文记录我们这一路从 openvela 竞赛模板走到 STM32H750B-DK 点亮第一颗 LED 的完整过程。

它不是只告诉你“改哪一行”，而是帮你建立一套迁移思维：你已经有 STM32HAL、FreeRTOS、LwIP、FatFs、Modbus 经验，那么学习 openvela/NuttX 时，最重要的是把已有经验映射到 NuttX 的概念上。

## 0. 你应该先建立的总图

在 STM32HAL 工程里，你大概会这样点灯：

```c
HAL_GPIO_WritePin(GPIOJ, GPIO_PIN_2, GPIO_PIN_RESET);
HAL_Delay(500);
HAL_GPIO_WritePin(GPIOJ, GPIO_PIN_2, GPIO_PIN_SET);
HAL_Delay(500);
```

在 openvela/NuttX 里，我们最终希望应用层这样写：

```c
board_userled(BOARD_LED_GREEN, true);
usleep(500 * 1000);
board_userled(BOARD_LED_GREEN, false);
usleep(500 * 1000);
```

两者最大的区别是：

```text
STM32HAL 写法：
应用直接知道 GPIO 端口、引脚、电平极性。

NuttX/openvela 推荐写法：
应用只表达“我要控制逻辑 LED”，具体 GPIO、极性、板子差异交给 BSP。
```

所以这条链路是：

```text
contest app
  -> board_userled_initialize()
  -> board_userled(BOARD_LED_GREEN, true/false)
  -> nuttx/boards/.../stm32_userleds.c
  -> stm32_gpiowrite()
  -> STM32 GPIO
  -> LED 亮灭
```

这就是我们这一路真正打通的东西。

## 1. 竞赛目录规则

你的当前竞赛仓库是：

```text
/home/debian19y/openvela/contest2026_004_TeamFalcons
```

openvela 全量源码在外层：

```text
/home/debian19y/openvela
```

关键目录关系：

```text
/home/debian19y/openvela/
  nuttx/                         公共 NuttX 内核和 BSP 仓
  apps/                          openvela/NuttX app 框架
  packages/                      package 聚合目录
  contest2026_004_TeamFalcons/   你的比赛仓
```

比赛规范的核心是：

```text
应用赛道代码：
放在 contest2026_004_TeamFalcons/app/hello_app/

BSP 修复：
可以本地验证，但正式提交不能混在你的应用仓 PR 里。
应该单独 fork 公共 nuttx 仓，向 dev-ai-contest-2026 分支提 PR。
```

你的应用实际路径是：

```text
contest2026_004_TeamFalcons/app/hello_app/
```

openvela 编译树里通过软链接映射到：

```text
packages/demos/contest2026_004_hello_app
```

这就是为什么你不需要手动 copy 文件。比赛 manifest 会通过 `<linkfile>` 处理。

## 2. 我们从模板工程改了什么

模板最开始是 `team 000 hello app`。

我们把它改成了你队伍的 app：

```text
team 000 -> team 004
hello_app -> first_led
```

涉及这些文件：

```text
app/hello_app/Kconfig
app/hello_app/Make.defs
app/hello_app/Makefile
app/hello_app/CMakeLists.txt
app/hello_app/hello_app_main.c
```

### 2.1 Kconfig 做什么

`Kconfig` 的作用类似你在 STM32HAL 工程里的各种宏开关，例如：

```c
#define HAL_TIM_MODULE_ENABLED
#define HAL_GPIO_MODULE_ENABLED
```

只不过 NuttX 用 Kconfig 统一管理。

我们的 app Kconfig 符号是：

```text
CONFIG_LVX_USE_DEMO_CONTEST2026_004_HELLO_APP
```

它的含义是：

```text
是否把你的 first_led app 编进系统。
```

如果这个没打开，你的代码即使写对了，也不会进最终固件。

### 2.2 Make.defs 做什么

`Make.defs` 告诉 NuttX：

```text
如果 CONFIG_LVX_USE_DEMO_CONTEST2026_004_HELLO_APP=y，
就把 packages/demos/contest2026_004_hello_app 加入编译。
```

这一步相当于 STM32HAL 工程里把某个 `.c` 文件加入工程。

### 2.3 Makefile 做什么

`Makefile` 里最关键的是：

```make
PROGNAME  = first_led
MAINSRC   = hello_app_main.c
```

含义是：

```text
这个应用叫 first_led。
主源文件是 hello_app_main.c。
```

在 NuttX 内置应用模式下，源码里仍然写：

```c
int main(int argc, char *argv[])
```

但是构建系统会根据 `PROGNAME = first_led` 处理入口符号，所以 `.config` 里启动入口使用：

```text
CONFIG_INIT_ENTRYPOINT="first_led_main"
CONFIG_INIT_ENTRYNAME="first_led"
```

你可以把它理解成：

```text
源码 main()
  -> 构建系统包装/重命名
  -> first_led_main()
  -> 系统启动后直接运行 first_led
```

## 3. 当前 app 的最终代码

当前文件：

```text
app/hello_app/hello_app_main.c
```

核心代码是：

```c
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>

#include <arch/board/board.h>
#include <nuttx/board.h>

/* Blink the board's first user LED through the NuttX board LED API.
 */

#define FIRST_LED BOARD_LED_GREEN

int main(int argc, char *argv[])
{
  uint32_t nleds = board_userled_initialize();

  if (nleds == 0)
    {
      printf("first_led: no user LEDs available\n");
      return 1;
    }

  for (; ; )
    {
      board_userled(FIRST_LED, true);
      usleep(500 * 1000);

      board_userled(FIRST_LED, false);
      usleep(500 * 1000);
    }
}
```

逐行解释：

```c
#include <arch/board/board.h>
```

这个头文件提供板级符号，比如：

```c
BOARD_LED_GREEN
BOARD_LED_RED
BOARD_NLEDS
```

也就是“这块板子有哪些逻辑 LED”。

```c
#include <nuttx/board.h>
```

这个头文件提供通用板级 API，例如：

```c
board_userled_initialize()
board_userled()
board_userled_all()
```

也就是“应用如何通过 NuttX 标准方式操作 LED”。

```c
#define FIRST_LED BOARD_LED_GREEN
```

这行很重要。它避免了硬编码：

```c
#define FIRST_LED 0
```

为什么不要硬编码 `0`？

因为 `0` 只是数组下标。今天 index 0 是 `PJ2`，明天 BSP 修了映射，index 0 也许仍是绿灯，但具体管脚可能变。应用层应该依赖语义符号，而不是依赖下标。

```c
uint32_t nleds = board_userled_initialize();
```

这相当于 HAL 里的 GPIO 初始化：

```c
HAL_GPIO_Init(...)
```

只是 NuttX 帮你封装在 BSP 里。

```c
board_userled(FIRST_LED, true);
```

含义是：

```text
点亮 FIRST_LED。
```

注意，应用层不应该关心它到底写高电平还是低电平。极性应该由 BSP 处理。

```c
usleep(500 * 1000);
```

类似 FreeRTOS 的：

```c
vTaskDelay(pdMS_TO_TICKS(500));
```

只是这里用 POSIX 风格的 `usleep()`，单位是微秒。

## 4. 三个 LED 配置开关一定要分清

这部分是我们一路上最关键的知识点。

### 4.1 CONFIG_ARCH_HAVE_LEDS

```text
CONFIG_ARCH_HAVE_LEDS=y
```

含义：

```text
这个板子的 BSP 声明：我提供标准 LED API。
```

它不是“内核接管 LED”，只是“这个板子有 LED 能力”。

有了它，`<nuttx/board.h>` 才会暴露这些函数声明：

```c
uint32_t board_userled_initialize(void);
void board_userled(int led, bool ledon);
void board_userled_all(uint32_t ledset);
```

如果没有它，你就会遇到这种 warning：

```text
warning: implicit declaration of function 'board_userled_initialize'
warning: implicit declaration of function 'board_userled'
```

这就是为什么我们不应该在 app 里手动写：

```c
uint32_t board_userled_initialize(void);
void board_userled(int led, bool ledon);
```

手动声明只是绕过编译器 warning，不是根治问题。

根治问题是让 BSP 正确：

```kconfig
config ARCH_BOARD_STM32H750B_DK
	bool "STM32H750B-DK board"
	depends on ARCH_CHIP_STM32H750B
	select ARCH_HAVE_LEDS
```

### 4.2 CONFIG_ARCH_LEDS

```text
# CONFIG_ARCH_LEDS is not set
```

含义：

```text
不要让 NuttX 内核把 LED 当成系统状态灯使用。
```

如果打开它：

```text
CONFIG_ARCH_LEDS=y
```

内核会拿 LED 表示启动、异常、panic、idle 等状态。

这时应用再调用 `board_userled()` 控制 LED，就会出现所有权冲突：

```text
内核也在改 LED
你的 app 也在改 LED
最后现象会变得很难判断
```

所以应用赛道点灯时，应该关闭它。

### 4.3 CONFIG_USERLED

```text
# CONFIG_USERLED is not set
```

含义：

```text
不注册 /dev/userleds 这个字符设备。
```

如果你以后想用文件设备方式控制 LED，例如：

```text
open("/dev/userleds")
ioctl(...)
```

那才考虑打开它。

现在我们直接调用 `board_userled()`，所以不需要它。

### 4.4 当前正确组合

我们当前希望的组合是：

```text
CONFIG_ARCH_HAVE_LEDS=y
# CONFIG_ARCH_LEDS is not set
# CONFIG_USERLED is not set
```

翻译成人话：

```text
板子提供 LED API。
内核不抢 LED。
/dev/userleds 不抢 LED。
应用通过 board_userled() 控制 LED。
```

## 5. 为什么之前会有 warning

之前编译时出现：

```text
warning: implicit declaration of function 'board_userled_initialize'
warning: implicit declaration of function 'board_userled'
```

它的意思不是“链接不到函数”，而是：

```text
编译 hello_app_main.c 时，编译器没有看到函数声明。
```

原因是：

```text
STM32H750B-DK BSP 实际实现了 board_userled_initialize() 和 board_userled()，
但 Kconfig 没有 select ARCH_HAVE_LEDS，
所以 <nuttx/board.h> 把这些函数声明藏起来了。
```

我们一开始临时手动声明，能让编译通过，但不优雅。

正确做法是：

```text
BSP 声明自己 HAVE_LEDS。
应用 include 标准头文件。
应用不手写外部函数声明。
```

## 6. 为什么 LED 现象一度很奇怪

你观察到过：

```text
LD6 闪烁
LD7 熄灭
LD8 常亮
```

这不是烧录失败，而是 BSP 的 LED 表和真实硬件不一致。

原本 openvela/NuttX 这个板级代码里大致是：

```text
index 0 -> PI13
index 1 -> PJ2
index 2 -> PD3
```

而你的 app 当时写的是：

```c
#define FIRST_LED 0
```

所以：

```text
FIRST_LED 0
  -> index 0
  -> PI13
  -> 你看到 LD6 闪烁
```

至于 `LD8` 常亮，是因为 BSP 把 `PD3` 也混进 user LED 表里，并且我们当时把所有 LED 都按 active-low 处理。`PD3/LD8` 不应该简单套入和 `PJ2/PI13` 一样的逻辑。

最后我们按更合理的 BSP 语义修成：

```text
BOARD_NLEDS = 2
BOARD_LED_GREEN -> PJ2
BOARD_LED_RED   -> PI13
```

并且不再把 `PD3` 纳入 `board_userled()` 管理。

## 7. LED 极性为什么要放在 BSP 修

STM32H750B-DK 上用户 LED 是 active-low。

含义：

```text
GPIO 写 0，LED 亮。
GPIO 写 1，LED 灭。
```

如果应用层自己写：

```c
board_userled(FIRST_LED, !ledon);
```

这就错了。

因为应用层不应该知道极性。

正确分层是：

```text
应用层：
board_userled(led, true) 表示我要点亮。

BSP 层：
发现硬件 active-low，于是内部写 !ledon。
```

BSP 里的关键逻辑是：

```c
void board_userled(int led, bool ledon)
{
  if ((unsigned)led < ARRAYSIZE(g_ledcfg))
    {
      stm32_gpiowrite(g_ledcfg[led], !ledon);
    }
}
```

这和你熟悉的 HAL BSP 类似：

```c
void BSP_LED_On(Led_TypeDef Led)
{
  HAL_GPIO_WritePin(port, pin, GPIO_PIN_RESET);
}
```

应用只调用：

```c
BSP_LED_On(LED_GREEN);
```

不应该在应用层到处写 `GPIO_PIN_RESET`。

## 8. BSP 我们实际修了什么

这些属于公共 `nuttx` 仓的修复：

```text
/home/debian19y/openvela/nuttx/boards/Kconfig
/home/debian19y/openvela/nuttx/boards/arm/stm32h7/stm32h750b-dk/include/board.h
/home/debian19y/openvela/nuttx/boards/arm/stm32h7/stm32h750b-dk/src/stm32h750b-dk.h
/home/debian19y/openvela/nuttx/boards/arm/stm32h7/stm32h750b-dk/src/stm32_userleds.c
/home/debian19y/openvela/nuttx/boards/arm/stm32h7/stm32h750b-dk/src/stm32_autoleds.c
```

### 8.1 boards/Kconfig

增加：

```kconfig
select ARCH_HAVE_LEDS
```

目的：

```text
让 STM32H750B-DK 声明自己有标准 LED API。
```

### 8.2 stm32h750b-dk.h

把 LED GPIO 定义整理成：

```text
GPIO_LD1 -> PJ2
GPIO_LD2 -> PI13
```

并且默认输出高电平：

```text
GPIO_OUTPUT_SET
```

因为 active-low LED 在高电平时是熄灭的。

### 8.3 board.h

把逻辑 LED 数量改成：

```c
#define BOARD_NLEDS 2
```

并定义：

```c
#define BOARD_LED_GREEN BOARD_LED1
#define BOARD_LED_RED   BOARD_LED2
```

### 8.4 stm32_userleds.c

修正用户态 LED 控制逻辑：

```c
stm32_gpiowrite(g_ledcfg[led], !ledon);
```

以及：

```c
stm32_gpiowrite(g_ledcfg[i], (ledset & (1 << i)) == 0);
```

这让：

```text
board_userled(..., true)  -> 点亮
board_userled(..., false) -> 熄灭
```

语义恢复正常。

### 8.5 stm32_autoleds.c

虽然当前我们关闭了：

```text
# CONFIG_ARCH_LEDS is not set
```

所以 `stm32_autoleds.c` 当前不会参与编译。

但 BSP PR 应该把它也修掉，否则以后别人打开 `CONFIG_ARCH_LEDS` 又会遇到 active-high/active-low 反向问题。

## 9. 从空白工程复现一遍

下面是你以后从零复现的流程。

### 9.1 确认当前目录

```bash
cd /home/debian19y/openvela/contest2026_004_TeamFalcons
pwd
```

应该看到：

```text
/home/debian19y/openvela/contest2026_004_TeamFalcons
```

### 9.2 确认 app 被软链进 packages

```bash
ls -l /home/debian19y/openvela/packages/demos | grep contest2026_004
```

应该看到类似：

```text
contest2026_004_hello_app -> ../../contest2026_004_TeamFalcons/app/hello_app
```

如果没有这个软链，说明 manifest/linkfile 没生效，编译树看不到你的 app。

### 9.3 配置板子

```bash
cd /home/debian19y/openvela/nuttx
./tools/configure.sh stm32h750b-dk:lvgl
```

如果输出：

```text
No configuration change.
```

这不是错误，只是说明当前已经是这个配置。

### 9.4 设置构建环境变量

如果你直接 `make menuconfig`，可能会遇到：

```text
arm-none-eabi-gcc: command not found
kconfig-mconf: command not found
```

原因是 openvela 自带 prebuilts 没进 `PATH`。

手动设置：

```bash
export OPENVELA_DIR=/home/debian19y/openvela
export PATH="$OPENVELA_DIR/prebuilts/tools/python/bin:$OPENVELA_DIR/prebuilts/gcc/linux-x86_64/arm-none-eabi/bin:$OPENVELA_DIR/prebuilts/build-tools/linux-x86_64/bin:$PATH"
export PYTHONPATH="$OPENVELA_DIR/prebuilts/tools/python/dist-packages/kconfiglib:$OPENVELA_DIR/prebuilts/tools/python/dist-packages:${PYTHONPATH:-}"
```

之后再执行：

```bash
make menuconfig
```

### 9.5 在 menuconfig 里确认关键配置

进入 `make menuconfig` 后，不建议一层层找菜单。

更稳的方式是按 `/` 搜索符号。

需要确认：

```text
ARCH_HAVE_LEDS = y
ARCH_LEDS = n
USERLED = n
LVX_USE_DEMO_CONTEST2026_004_HELLO_APP = y
INIT_ENTRYPOINT = first_led_main
INIT_ENTRYNAME = first_led
```

最终 `.config` 应该包含：

```text
CONFIG_ARCH_HAVE_LEDS=y
# CONFIG_ARCH_LEDS is not set
# CONFIG_USERLED is not set
CONFIG_LVX_USE_DEMO_CONTEST2026_004_HELLO_APP=y
CONFIG_INIT_ENTRYPOINT="first_led_main"
CONFIG_INIT_ENTRYNAME="first_led"
```

### 9.6 编译

```bash
cd /home/debian19y/openvela/nuttx
make -j$(nproc)
```

成功时会看到：

```text
LD: nuttx
CP: nuttx.hex
CP: nuttx.bin
```

最终固件在：

```text
/home/debian19y/openvela/nuttx/nuttx.hex
/home/debian19y/openvela/nuttx/nuttx.bin
```

### 9.7 烧录

我们写了一个脚本：

```text
scripts/flash_openocd.sh
```

它做两件事：

```text
1. 先编译。
2. 编译成功后，再烧录 nuttx.hex。
```

执行：

```bash
cd /home/debian19y/openvela/contest2026_004_TeamFalcons
./scripts/flash_openocd.sh
```

脚本内部固定烧：

```text
/home/debian19y/openvela/nuttx/nuttx.hex
```

使用的是 Windows 侧 OpenOCD：

```text
/mnt/d/Develop/xpack-openocd-0.12.0-7/bin/openocd.exe
```

并把 hex 复制到 Windows 临时目录：

```text
C:/Users/19y/AppData/Local/Temp/openvela_nuttx.hex
```

这是为了避免 Windows OpenOCD 处理 WSL 路径时踩坑。

## 10. VSCode 自动补全为什么一开始不工作

你是通过 VSCode WSL 插件打开目录的。

自动补全和跳转依赖几个东西：

```text
1. C/C++ 扩展或 clangd。
2. 正确的 includePath。
3. 最好有 compile_commands.json。
4. openvela configure/build 后生成的软链接和配置头文件。
```

如果没有先 configure/build，很多文件不存在或者路径没展开：

```text
include/arch
include/arch/board
include/arch/chip
generated config
```

那么 VSCode 不知道 `<nuttx/board.h>`、`<arch/board/board.h>` 从哪里来。

我们仓里后来补了 `.vscode` 配置，例如：

```text
.vscode/c_cpp_properties.json
.vscode/compile_commands.json
.vscode/settings.json
.vscode/extensions.json
```

但你要记住：

```text
IDE 补全不是编译本身。
编译通过才是第一标准。
补全依赖编译配置。
```

## 11. 常见错误和判断方法

### 11.1 arm-none-eabi-gcc 找不到

现象：

```text
arm-none-eabi-gcc: command not found
```

原因：

```text
交叉编译器没进 PATH。
```

修法：

```bash
export OPENVELA_DIR=/home/debian19y/openvela
export PATH="$OPENVELA_DIR/prebuilts/gcc/linux-x86_64/arm-none-eabi/bin:$PATH"
```

我们的烧录脚本已经自动做了这件事。

### 11.2 kconfig-mconf 找不到

现象：

```text
kconfig-mconf: command not found
```

原因：

```text
Kconfig menu 工具没进 PATH。
```

修法：

```bash
export PATH="/home/debian19y/openvela/prebuilts/build-tools/linux-x86_64/bin:$PATH"
```

### 11.3 Kconfig 一堆 syntax error

如果看到很多：

```text
unknown option "--help--"
unknown option "osource"
syntax error
```

通常说明：

```text
你用了系统里的普通 kconfig 工具，
而不是 openvela prebuilts 里的工具。
```

先检查：

```bash
which kconfig-mconf
```

应该指向：

```text
/home/debian19y/openvela/prebuilts/build-tools/linux-x86_64/bin/kconfig-mconf
```

### 11.4 编译通过但 LED 不对

不要先怀疑烧录。

按这个顺序查：

```text
1. app 里控制的是哪个逻辑 LED？
2. BOARD_LED_GREEN 在 board.h 里等于几？
3. stm32_userleds.c 的数组 index 映射到哪个 GPIO？
4. 这个 GPIO 对应开发板上哪颗 LED？
5. LED 是 active-high 还是 active-low？
6. CONFIG_ARCH_LEDS 是否打开导致内核抢 LED？
7. CONFIG_USERLED 是否打开导致 /dev/userleds 也参与？
```

这就是我们排查出 `LD6 闪烁、LD7 熄灭、LD8 常亮` 的方法。

### 11.5 烧录成功但现象没变

检查：

```text
1. 你烧的是不是 /home/debian19y/openvela/nuttx/nuttx.hex？
2. 编译是否真的重新生成了 nuttx.hex？
3. OpenOCD 是否连接到当前这块板？
4. reset 后程序是否真的从 flash 启动？
5. app 是否作为 init entry 自动运行？
```

可以确认 `.config`：

```bash
grep -n "INIT_ENTRY\\|CONTEST2026_004" /home/debian19y/openvela/nuttx/.config
```

期望：

```text
CONFIG_INIT_ENTRYPOINT="first_led_main"
CONFIG_INIT_ENTRYNAME="first_led"
CONFIG_LVX_USE_DEMO_CONTEST2026_004_HELLO_APP=y
```

## 12. 你已有经验如何迁移

### 12.1 STM32HAL 经验

你已经熟悉：

```text
GPIO 初始化
GPIO 输出高低电平
active-high / active-low
HAL_GPIO_WritePin()
```

迁移到 NuttX 时：

```text
GPIO 初始化逻辑放 BSP。
极性放 BSP。
应用不直接碰 GPIO。
应用调用 board_userled()。
```

### 12.2 FreeRTOS 经验

你熟悉：

```text
task
vTaskDelay()
优先级
栈大小
```

迁移到 NuttX 时：

```text
NuttX app 可以像 POSIX 程序一样写 main()。
delay 可以先用 usleep()。
栈大小在 Makefile 或 Kconfig 里配置。
```

例如：

```make
STACKSIZE = 2048
```

类似 FreeRTOS task stack size。

### 12.3 LwIP 经验

你熟悉 socket、IP、网卡初始化。

NuttX 里网络更偏 POSIX 风格：

```text
socket()
bind()
listen()
send()
recv()
```

底层网卡和协议栈由 Kconfig 和驱动控制。

### 12.4 FatFs 经验

你熟悉：

```text
f_open()
f_read()
f_write()
```

NuttX 里更常见的是：

```text
open()
read()
write()
close()
mount()
```

文件系统通过 Kconfig 开关和驱动挂载。

### 12.5 Modbus 经验

你熟悉串口、帧、超时、CRC。

NuttX 里串口一般走设备文件：

```text
/dev/ttyS0
/dev/ttyS1
```

应用层用：

```c
open()
read()
write()
ioctl()
```

你的 Modbus 经验会很有用，只是底层驱动入口从 HAL UART 变成了 POSIX device file。

## 13. 你现在最应该掌握的五个能力

### 13.1 会看 Kconfig

你要知道：

```text
某个功能有没有编进去？
某个宏在哪里定义？
某个选项由谁 select？
```

常用方法：

```bash
grep -RIn "CONFIG_ARCH_HAVE_LEDS" /home/debian19y/openvela/nuttx
grep -RIn "LVX_USE_DEMO_CONTEST2026_004_HELLO_APP" /home/debian19y/openvela
```

如果机器上没有 `rg`，用 `grep -RIn`。

### 13.2 会看 .config

`.config` 是当前构建结果的开关集合。

查 LED：

```bash
grep -n "ARCH_HAVE_LEDS\\|ARCH_LEDS\\|USERLED" /home/debian19y/openvela/nuttx/.config
```

查 app：

```bash
grep -n "CONTEST2026_004\\|INIT_ENTRY" /home/debian19y/openvela/nuttx/.config
```

### 13.3 会顺着调用链找代码

从 app 往底层找：

```text
hello_app_main.c
  -> board_userled()
  -> include/nuttx/board.h
  -> boards/arm/stm32h7/stm32h750b-dk/src/stm32_userleds.c
  -> stm32_gpiowrite()
```

这和你在 HAL 工程里从业务代码追到 HAL driver 是一个思路。

### 13.4 会判断所有权

硬件资源最怕多个 owner。

LED 这里的 owner 可能有：

```text
你的 app
NuttX auto LED
/dev/userleds
bootloader 或调试器遗留状态
```

我们要的状态是：

```text
只有你的 app 通过 board_userled() 控制 LED。
```

### 13.5 会分清应用修复和 BSP 修复

应用问题：

```text
app 写错 LED 名字。
app 没启用 Kconfig。
app 没作为 init entry。
```

BSP 问题：

```text
GPIO 映射错。
LED 极性错。
board.h 注释和实际硬件不一致。
Kconfig 没 select ARCH_HAVE_LEDS。
```

比赛提交时，这两类修复的归属不一样。

## 14. 比赛规范下该怎么提交

你的应用仓可以提交：

```text
app/hello_app/
scripts/flash_openocd.sh
docs/openvela_stm32h750b_dk_first_led_babysitter_guide.md
logs/
README.md
.vscode/ 如果你想提交 IDE 配置
```

你的应用仓不应该正式提交：

```text
../nuttx/boards/Kconfig
../nuttx/boards/arm/stm32h7/stm32h750b-dk/...
../nuttx/drivers/...
```

这些属于公共仓。

如果要正式修 BSP，应该：

```text
1. fork 公共 nuttx 仓。
2. 切到 dev-ai-contest-2026 分支。
3. 提交 STM32H750B-DK LED BSP 修复。
4. 发 PR 给公共 nuttx 仓。
5. 在你的应用仓 README 里说明依赖这个 BSP PR。
```

这样才符合比赛规范。

## 15. 建议你做的练习

### 15.1 改闪烁频率

把：

```c
usleep(500 * 1000);
```

改成：

```c
usleep(100 * 1000);
```

观察 LED 是否快速闪烁。

你会学到：

```text
应用改动 -> 编译 -> 烧录 -> 现象验证
```

### 15.2 改闪第二颗 LED

把：

```c
#define FIRST_LED BOARD_LED_GREEN
```

改成：

```c
#define FIRST_LED BOARD_LED_RED
```

观察第二颗 user LED。

你会学到：

```text
应用只改逻辑 LED，不改 GPIO。
```

### 15.3 打印 LED 数量

加一行：

```c
printf("first_led: board reports %lu LEDs\n", (unsigned long)nleds);
```

你会学到：

```text
board_userled_initialize() 返回 BSP 暴露的 LED 数量。
```

### 15.4 用 board_userled_all()

尝试：

```c
board_userled_all(BOARD_LED1_BIT | BOARD_LED2_BIT);
usleep(500 * 1000);
board_userled_all(0);
usleep(500 * 1000);
```

你会学到：

```text
单灯控制和批量控制的区别。
```

## 16. 以后遇到硬件现象不对时的排查模板

把下面这段当成 checklist：

```text
1. 先确认当前烧录的是最新 hex。
2. 确认 app 是否真的编进系统。
3. 确认 app 是否真的作为 init entry 运行。
4. 确认 Kconfig 没有别的 owner 抢硬件。
5. 确认应用用的是逻辑名字，不是硬编码 index。
6. 确认 BSP 的 board.h 逻辑名字和数组一致。
7. 确认 BSP 的 GPIO 映射和原理图/ST BSP 一致。
8. 确认 active-high / active-low 极性。
9. 确认初始化默认态不会误点亮。
10. 最后再怀疑调试器、烧录器、硬件连接。
```

这次我们就是按这条思路找到问题的。

## 17. 这一路我们具体做了什么

按时间顺序总结：

```text
1. 确认比赛目录规则，明确应用代码应该放在 contest 仓。
2. 确认 manifest/linkfile 会把 app 软链到 packages/demos。
3. 把 team 000 模板改成 team 004。
4. 把应用名改成 first_led。
5. 配置 .config，让 first_led app 编入系统并作为启动入口。
6. 解决 arm-none-eabi-gcc 找不到的问题。
7. 解决 kconfig-mconf 找不到和 Kconfig 解析工具不对的问题。
8. 写第一版 board_userled() blink app。
9. 发现 warning，定位到 CONFIG_ARCH_HAVE_LEDS 没打开。
10. 解释为什么不应该在 app 里手动声明 BSP 函数。
11. 打开 CONFIG_ARCH_HAVE_LEDS。
12. 确认 CONFIG_ARCH_LEDS 应该关闭，避免内核抢 LED。
13. 确认 CONFIG_USERLED 应该关闭，避免 /dev/userleds 参与。
14. 写 build + flash 脚本，固定编译后烧录 nuttx.hex。
15. 用 OpenOCD 烧录并观察真实硬件现象。
16. 发现 LED 极性相反，定位到 active-low。
17. 修 board_userled()，让 true 表示亮，false 表示灭。
18. 发现 LD6/LD7/LD8 现象仍不符合预期。
19. 继续 review BSP LED 映射，发现 PI13/PJ2/PD3 混在一个 LED 表里。
20. 按 STM32H750B-DK BSP 语义整理为 PJ2 和 PI13 两个 user LED。
21. app 改为使用 BOARD_LED_GREEN，而不是硬编码 index。
22. 编译验证通过。
```

## 18. 你现在应该记住的核心原则

第一条：

```text
应用层不要直接写 STM32 寄存器，也不要直接写具体 GPIO。
```

第二条：

```text
应用层表达意图，BSP 层处理硬件细节。
```

第三条：

```text
CONFIG_ARCH_HAVE_LEDS 是能力声明。
CONFIG_ARCH_LEDS 是内核接管。
CONFIG_USERLED 是字符设备接管。
```

第四条：

```text
LED 极性必须在 BSP 修，不应该在 app 里到处取反。
```

第五条：

```text
比赛应用仓和公共 nuttx 仓要分开提交。
```

如果你把这五条吃透，后面从 LED 扩展到按键、串口、I2C、SPI、屏幕、文件系统，思路都是一样的。

