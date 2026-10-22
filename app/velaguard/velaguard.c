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

/* The system entry task uses CONFIG_INIT_STACKSIZE, not the builtin app's
 * STACKSIZE. Boot now performs verified FAT writes when refreshing Skills;
 * the former 2 KiB init stack overflows inside open()/getumask(). */
#if defined(CONFIG_VG_HMI_AUTOSTART) && CONFIG_INIT_STACKSIZE < 8192
#  error "VelaGuard boot storage path requires CONFIG_INIT_STACKSIZE >= 8192"
#endif

/* The console thread runs the whole shell, so every filesystem command runs
 * on this stack: vgpoint, vgstoretest, cp/mv, and the vfat plus SDMMC read
 * path underneath them.  The pthread default (CONFIG_PTHREAD_STACK_DEFAULT,
 * 2 KiB here) is not enough.  Measured on the board: during a vfat rename the
 * stack reached the very bottom of its allocation, and NuttX keeps
 * struct tls_info_s there, so the overflow overwrote tls_info_s->tl_task.
 * The next getpid() dereferenced the wrecked pointer and the board took a
 * HardFault and stopped responding.  Keep this in step with the matching
 * CONFIG_VG_NSH_STACKSIZE range in Kconfig. */

#ifndef CONFIG_VG_NSH_STACKSIZE
#  define CONFIG_VG_NSH_STACKSIZE 8192
#endif

#if CONFIG_VG_NSH_STACKSIZE < 8192
#  error "VelaGuard console shell requires CONFIG_VG_NSH_STACKSIZE >= 8192"
#endif

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
#include "vg_agent_tools.h"
#include "vg_provision.h"
#endif

#ifdef CONFIG_VG_AGENT_OPS
#include "vg_agent_alarm.h"
#endif

#if defined(CONFIG_VG_PTT) || defined(CONFIG_VG_VOICE_LOOPBACK)
#include "vg_ptt_board.h"
#endif

#ifdef CONFIG_VG_VOICE_LOOPBACK
#include "vg_voice_ptt.h"
#include "vg_voice_round.h"
#include "vg_voice_pref.h"
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

    /* Spawn console shell on a side thread.  It gets an explicit stack rather
     * than the pthread default: the shell owns the deep vfat/SDMMC call paths
     * described above. */

    pthread_t tid;
    pthread_attr_t attr;

    if (pthread_attr_init(&attr) == 0)
      {
        pthread_attr_setstacksize(&attr, CONFIG_VG_NSH_STACKSIZE);
        if (pthread_create(&tid, &attr, nsh_thread, NULL) != 0)
          {
            printf("vg: console thread not started\n");
          }

        pthread_attr_destroy(&attr);
      }
    else
      {
        printf("vg: console attr init failed, using default stack\n");
        pthread_create(&tid, NULL, nsh_thread, NULL);
      }

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
    /* Hand the read-only data tools to ai_agent before its daemon starts, so
     * the first round already sees them.  Registration only appends to the
     * registry's static provider array; the daemon does the same from its own
     * task in the flat build, so this is the same registry. */
    vg_agent_tools_register();
    vg_provision_boot_apply_if_needed();
#endif

#ifdef CONFIG_VG_AGENT_OPS
    vg_agent_alarm_start();
#endif

#if defined(CONFIG_VG_VOICE_LOOPBACK) && defined(CONFIG_EXAMPLES_AI_AGENT_VELA)
    /* USER1 -> record -> ASR -> on-board agent -> TTS -> Line Out.  The round
     * worker owns the button task, so the handler is fixed before any edge is
     * delivered.  Stale audio from a crash is removed first, and the stored
     * speech preference is applied before the first round can run. */

    {
      bool muted = false;
      bool have_pref;

      vg_voice_pref_sweep();
      have_pref = (vg_voice_pref_load(&muted) == 0);

      if (vg_voice_round_start() != 0)
        {
          printf("vgvoice: round not started\n");
        }
      else if (have_pref)
        {
          /* start() initialises the session, so the stored value is applied
           * after it, not before. */
          vg_voice_round_set_muted(muted);
        }
    }
#elif defined(CONFIG_VG_VOICE_LOOPBACK)
    /* USER1 -> local record/playback loop.  This starts the button task too,
     * with the recording handler already registered; starting the button task
     * with the default handler first would fix the print-only callback and the
     * recording path would never be reached. */

    if (vg_voice_ptt_start() != 0)
      {
        printf("vgvoice: loopback not started\n");
      }
#elif defined(CONFIG_VG_PTT_AUTOSTART)
    /* USER1 push-to-talk event thread without an audio backend: it only opens
     * /dev/buttons and turns edges into record/submit events. */

    if (vg_ptt_start(NULL, NULL, NULL) != 0)
      {
        printf("vgptt: button thread not started\n");
      }
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
      /* Default 32 KiB: 16 KiB overflowed into heap on HMI builds (see
       * packages/ai_agent agent_config.h CONFIG_VG_HMI note / wedge task). */
      int astack = 32768;

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
