/****************************************************************************
 * app/velaguard/vg_net_mgr.h
 *
 * 故障转移运行时：后台线程采样 eth0 / ESP，驱动策略，并维持单条 MQTT TCP。
 ****************************************************************************/

#ifndef __VELAGUARD_VG_NET_MGR_H
#define __VELAGUARD_VG_NET_MGR_H

#include <stdio.h>
#include <stdbool.h>

/**
  * @brief  启动 net_mgr 后台线程（幂等）。
  * @note   由 velaguard_app_main 在 NSH 线程之后调用；不依赖 NSH。
  *         线程栈 8192，create 后 detach；上电即尝试 ESP join（热备）。
  * @retval 0   已启动或早已启动。
  * @retval -1  pthread_create 失败。
  */
int vg_net_mgr_start(void);

/**
  * @brief  打印当前策略与承载状态到 out（供 NSH `vgnet status`）。
  * @param  out  输出流，通常为 stdout。
  * @retval None
  */
void vg_net_mgr_print_status(FILE *out);

/**
  * @brief  注入 bearer 健康覆盖，用于无物理拔线时验收切换。
  * @param  bearer  "rj45" 或 "wifi"。
  * @param  mode    "down" | "up" | "auto"。
  * @retval 0   成功。
  * @retval -1  参数非法。
  */
int vg_net_mgr_inject(const char *bearer, const char *mode);

/**
  * @brief  覆盖 RAM 中的 Wi-Fi 凭据；下次 join 重试时生效。
  * @param  ssid  AP SSID。
  * @param  psk   AP 密码。
  * @retval 0     成功。
  * @retval 负值  参数非法（见 vg_esp_set_wifi）。
  */
int vg_net_mgr_set_wifi(const char *ssid, const char *psk);

#endif
