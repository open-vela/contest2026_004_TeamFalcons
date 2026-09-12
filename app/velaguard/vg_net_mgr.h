/****************************************************************************
 * app/velaguard/vg_net_mgr.h
 *
 * 故障转移运行时：后台线程采样 eth0 / ESP，驱动策略，并维持单条 MQTT TCP。
 ****************************************************************************/

#ifndef __VELAGUARD_VG_NET_MGR_H
#define __VELAGUARD_VG_NET_MGR_H

#include <stdio.h>
#include <stdbool.h>

#include "vg_net_policy.h"

/**
  * @brief  对外的实时网络状态快照（HMI 状态栏 / 时间同步共用）。
  * @note   取自 net_mgr 线程最近一拍缓存的 eth/esp 采样，
  *         查询方不做 IO，不碰 AT UART。
  */
struct vg_net_live_status
{
  bool rj45_link;                    /**< eth0 IFF_RUNNING */
  bool rj45_has_ip;                  /**< DHCP/静态 IPv4 非 0 */
  bool rj45_ping_ok;                 /**< 最近一次 ping 结果 */
  bool wifi_assoc;                   /**< ESP STA 已关联（非零 IP 推断） */
  bool wifi_has_ip;                  /**< ESP STA IPv4 可用 */
  bool mqtt_online;                  /**< MQTT 已收 CONNACK */
  vg_egress_t egress;                /**< 活动出口 */
  vg_net_state_t state;              /**< 策略状态机 */
  char ip[16];                       /**< 活动出口 IP；无则空串 */
};

/**
  * @brief  启动 net_mgr 后台线程（幂等）。
  * @note   由 velaguard_app_main 在 NSH 线程之后调用；不依赖 NSH。
  *         线程栈 8192，create 后 detach；上电即尝试 ESP join（热备）。
  * @retval 0   已启动或早已启动。
  * @retval -1  pthread_create 失败。
  */
int vg_net_mgr_start(void);

/**
  * @brief  读取最近一拍的网络状态快照。
  * @note   线程安全性：eth/esp 采样副本与策略字段在 g_lock 内读取。
  *         net_mgr 未启动时各字段为 false/none，仍返回 0。
  * @param  out  输出；不可为 NULL。
  * @retval 0    成功。
  * @retval -1   参数非法。
  */
int vg_net_mgr_status(struct vg_net_live_status *out);

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
