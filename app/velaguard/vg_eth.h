/****************************************************************************
 * app/velaguard/vg_eth.h
 *
 * RJ45（eth0）采样与 icmp_ping。ping 只作故障判定，不会故障转移到 ESP。
 ****************************************************************************/

#ifndef __VELAGUARD_VG_ETH_H
#define __VELAGUARD_VG_ETH_H

#include <nuttx/compiler.h>
#include <stdbool.h>
#include <arpa/inet.h>

/**
  * @brief  eth0 一拍采样结果。
  * @note   由 vg_eth_sample() 填充，供 vg_net_mgr 写入 vg_net_sample。
  */
struct vg_eth_sample
{
  bool link;                         /**< IFF_RUNNING */
  bool has_ip;                       /**< DHCP/静态 IPv4 非 0 */
  bool ping_ok;                      /**< 本拍或缓存的最近一次 ping */
  char ip[INET_ADDRSTRLEN];
  char gw[INET_ADDRSTRLEN];          /**< DHCP 网关；空则可用 Kconfig 兜底 */
};

/**
  * @brief  对 host 发 1 次 ICMP echo（绑定 eth0，若启用 BINDTODEVICE）。
  * @note   仅判定 RJ45 上游可达性；NSH `ping` 语义不变，也不会改走 ESP。
  * @param  host  点分 IP 或主机名；NULL/空串返回 false。
  * @retval true  收到至少 1 个 reply。
  * @retval false 超时或失败。
  */
bool vg_eth_ping(FAR const char *host);

/**
  * @brief  采样 link / IP / 网关；可选执行 ping。
  * @note   do_ping=false 时 ping_ok 复用上次成功缓存，
  *         避免退避窗口内的空转把失败 streak 打满。
  * @param  out      输出；不可为 NULL。
  * @param  do_ping  true 时在有 link+IP 时 ping 网关（或 VG_NET_PING_HOST）。
  * @retval None
  */
void vg_eth_sample(struct vg_eth_sample *out, bool do_ping);

#endif
