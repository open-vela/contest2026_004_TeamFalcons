/****************************************************************************

* app/velaguard/velaguard_pwm.c
*
* VelaGuard - vgpwm：通过 /dev/pwm0 驱动 DO1 无源蜂鸣器
*
* 用法：
* vgpwm [频率Hz] [占空比百分比] [持续时间ms]
* 默认值：2700 Hz，50%，3000 ms
*
* 这是一个最小化的用户空间 PWM 驱动测试：
* 打开设备 -> 设置 PWM 参数 -> 启动 -> 等待 -> 停止 -> 关闭设备
  ****************************************************************************/

#include <nuttx/config.h>

#include <sys/ioctl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#include <fixedmath.h>        /* b16HALF、uitoub16、b16divi */
#include <nuttx/timers/pwm.h> /* pwm_info_s、PWMIOC_* */

/****************************************************************************

* 公共函数
  ****************************************************************************/

int main(int argc, FAR char *argv[])
{
    struct pwm_info_s info;
    uint32_t hz = 2048;
    uint32_t pct = 50;
    uint32_t ms = 3000;
    int fd;
    int ret;

    /* 解析可选参数：vgpwm [频率Hz] [占空比百分比] [持续时间ms] */

    if (argc > 1)
    {
        hz = (uint32_t)strtoul(argv[1], NULL, 10);
    }

    if (argc > 2)
    {
        pct = (uint32_t)strtoul(argv[2], NULL, 10);
    }

    if (argc > 3)
    {
        ms = (uint32_t)strtoul(argv[3], NULL, 10);
    }

    /* 基本参数检查（具体的参数范围也会由 PWM 驱动进行检查） */

    if (hz == 0 || pct == 0 || pct > 99 || ms == 0)
    {
        fprintf(stderr,
                "vgpwm: bad arguments (hz>=1, 1<=pct<=99, ms>=1)\n");
        return 1;
    }

    printf("vgpwm: %lu Hz, %lu%%, %lu ms\n",
           (unsigned long)hz, (unsigned long)pct, (unsigned long)ms);

    /* 1. 打开 PWM 设备。
    *

    * 这里使用 O_RDONLY 是 NuttX PWM 驱动的常见方式：
    * PWM 驱动不通过 read()/write() 操作，
    * 所有控制操作都通过 ioctl() 完成。
      */

    fd = open("/dev/pwm0", O_RDONLY);
    if (fd < 0)
    {
        fprintf(stderr, "vgpwm: open /dev/pwm0 failed: %d\n", errno);
        return 1;
    }

    /* 2. 设置 PWM 输出参数。
    *

    * duty 使用 ub16 定点数表示，占用 16 位整数位和 16 位小数位，
    * 取值范围为 0x00000000 ~ 0x00010000。
    *
    * 例如：
    * ```
       0%   = 0x00000000
      ```
    * ```
       50%  = 0x00008000
      ```
    * ```
       100% = 0x00010000
      ```
    *
    * uitoub16(pct) 将百分比转换为 ub16 定点数，
    * 再通过 b16divi(..., 100) 除以 100，
    * 最终得到 PWM 驱动所需要的占空比格式。
      */

    info.frequency = hz;
    info.duty = b16divi(uitoub16(pct), 100);

    ret = ioctl(fd, PWMIOC_SETCHARACTERISTICS,
                (unsigned long)((uintptr_t)&info));
    if (ret < 0)
    {
        fprintf(stderr, "vgpwm: ioctl(PWMIOC_SETCHARACTERISTICS) failed: %d\n",
                errno);
        goto errout;
    }

    /* 3. 启动 PWM 输出。 */

    ret = ioctl(fd, PWMIOC_START, 0);
    if (ret < 0)
    {
        fprintf(stderr, "vgpwm: ioctl(PWMIOC_START) failed: %d\n", errno);
        goto errout;
    }

    /* 4. 持续输出 PWM 信号指定的时间，让蜂鸣器发声。 */

    usleep(ms * 1000);

    /* 5. 停止 PWM 输出并关闭设备。 */

    ret = ioctl(fd, PWMIOC_STOP, 0);
    if (ret < 0)
    {
        fprintf(stderr, "vgpwm: ioctl(PWMIOC_STOP) failed: %d\n", errno);
        goto errout;
    }

    close(fd);
    return 0;

errout:
    close(fd);
    return 1;
}
