/****************************************************************************
 * app/velaguard/modbus_port_openvela.h
 *
 * nanoMODBUS 串口适配层公开接口。只负责把字符设备接到
 * nmbs_platform_conf，不含 Modbus 协议逻辑。
 ****************************************************************************/

#ifndef __APPS_VELAGUARD_MODBUS_PORT_OPENVELA_H
#define __APPS_VELAGUARD_MODBUS_PORT_OPENVELA_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>
#include <stdint.h>

#include "nanomodbus/nanomodbus.h"

/****************************************************************************
 * Public Types
 ****************************************************************************/

struct vg_modbus_port_s
{
  int fd;
};

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

/* 打开串口并设 raw 模式（波特率跟随驱动，UART7 当前为 9600 8N1）。 */

int vg_modbus_port_open(FAR struct vg_modbus_port_s *port,
                        FAR const char *devpath);

void vg_modbus_port_close(FAR struct vg_modbus_port_s *port);

/* 把本 port 填进 nanoMODBUS 的 platform_conf（RTU + read/write/flush）。 */

void vg_modbus_port_bind(FAR struct vg_modbus_port_s *port,
                         FAR nmbs_platform_conf *pc);

#endif /* __APPS_VELAGUARD_MODBUS_PORT_OPENVELA_H */
