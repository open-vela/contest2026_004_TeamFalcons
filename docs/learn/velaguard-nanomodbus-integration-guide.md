# 把 nanoMODBUS 接进 VelaGuard：从零到 RTU 主站跑通

> 面向初学者的垂直教程。目标：在 `app/velaguard` 里接入
> [nanoMODBUS](https://github.com/debevv/nanoMODBUS)（MIT，约 2000 行 C 的
> Modbus RTU/TCP 协议栈），用 `modbus_port_openvela.c` 做串口适配层，再写一个
> `modbus_collector`（NSH 命令 `vgmodbus`）实现 Modbus RTU 主站，能周期读取
> 一个从站的保持寄存器。
>
> 本文不建 Trellis 任务，也不改任何生产仓——所有代码都落在参赛仓自己的
> `app/velaguard/` 目录里。每一步都说明**为什么存在**、**接口是什么**、
> **怎么验证**。

## 0. 先看全貌

### 0.1 现在工程里已经有什么

- 串口基础：`/dev/rs485` 已存在（STM32H750B-DK 板级代码在
  `CONFIG_UART7_RS485` 下把 `/dev/ttyS2` 软链成 `/dev/rs485`）；
  `velaguard-net` 预设里 UART7 是 9600 8N1，DE/RE 方向由 NuttX RS485 驱动自动
  切换（`CONFIG_UART7_RS485` + `CONFIG_UART7_RS485_DIR_POLARITY`）。
- 最小读写范例：[`vgrs485`](../../app/velaguard/velaguard_rs485.c) 已经演示了
  `open("/dev/rs485")` → `write()` → `tcdrain()` → `read()` 这一套，说明串口
  链路是通的，缺的只是"上面那层 Modbus 协议"。
- 方案文档 [`VelaGuard_推进方案.md`](../../VelaGuard_推进方案.md) 5.2 节已经
  写明阶段 2 目标：**基于 nanoMODBUS 实现 Modbus RTU 主站**、编写
  `modbus_port_openvela.c` 适配层、用 NuttX 串口字符设备实现 read/write 回调、
  支持 Holding Register / Input Register、失败重试、离线判定。

### 0.2 为什么选 nanoMODBUS，而不是 NuttX 自带的 FreeModbus

NuttX 里其实有一套 `apps/modbus`（FreeModbus）+ `apps/examples/modbusmaster`。
但 nanoMODBUS 更适合这个项目：

| 维度 | nanoMODBUS | NuttX 自带 FreeModbus 移植 |
|---|---|---|
| 体量 | 1 个 `.c` + 1 个 `.h`，约 2000 行 | 多文件、带 lib 层和示例 |
| 依赖 | 只有 C99 + 标准库 | 绑定 NuttX 任务/信号量等 |
| 接入成本 | 只需实现 read/write 两个传输回调 | 需要按它规定的 port 层改配置 |
| 裁剪 | 可 `-DNMBS_SERVER_DISABLED` 只留 client | 裁剪粒度粗 |

而且方案文档的风险表里明确写了应对：**只补 transport 回调和串口配置，不改
nanoMODBUS 核心**。

### 0.3 改动清单（预期 5 个文件）

| 交付物 | 位置 | 作用 |
|---|---|---|
| 第三方源码 | `app/velaguard/nanomodbus/`（nanomodbus.c/.h/LICENSE） | 协议栈本体，固定 tag，不修改 |
| 适配层 | `app/velaguard/modbus_port_openvela.c` | 把 `/dev/rs485` 变成 nanoMODBUS 的 read/write/flush |
| 采集命令 | `app/velaguard/modbus_collector.c` | `vgmodbus`：RTU 主站轮询 + 错误计数 |
| 构建挂载 | `app/velaguard/Makefile` + `CMakeLists.txt` | 让 vgmodbus 进固件、进 NSH 命令表 |
| 本文 | `docs/learn/` | 步骤 + 验证 + 踩坑 |

## 1. 拿源码：下载并 vendoring

nanoMODBUS 官方安装方式就是"把 `nanomodbus.c` 和 `nanomodbus.h` 拷进你的代码
库"。比赛规则要求生产仓零改动、只在自己的仓里开发，所以放：

```text
app/velaguard/nanomodbus/
├── nanomodbus.c
├── nanomodbus.h
└── LICENSE        # MIT 许可证，务必保留版权声明
```

**固定一个 tag**，不要追 master。当前最新稳定版是 **v1.23.0**：

```bash
# 在 app/velaguard/ 下
mkdir -p nanomodbus
curl -L -o nanomodbus/nanomodbus.c \
  https://raw.githubusercontent.com/debevv/nanoMODBUS/v1.23.0/nanomodbus.c
curl -L -o nanomodbus/nanomodbus.h \
  https://raw.githubusercontent.com/debevv/nanoMODBUS/v1.23.0/nanomodbus.h
curl -L -o nanomodbus/LICENSE \
  https://raw.githubusercontent.com/debevv/nanoMODBUS/v1.23.0/LICENSE
```

> 为什么不用 CMake 的 `FetchContent`？那会引入对网络的构建期依赖，也不符合
> "参赛仓自带全部代码"的要求。vendoring 后构建完全离线、可复现。
>
> API 以 v1.23.0 为准：`nmbs_client_create` / `nmbs_read_holding_registers` /
> `nmbs_set_destination_rtu_address`。旧文章里可能看到的 `nmbs_create` /
> `nmbs_make_request_rtu` 是更早版本的 API，别混用。

## 2. 写适配层：`modbus_port_openvela.c`

nanoMODBUS 只要求你实现 `nmbs_platform_conf` 里的两个传输回调（`crc_calc`、
`flush` 可选）。核心契约（读 v1.23.0 的 `nanomodbus.h` 注释）：

- 阻塞到**收满/发完 `count` 字节**，或超过 `byte_timeout_ms` 到期；
- `byte_timeout_ms < 0` 表示无限等待；`== 0` 表示非阻塞地读/写一次立即返回；
- 返回值是实际传输的字节数，`< 0` 表示传输错误；返回 `[0, count-1]` 会被
  nanoMODBUS 当成传输侧超时。

因为 `/dev/rs485` 是 NuttX 的 RS485 驱动，**DE/RE 方向控制已经由驱动处理**，
适配层不用碰 GPIO。如果将来换裸 UART + 外部 DE 引脚，才需要你在 `write` 前
拉高 DE、`tcdrain()` 后再拉低。

```c
/* modbus_port_openvela.c —— 只做串口传输，不含任何 Modbus 逻辑 */
#include <nuttx/config.h>
#include <poll.h>
#include <time.h>
#include <errno.h>
#include <unistd.h>
#include "nanomodbus/nanomodbus.h"

typedef struct { int fd; } port_t;

static int32_t now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

/* read：收满 count 字节，或 byte_timeout_ms 到期返回已收字节数 */
static int32_t port_read(uint8_t *buf, uint16_t count,
                         int32_t byte_timeout_ms, void *arg)
{
    port_t *p = arg;
    int32_t deadline = byte_timeout_ms < 0 ? INT32_MAX
                                           : now_ms() + byte_timeout_ms;
    uint16_t got = 0;

    while (got < count)
    {
        int32_t remain = deadline - now_ms();
        if (remain < 0)
            return got;                       /* 超时：返回部分字节 */

        struct pollfd fds = { .fd = p->fd, .events = POLLIN };
        int r = poll(&fds, 1, remain);
        if (r == 0)
            return got;                       /* 超时 */
        if (r < 0)
            return -1;                        /* 传输错误 */

        ssize_t n = read(p->fd, buf + got, count - got);
        if (n > 0)
            got += n;
        else if (n < 0 && errno == EAGAIN)
            continue;
        else
            return -1;
    }
    return got;
}

/* write：发完所有字节；tcdrain 等物理发完再返回（RS485 切向安全） */
static int32_t port_write(const uint8_t *buf, uint16_t count,
                          int32_t byte_timeout_ms, void *arg)
{
    port_t *p = arg;
    uint16_t sent = 0;

    while (sent < count)
    {
        ssize_t n = write(p->fd, buf + sent, count - sent);
        if (n > 0)
            sent += n;
        else if (n < 0 && errno == EAGAIN)
            continue;
        else
            return -1;
    }
    tcdrain(p->fd);
    return sent;
}

/* 可选 flush：nanoMODBUS 在发请求前会调它清掉线路上残留的脏字节 */
static void port_flush(nmbs_t *nmbs, void *arg)
{
    port_t *p = arg;
    struct pollfd fds = { .fd = p->fd, .events = POLLIN };
    char tmp[32];

    while (poll(&fds, 1, 0) > 0)
    {
        if (read(p->fd, tmp, sizeof(tmp)) <= 0)
            break;
    }
}
```

两个容易被忽略的点：

1. **超时语义要精确**。nanoMODBUS 会分多次调用 read（帧头、PDU、CRC 各一段），
   每次都希望"要么收满这段、要么按时超时"。返回部分字节会被它当成超时并
   丢弃整帧，所以不要一次只 `read()` 一次就返回，必须循环凑满 `count`。
2. **打开设备后设 raw 模式**。在采集命令里 open 之后用 `termios` 做
   `cfmakeraw()` + 波特率/数据位/校验位（当前 UART7 是 9600 8N1），避免
   换行符处理或 canonical 模式干扰二进制帧。

## 3. 写采集命令：`modbus_collector.c`

最小 RTU 主站只需要：创建 client → 设置从站地址 → 发请求 → 处理错误。
"失败计数 → 离线判定"正好对得上阶段 2 的验收标准（断开 RS485 后产生离线
告警）。

```c
/* modbus_collector.c —— vgmodbus：RTU 主站轮询一个从站的保持寄存器 */
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include "nanomodbus/nanomodbus.h"

extern int32_t port_read(uint8_t *, uint16_t, int32_t, void *);
extern int32_t port_write(const uint8_t *, uint16_t, int32_t, void *);
extern void port_flush(nmbs_t *, void *);

int main(int argc, char *argv[])
{
    int fd = open("/dev/rs485", O_RDWR);
    if (fd < 0)
    {
        perror("open /dev/rs485");
        return 1;
    }

    /* 串口设 raw 模式；波特率 9600 与 defconfig 保持一致 */
    struct termios tio;
    tcgetattr(fd, &tio);
    cfmakeraw(&tio);
    cfsetispeed(&tio, B9600);
    cfsetospeed(&tio, B9600);
    tcsetattr(fd, TCSANOW, &tio);

    port_t port = { .fd = fd };
    nmbs_platform_conf pc;
    nmbs_platform_conf_create(&pc);
    pc.transport = NMBS_TRANSPORT_RTU;
    pc.read  = port_read;
    pc.write = port_write;
    pc.flush = port_flush;
    pc.arg   = &port;

    nmbs_t nmbs;
    if (nmbs_client_create(&nmbs, &pc) != NMBS_ERROR_NONE)
        return 1;
    nmbs_set_read_timeout(&nmbs, 500);   /* 整帧响应超时 */
    nmbs_set_byte_timeout(&nmbs, 20);    /* 字节间超时 */

    uint8_t addr = 1;                    /* 从站地址，可做成 argv 参数 */
    uint32_t fails = 0;

    for (;;)
    {
        uint16_t regs[4] = {0};

        nmbs_set_destination_rtu_address(&nmbs, addr);
        nmbs_error err = nmbs_read_holding_registers(&nmbs, 0, 4, regs);

        if (err == NMBS_ERROR_NONE)
        {
            fails = 0;
            printf("addr=%u: reg0=%u reg1=%u reg2=%u reg3=%u\n",
                   addr, regs[0], regs[1], regs[2], regs[3]);
        }
        else
        {
            fails++;
            printf("read failed: %s (fails=%lu)\n",
                   nmbs_strerror(err), (unsigned long)fails);
            /* fails >= N 就进入 offline/degraded 状态 */
        }

        sleep(1);
    }
}
```

要点：

- `nmbs_set_destination_rtu_address()` 是**按请求设置**的。多从站轮询就是每帧
  前换地址，其余代码不用改。
- 返回错误先看 `nmbs_strerror()`：`NMBS_ERROR_TIMEOUT` 说明帧没回来
  （线路/地址/波特率问题），Modbus 异常（返回值为正）说明设备收到了但拒绝
  （寄存器地址越界等）。
- 把 `fails` 阈值和"最后一次成功时间"做成状态机，就是阶段 2 的
  `online / degraded / offline / recovering` 四态来源。

## 4. 挂进构建系统

`app/velaguard/` 目前是 Makefile（APPDIR 构建）和 CMake（nuttx_add_application）
双构建支持，**两处都要改并保持一致**。

### 4.1 Makefile

`PROGNAME` 和 `MAINSRC` 按位置一一对应（第 i 个 PROGNAME 对应第 i 个 MAINSRC
的 main）；公共源码用 `CSRCS`：

```make
PROGNAME  = velaguard_app vgpwm vgrs485 vgesp vgmqtt vgmodbus
PRIORITY  = SCHED_PRIORITY_DEFAULT
STACKSIZE = 4096
MODULE    = $(CONFIG_VG_BRINGUP_TOOLS)

MAINSRC = velaguard.c velaguard_pwm.c velaguard_rs485.c velaguard_esp.c \
          velaguard_mqtt.c modbus_collector.c
CSRCS   = modbus_port_openvela.c nanomodbus/nanomodbus.c
```

`CSRCS` 会被链接进本目录所有程序。demo 阶段无所谓；如果不想让 vgpwm 等工具
也带上 nanoMODBUS，就把 modbus 相关文件放进独立的子目录
`app/velaguard/modbus/`（自带 Make.defs/Kconfig），那是更干净的做法。

### 4.2 CMakeLists.txt

```cmake
nuttx_add_application(
  NAME vgmodbus
  SRCS modbus_collector.c modbus_port_openvela.c nanomodbus/nanomodbus.c
  STACKSIZE 4096)
```

### 4.3 可选：裁掉 server 代码

只需要 client，可以在 CFLAGS 加 `-DNMBS_SERVER_DISABLED` 把 server 部分裁掉，
省 flash/ram。CMake 里对应 `nuttx_add_application` 的编译选项参数，Makefile
里加 `CFLAGS += -DNMBS_SERVER_DISABLED`（注意别影响同目录其他程序）。

## 5. 配置、编译、烧录

`velaguard-net` 预设里 `CONFIG_VG_BRINGUP_TOOLS=y` 已经开着，理论上 make 构建
会自动带上 `vgmodbus`。稳妥起见走一遍菜单确认：

```bash
cd ..   # openvela 工作区根目录（你的仓的上一级）
./build.sh nuttx/boards/arm/stm32h7/stm32h750b-dk/configs/velaguard-net menuconfig
# 确认 VG_BRINGUP_TOOLS=y，退出保存
./build.sh nuttx/boards/arm/stm32h7/stm32h750b-dk/configs/velaguard-net -j8
```

烧录后进 NSH：

1. `vgrs485 tx /dev/rs485` + 对端回 `vgrs485 rx`，确认串口链路本身没问题；
2. `help | grep vgmodbus` 确认命令已注册；
3. 连上从站后 `vgmodbus` 开跑。

## 6. 验证：没有真实从站也能测

| 方案 | 做法 | 适合验证 |
|---|---|---|
| PC 当从站 | 板子 UART7 接 USB 转 RS485，PC 跑 `diagslave -m rtu -b 9600 -d 8 -p none -a 1`，板端 `vgmodbus` | 主站收帧、寄存器解析 |
| PC 当主站 | 反过来用 `mbpoll`/`modpoll` 读板子（板端先做 nanoMODBUS server） | 板端 server 模式 |
| 板间互测 | 另一块板用 `nmbs_server_create` + `nmbs_server_poll` 当从站 | 双板联调 |
| 拔线测试 | 轮询过程中拔掉 RS485 | `fails` 增长 → 离线判定输入 |

注意 PC 侧模拟器的串口参数必须和板端一致：**9600、8 数据位、无校验、1 停止位**
（Modbus 默认 8E1 的情况要两边一起改）。

## 7. 往阶段 2 扩展的路线

`modbus_port_openvela.c` 保持"只做传输"的纯度，后续都在 `modbus_collector`（或
更上层的采集服务）里加：

1. 多从站轮询：每帧前换 `nmbs_set_destination_rtu_address`；
2. 寄存器倍率/单位/数据类型换算（方案里提到的 `倍率 0.1` 之类）；
3. 失败重试 + 离线/恢复状态机；
4. 采样结果进 `sensor_registry` / `rule_engine` / `event_store`。

这套分层和方案文档 5.2 的目标一一对应，且每一层都能单独验证。

## 8. 阅读清单

- nanoMODBUS 仓库与 API：[debevv/nanoMODBUS](https://github.com/debevv/nanoMODBUS)、
  [v1.23.0 的 nanomodbus.h](https://github.com/debevv/nanoMODBUS/blob/v1.23.0/nanomodbus.h)
- 本仓现有 RS485 基础：[velaguard_rs485.c](../../app/velaguard/velaguard_rs485.c)、
  `nuttx/boards/arm/stm32h7/stm32h750b-dk/src/stm32_bringup.c`（`/dev/rs485` 软链）
- 方案文档：[VelaGuard_推进方案.md](../../VelaGuard_推进方案.md) 第 5 节
- 构建入口：`../build.sh` 与 `docs/agents/BOUNDARY.md`（构建必须从 openvela 根目录执行）
