/****************************************************************************
 * app/velaguard/vgnet.c
 *
 * NSH 观察/注入命令，不启动 net_mgr。
 *
 * 用法：
 *   vgnet [status]
 *   vgnet inject <rj45|wifi> <down|up|auto>
 *   vgnet wifi <ssid> <psk>
 ****************************************************************************/

#include <nuttx/config.h>
#include <stdio.h>
#include <string.h>

#include "vg_net_mgr.h"

/**
  * @brief  vgnet NSH 入口：查询状态或注入故障/改 Wi-Fi 凭据。
  * @note   本命令不是故障转移的启动条件；net_mgr 由 velaguard_app_main 拉起。
  *         inject 覆盖策略健康位，便于无拔线验收 AC；wifi 仅改 RAM 凭据。
  * @param  argc  参数个数。
  * @param  argv  参数向量；argv[1] 为子命令。
  * @retval 0  成功。
  * @retval 1  用法或参数错误。
  */
int main(int argc, FAR char *argv[])
{
  /* Default / explicit status: dump policy + bearer snapshot */

  if (argc < 2 || strcmp(argv[1], "status") == 0)
    {
      vg_net_mgr_print_status(stdout);
      return 0;
    }

  /* Force bearer health for lab acceptance without unplugging */

  if (strcmp(argv[1], "inject") == 0)
    {
      if (argc < 4)
        {
          fprintf(stderr, "Usage: vgnet inject <rj45|wifi> <down|up|auto>\n");
          return 1;
        }

      if (vg_net_mgr_inject(argv[2], argv[3]) != 0)
        {
          fprintf(stderr, "vgnet: bad inject\n");
          return 1;
        }

      printf("vgnet: inject %s %s\n", argv[2], argv[3]);
      return 0;
    }

  /* Override in-RAM AP credentials; join happens on next mgr retry */

  if (strcmp(argv[1], "wifi") == 0)
    {
      if (argc < 4)
        {
          fprintf(stderr, "Usage: vgnet wifi <ssid> <psk>\n");
          return 1;
        }

      if (vg_net_mgr_set_wifi(argv[2], argv[3]) != 0)
        {
          return 1;
        }

      printf("vgnet: wifi credentials updated (join on next retry)\n");
      return 0;
    }

  fprintf(stderr, "Usage: vgnet [status|inject ...|wifi ...]\n");
  return 1;
}
