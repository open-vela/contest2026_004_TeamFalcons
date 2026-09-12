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
 *   若开启 CONFIG_VG_HMI_AUTOSTART：等待 /dev/fb0 后 task_create vghmi。
 *   若开启 CONFIG_VG_AGENT_AUTOSTART：在 HMI 之后拉起 ai_agent --daemon，
 *   心跳/日报无需手动启动；ai_agent（不带参数）只附着交互 CLI。
 *
 * velaguard.c 的 main 经 Makefile -Dmain 重命名为 velaguard_app_main，
 * 并注册为 NSH 命令 velaguard_app；若在 shell 里重复启动，防重护栏
 * 直接退出，不会拉起第二个实例。
 ****************************************************************************/

#include <stdio.h>
#include <nuttx/config.h>
#include <nuttx/nuttx.h>
#include <nuttx/board.h>
#include <sys/stat.h>
#include <unistd.h>

#include <nshlib/nshlib.h>
#include <pthread.h>

#if defined(CONFIG_VG_AGENT_AUTOSTART) || defined(CONFIG_VG_HMI_AUTOSTART)
#include <nuttx/sched.h>
#endif

#ifdef CONFIG_VG_AGENT_AUTOSTART
extern int ai_agent_main(int argc, char *argv[]);
#endif

#ifdef CONFIG_VG_HMI_AUTOSTART
extern int vghmi_main(int argc, char *argv[]);
#endif

#ifdef CONFIG_VG_NET_FAILOVER
#include "vg_net_mgr.h"
#endif

#ifdef CONFIG_VG_TIME_SYNC
#include "vg_time_sync.h"
#endif

#ifdef CONFIG_VG_HEAP_WATCH
#include "vg_heapwatch.h"
#endif

#ifdef CONFIG_EXAMPLES_AI_AGENT_VELA
#include "vg_agent_seed.h"
#include "vg_provision.h"
#endif

#ifdef CONFIG_VG_AGENT_OPS
#include "vg_agent_alarm.h"
#endif

#ifdef CONFIG_VG_CONFIG_STORE
#include "vg_config_store.h"
#ifndef CONFIG_VG_CONFIG_BASEDIR
#  define CONFIG_VG_CONFIG_BASEDIR "/data/velaguard/config"
#endif
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

#ifdef CONFIG_VG_TIME_SYNC
    /* Wall-time persist + SNTP sync (waits for eMMC mount internally) */

    vg_time_sync_start();
#endif

#ifdef CONFIG_VG_HEAP_WATCH
    vg_heapwatch_start();
#endif

#ifdef CONFIG_VG_CONFIG_STORE
    {
      struct vg_config cfg;
      struct stat st;
      int cfg_ret;
      int i;

      /* Board late-init mounts eMMC from the NSH thread; wait briefly. */

      for (i = 0; i < 50; i++)
        {
          if (stat("/data/velaguard/config", &st) == 0 ||
              stat("/data", &st) == 0)
            {
              break;
            }

          usleep(100000);
        }

      vg_config_set_basedir(CONFIG_VG_CONFIG_BASEDIR);
      cfg_ret = vg_config_load(&cfg);
      if (cfg_ret < 0)
        {
          printf("vgcfg: load error %d\n", cfg_ret);
        }
      else
        {
          printf("vgcfg: %s seq=%u name=%s\n",
                 (cfg_ret == 1) ? "FACTORY" : "OK",
                 (unsigned)cfg.seq,
                 cfg.device_name);
        }
    }
#endif

#ifdef CONFIG_EXAMPLES_AI_AGENT_VELA
    vg_agent_seed_content();
    vg_provision_boot_apply_if_needed();
#endif

#ifdef CONFIG_VG_AGENT_OPS
    vg_agent_alarm_start();
#endif

#ifdef CONFIG_VG_HMI_AUTOSTART
    {
      struct stat st;
      int i;
      int prio = 100;
      int stack = 49152;
      char *hmi_argv[] = { "vghmi", NULL };

#ifdef CONFIG_VG_HMI_PRIORITY
      prio = CONFIG_VG_HMI_PRIORITY;
#endif
#ifdef CONFIG_VG_HMI_STACKSIZE
      stack = CONFIG_VG_HMI_STACKSIZE;
#endif

      for (i = 0; i < 50; i++)
        {
          if (stat("/dev/fb0", &st) == 0)
            {
              break;
            }

          usleep(100000);
        }

      if (task_create("vghmi", prio, stack, vghmi_main, hmi_argv) < 0)
        {
          printf("vghmi: autostart failed\n");
        }
      else
        {
          printf("vghmi: autostart ok\n");
        }
    }
#endif

#ifdef CONFIG_VG_AGENT_AUTOSTART
    {
      char *ai_argv[] = { "ai_agent", "--daemon", NULL };
      int astack = 16384;

#ifdef CONFIG_EXAMPLES_AI_AGENT_VELA_STACKSIZE
      astack = CONFIG_EXAMPLES_AI_AGENT_VELA_STACKSIZE;
#endif

      /* UI task up first; settle eMMC/net before the TLS-heavy agent
       * (boot race → assert). The agent loop retries network on its own. */

      sleep(3);

      if (task_create("ai_agent", SCHED_PRIORITY_DEFAULT,
                      astack, ai_agent_main, ai_argv) < 0)
        {
          printf("vgagent: ai_agent autostart failed\n");
        }
      else
        {
          printf("vgagent: ai_agent autostart ok (stack=%d)\n", astack);
        }
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
