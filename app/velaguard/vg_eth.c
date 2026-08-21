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

static void ping_cb(FAR const struct ping_result_s *result)
{
  if (result->code == ICMP_I_FINISH)
    {
      g_ping_replies = result->nreplies;
    }
}

bool vg_eth_ping(FAR const char *host)
{
  struct ping_info_s info;

  if (host == NULL || host[0] == '\0')
    {
      return false;
    }

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
  icmp_ping(&info);
  return g_ping_replies > 0;
}

void vg_eth_sample(struct vg_eth_sample *out, bool do_ping)
{
  struct ifreq ifr;
  struct in_addr addr;
  int sd;

  memset(out, 0, sizeof(*out));
  sd = socket(AF_INET, SOCK_DGRAM, 0);
  if (sd < 0)
    {
      return;
    }

  memset(&ifr, 0, sizeof(ifr));
  strlcpy(ifr.ifr_name, VG_ETH_IFNAME, IFNAMSIZ);
  if (ioctl(sd, SIOCGIFFLAGS, (unsigned long)&ifr) >= 0)
    {
      out->link = (ifr.ifr_flags & IFF_RUNNING) != 0;
    }

  close(sd);

  if (netlib_get_ipv4addr(VG_ETH_IFNAME, &addr) == 0 && addr.s_addr != 0)
    {
      out->has_ip = true;
      inet_ntop(AF_INET, &addr, out->ip, sizeof(out->ip));
    }

  if (netlib_get_dripv4addr(VG_ETH_IFNAME, &addr) == 0 && addr.s_addr != 0)
    {
      inet_ntop(AF_INET, &addr, out->gw, sizeof(out->gw));
    }

#ifdef CONFIG_VG_NET_PING_HOST
  if (out->gw[0] == '\0' && CONFIG_VG_NET_PING_HOST[0] != '\0')
    {
      strlcpy(out->gw, CONFIG_VG_NET_PING_HOST, sizeof(out->gw));
    }
#endif

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
      out->ping_ok = vg_eth_ping(target);
      g_last_ping_ok = out->ping_ok;
    }
  else
    {
      out->ping_ok = g_last_ping_ok;
    }
}
