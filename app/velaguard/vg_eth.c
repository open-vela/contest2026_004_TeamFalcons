/****************************************************************************
 * app/velaguard/vg_eth.c
 *
 * RJ45（eth0）link/IP/网关采样 + icmp_ping。
 * ping 只判定 RJ45 是否异常；NSH ping 不会改走 ESP。API 见 vg_eth.h。
 ****************************************************************************/

#include <nuttx/config.h>

#include <arpa/inet.h>
#include <net/if.h>
#include <netinet/in.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

#include "netutils/icmp_ping.h"
#include "netutils/netlib.h"

#include "vg_eth.h"

#ifndef VG_ETH_IFNAME
#  define VG_ETH_IFNAME "eth0"
#endif

static int g_ping_replies;
static bool g_last_ping_ok;

/**
  * @brief  icmp_ping 完成回调：记录本轮 reply 个数。
  * @note   仅在 ICMP_I_FINISH 时更新 g_ping_replies；中间进度码忽略。
  * @param  result  netutils icmp_ping 结果结构。
  * @retval None
  */
static void ping_cb(FAR const struct ping_result_s *result)
{
  /* Capture final reply count when the ping sequence finishes */

  if (result->code == ICMP_I_FINISH)
    {
      g_ping_replies = result->nreplies;
    }
}

/**
  * @brief  对指定 host 发送 1 次 ICMP echo（尽量绑定 eth0）。
  * @note   本函数只作 RJ45 健康判定，不会把流量改走 ESP。
  *         timeout 与 NSH ping 对齐为 1000ms；count=1。
  *         若启用 CONFIG_NET_BINDTODEVICE，探测绑定 VG_ETH_IFNAME，
  *         避免误走其它 netdev。
  * @param  host  点分 IPv4 或主机名；NULL/空串视为失败。
  * @retval true  收到至少 1 个 echo reply。
  * @retval false 参数非法、超时或无 reply。
  */
bool vg_eth_ping(FAR const char *host)
{
  struct ping_info_s info;

  /* Reject empty target before touching the stack */

  if (host == NULL || host[0] == '\0')
    {
      return false;
    }

  /* Reset reply counter and fill a one-shot ping request */

  g_ping_replies = 0;
  memset(&info, 0, sizeof(info));
  info.hostname = host;
#ifdef CONFIG_NET_BINDTODEVICE
  info.devname  = VG_ETH_IFNAME;
#endif
  info.count    = 1;
  info.datalen  = 32;
  info.delay    = 0;
  info.timeout  = 1000; /* milliseconds, same as NSH ping */
  info.callback = ping_cb;
  info.priv     = &g_ping_replies;

  /* Blocking icmp_ping; result arrives via ping_cb */

  icmp_ping(&info);
  return g_ping_replies > 0;
}

/**
  * @brief  采样 eth0 link / IPv4 / 网关，并按需执行 ping。
  * @note   link 取自 SIOCGIFFLAGS 的 IFF_RUNNING。
  *         网关优先 DHCP drip；若空且配置了 CONFIG_VG_NET_PING_HOST 则兜底。
  *         do_ping=false 时复用 g_last_ping_ok，避免退避空转把失败 streak 打满。
  *         link 或 IP 缺失时强制 ping_ok=false 并刷新缓存。
  * @param  out      输出采样结构；调用方保证非 NULL。
  * @param  do_ping  true=本拍真正发 ICMP；false=只读缓存。
  * @retval None
  */
void vg_eth_sample(struct vg_eth_sample *out, bool do_ping)
{
  struct ifreq ifr;
  struct in_addr addr;
  int sd;

  /* Clear output; early return leaves all-false / empty strings */

  memset(out, 0, sizeof(*out));
  sd = socket(AF_INET, SOCK_DGRAM, 0);
  if (sd < 0)
    {
      return;
    }

  /* Query IFF_RUNNING on eth0 as RJ45 link indicator */

  memset(&ifr, 0, sizeof(ifr));
  strlcpy(ifr.ifr_name, VG_ETH_IFNAME, IFNAMSIZ);
  if (ioctl(sd, SIOCGIFFLAGS, (unsigned long)&ifr) >= 0)
    {
      out->link = (ifr.ifr_flags & IFF_RUNNING) != 0;
    }

  close(sd);

  /* Read assigned IPv4 address (DHCP or static) */

  if (netlib_get_ipv4addr(VG_ETH_IFNAME, &addr) == 0 && addr.s_addr != 0)
    {
      out->has_ip = true;
      inet_ntop(AF_INET, &addr, out->ip, sizeof(out->ip));
    }

  /* Prefer DHCP gateway as ping target */

  if (netlib_get_dripv4addr(VG_ETH_IFNAME, &addr) == 0 && addr.s_addr != 0)
    {
      inet_ntop(AF_INET, &addr, out->gw, sizeof(out->gw));
    }

#ifdef CONFIG_VG_NET_PING_HOST
  /* Kconfig override when DHCP did not publish a gateway */

  if (out->gw[0] == '\0' && CONFIG_VG_NET_PING_HOST[0] != '\0')
    {
      strlcpy(out->gw, CONFIG_VG_NET_PING_HOST, sizeof(out->gw));
    }
#endif

  /* Without link or IP, RJ45 cannot be healthy — skip ping */

  if (!out->link || !out->has_ip)
    {
      out->ping_ok = false;
      g_last_ping_ok = false;
      return;
    }

  if (do_ping)
    {
      const char *target = out->gw[0] ? out->gw : NULL;
#ifdef CONFIG_VG_NET_PING_HOST
      if (target == NULL && CONFIG_VG_NET_PING_HOST[0] != '\0')
        {
          target = CONFIG_VG_NET_PING_HOST;
        }
#endif

      /* Perform real probe and refresh the last-ok cache */

      out->ping_ok = vg_eth_ping(target);
      g_last_ping_ok = out->ping_ok;
    }
  else
    {
      /* Waiting tick: reuse cache so policy backoff is not polluted */

      out->ping_ok = g_last_ping_ok;
    }
}
