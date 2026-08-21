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
 * @brief vgnet NSH 入口：查询状态或注入故障/改 Wi-Fi 凭据。
 * @retval 0 成功；非 0 用法或参数错误。
 */
int main(int argc, FAR char *argv[])
{
  if (argc < 2 || strcmp(argv[1], "status") == 0)
    {
      vg_net_mgr_print_status(stdout);
      return 0;
    }

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
