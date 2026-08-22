/****************************************************************************
 * app/velaguard/vg_mqtt_session.h
 *
 * 故障转移第一号 TCP 用户：MQTT-C 明文连测试 Broker。
 * CONNACK 才算该出口在线；切出口须先 close 再 open。
 ****************************************************************************/

#ifndef __VELAGUARD_VG_MQTT_SESSION_H
#define __VELAGUARD_VG_MQTT_SESSION_H

#include "vg_net_policy.h"
#include <stdbool.h>

/**
  * @brief  关闭当前 MQTT TCP，清除 online 标志。
  * @retval None
  */
void vg_mqtt_session_close(void);

/**
  * @brief  驱动 mqtt_sync；出错则关闭会话。
  * @note   由 net_mgr 周期调用；无在线会话时立即返回。
  * @retval None
  */
void vg_mqtt_session_poll(void);

/**
  * @brief  在指定 backend 上建连、CONNECT，等待 CONNACK 后发 QoS0 retained status。
  * @param  backend   VG_TCP_POSIX 或 VG_TCP_LESP。
  * @param  net_name  写入 status JSON 的 network 字段（rj45|esp01|none）。
  * @retval 0   CONNACK 成功且已 publish status。
  * @retval -1  建连/CONNECT/CONNACK 失败（会话已关闭）。
  */
int vg_mqtt_session_open(vg_tcp_backend_t backend, const char *net_name);

/**
  * @brief  当前会话是否已收到 CONNACK。
  * @retval true   在线。
  * @retval false  未连接或已关闭。
  */
bool vg_mqtt_session_online(void);

#endif
