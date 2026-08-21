/****************************************************************************
 * app/velaguard/vg_tcp_transport.h
 *
 * 单活动 TCP 出口：POSIX（eth0）或 lesp_*（ESP-01S AT）。
 * MQTT-C pal 通过 weak hook 识别带 TAG 的 lesp 句柄。
 ****************************************************************************/

#ifndef __VELAGUARD_VG_TCP_TRANSPORT_H
#define __VELAGUARD_VG_TCP_TRANSPORT_H

#include <stddef.h>
#include <sys/types.h>

#include "vg_net_policy.h"

/**
 * @brief 打在 lesp 套接字上的高位标签，避免与真实 POSIX fd 混淆。
 * @note mqtt_pal 看到带 TAG 的 fd 才走 vg_mqtt_pal_try_*；否则仍用 send/recv。
 */
#define VG_MQTT_LESP_TAG  0x40000000

/**
 * @brief 按 backend 建立到 host:port 的 TCP 连接。
 *
 * @param backend VG_TCP_POSIX 或 VG_TCP_LESP（VG_TCP_NONE 失败）。
 * @param host    Broker 主机名或 IP。
 * @param port    端口十进制字符串。
 * @retval >=0  句柄；lesp 路径会或上 VG_MQTT_LESP_TAG。
 * @retval -1   失败。
 */
int vg_tcp_open(vg_tcp_backend_t backend, const char *host, const char *port);

/**
 * @brief 关闭连接并把 *fd 置为 -1；按 TAG 选择 close 或 lesp_closesocket。
 *
 * @param fd 指向句柄的指针；NULL 或 *fd<0 时无操作。
 */
void vg_tcp_close(int *fd);

/**
 * @brief MQTT-C pal weak hook：若 fd 带 LESP TAG 则 lesp_send，否则返回 -1 让 pal 走 POSIX。
 *
 * @param fd    mqtt_pal_socket_handle（可能带 TAG）。
 * @param buf   发送缓冲。
 * @param len   字节数。
 * @param flags 传给 lesp_send。
 * @param out   成功时写入已发送字节；失败写 MQTT_ERROR_SOCKET_ERROR。
 * @retval 0   本 hook 已处理（无论成败）。
 * @retval -1  非 lesp fd，调用方继续默认 send。
 */
int vg_mqtt_pal_try_sendall(int fd, const void *buf, size_t len, int flags,
                            ssize_t *out);

/**
 * @brief MQTT-C pal weak hook：lesp_recv 版本；语义同 try_sendall。
 */
int vg_mqtt_pal_try_recvall(int fd, void *buf, size_t bufsz, int flags,
                            ssize_t *out);

#endif
