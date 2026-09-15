/****************************************************************************
 * app/velaguard/vg_mqtt_session.h
 *
 * 故障转移第一号 TCP 用户：MQTT-C 明文连看板 Broker。
 * CONNACK 才算该出口在线；切出口须先 close 再 open。
 * 采集/HMI 线程只调用 enqueue / notify；publish 在 net_mgr 的 poll 里。
 ****************************************************************************/

#ifndef __VELAGUARD_VG_MQTT_SESSION_H
#define __VELAGUARD_VG_MQTT_SESSION_H

#include "vg_net_policy.h"
#include <stdbool.h>

void vg_mqtt_session_close(void);

void vg_mqtt_session_poll(void);

int vg_mqtt_session_open(vg_tcp_backend_t backend, const char *net_name);

bool vg_mqtt_session_online(void);

void vg_mqtt_enqueue_alarm(const char *id, const char *kind,
                           const char *state, float value, float thr,
                           const char *level, long long ts_ms);

void vg_mqtt_notify_point_table(void);

#endif
