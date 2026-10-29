/* SPDX-License-Identifier: Apache-2.0 */
#ifdef __NuttX__
#include <nuttx/config.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <nuttx/net/mii.h>
#include <unistd.h>
#endif
#include "vg_ota_link.h"
#include <errno.h>
#include <string.h>

int vg_ota_link_check(const struct vg_ota_link_ports *p,struct vg_ota_link_info *info)
{
  int ret=-EINVAL;
  if(!p || !p->address || !p->read || !p->delay_ms || !info) return ret;
  memset(info,0,sizeof(*info));
  for(unsigned attempt=0;attempt<3;attempt++) {
    ret=p->address(p->ctx,&info->phy);
    if(!ret && info->phy>31) return -EIO;
    if(!ret) ret=p->read(p->ctx,info->phy,1,&info->first);
    if(!ret) ret=p->read(p->ctx,info->phy,1,&info->second);
    if(ret!=-EAGAIN && ret!=-EBUSY) break;
    p->delay_ms(p->ctx,1);
  }
  if(ret) return ret;
  if(info->first==0xffff || info->second==0xffff) return -EIO;
  /* BMSR 的 link 位为 latch-low；第二次读取才表示当前链路。 */
  return (info->second&4u) ? 1 : 0;
}
#if defined(__NuttX__) && defined(CONFIG_NETDEV_PHY_IOCTL)
static int address(void *ctx,uint16_t *phy)
{
  int fd=*(int *)ctx; struct ifreq req; memset(&req,0,sizeof(req));
  strcpy(req.ifr_name,"eth0");
  if(ioctl(fd,SIOCGMIIPHY,(unsigned long)&req)<0) return -errno;
  *phy=req.ifr_mii_phy_id; return 0;
}
static int phyread(void *ctx,uint16_t phy,uint16_t reg,uint16_t *value)
{
  int fd=*(int *)ctx; struct ifreq req; memset(&req,0,sizeof(req));
  strcpy(req.ifr_name,"eth0"); req.ifr_mii_phy_id=phy; req.ifr_mii_reg_num=reg;
  if(ioctl(fd,SIOCGMIIREG,(unsigned long)&req)<0) return -errno;
  *value=req.ifr_mii_val_out; return 0;
}
static void delay(void *ctx,unsigned ms) { (void)ctx; usleep(ms*1000u); }
#endif
int vg_ota_link_probe(struct vg_ota_link_info *info)
{
#if defined(__NuttX__) && defined(CONFIG_NETDEV_PHY_IOCTL)
  int fd=socket(AF_INET,SOCK_DGRAM,0);
  if(fd<0) return -errno;
  struct vg_ota_link_ports ports={&fd,address,phyread,delay};
  int ret=vg_ota_link_check(&ports,info);
  close(fd); return ret;
#else
  if(info) memset(info,0,sizeof(*info));
  return -ENOTSUP;
#endif
}
int vg_ota_link_ready(void)
{
  struct vg_ota_link_info info;
  return vg_ota_link_probe(&info)==1;
}
