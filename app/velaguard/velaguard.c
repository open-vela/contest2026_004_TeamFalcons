/****************************************************************************
 * Contest 2026 team 004 - velaguard_app
 *
 * 单一形态：
 *   velaguard_app_main 是系统入口（CONFIG_INIT_ENTRYPOINT），主线程常驻
 *   运行主循环（LED 演示，后续挂 LVGL 等业务），同时拉一个 NSH 线程
 *   （nsh_initialize + nsh_consolemain）提供 shell，供调试 vgpwm/vgrs485/
 *   vgesp 等小工具。板级初始化由 NSH 线程内的 NSH_ARCHINIT 完成一次。
 *
 *   若开启 CONFIG_VG_NET_FAILOVER：在 NSH 之后调用 vg_net_mgr_start()，
 *   由独立线程做 RJ45/ESP TCP 故障转移；不依赖敲 NSH。
 *
 * velaguard.c 的 main 经 Makefile -Dmain 重命名为 velaguard_app_main，
 * 并注册为 NSH 命令 velaguard_app；若在 shell 里重复启动，防重护栏
 * 直接退出，不会拉起第二个实例。
 ****************************************************************************/

#include <stdio.h>
#include <nuttx/nuttx.h>
#include <nuttx/board.h>
#include <unistd.h>

#include <nshlib/nshlib.h>
#include <pthread.h>

#ifdef CONFIG_VG_NET_FAILOVER
#include "vg_net_mgr.h"
#endif

static int g_app_running = 0;

/**
  * @brief  NSH 线程入口：初始化后进入 nsh_consolemain REPL（正常不返回）。
  * @note   签名适配 pthread；nsh_consolemain 本身是任务式 main(argc,argv)。
  * @param  arg  未使用。
  * @retval NULL（正常路径不返回）。
  */
static void *nsh_thread(void *arg)
{
    (void)arg;

    /* Board bring-up for tools lives under NSH_ARCHINIT once */

    nsh_initialize();
    nsh_consolemain(0, NULL);
    return NULL;
}

/**
  * @brief  系统入口 / NSH 命令 velaguard_app。
  * @note   防重入；拉起 NSH 与（可选）net_mgr 后进入 LED 主循环。
  *         CONFIG_VG_NET_FAILOVER 时自动 vg_net_mgr_start()，不依赖敲 NSH。
  * @param  argc  参数个数（入口/命令共用）。
  * @param  argv  参数向量。
  * @retval 0  防重入早退；主循环正常不返回。
  */
int main(int argc, char *argv[])
{
    if (g_app_running)
    {
        /* 入口已常驻运行（velaguard_app_main），shell 里再敲
         * velaguard_app 不重复拉起，避免第二个 LED 循环 + 嵌套 NSH。
         */

        printf("velaguard_app: already running as system entrypoint\n");
        return 0;
    }

    g_app_running = 1;

    /* Spawn console shell on a side thread */

    pthread_t tid;
    pthread_create(&tid, NULL, nsh_thread, NULL);

#ifdef CONFIG_VG_NET_FAILOVER
    /* Start RJ45/ESP TCP failover manager (independent of NSH) */

    if (vg_net_mgr_start() != 0)
      {
        printf("vgnet: failover manager not started\n");
      }
#endif

    uint8_t lednum = board_userled_initialize();
    printf("LED num: %d\r\n", lednum);
    printf("Hello from openvela contest 2026 team 004!\n");

    /* Forever LED heartbeat; local Modbus/collection stays independent */

    for (;;)
    {
        usleep(1000000);
        board_userled(1, true);
        usleep(1000000);
        board_userled(1, false);
    }

    return 0;
}
