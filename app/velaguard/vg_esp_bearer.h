/****************************************************************************
 * app/velaguard/vg_esp_bearer.h
 *
 * ESP-01S（USART2 /dev/ttyS1）热备：上电 join，与 RJ45 是否健康无关。
 * TCP 仅在策略选中 wifi 时才走 lesp_*。
 ****************************************************************************/

#ifndef __VELAGUARD_VG_ESP_BEARER_H
#define __VELAGUARD_VG_ESP_BEARER_H

#include <nuttx/compiler.h>
#include <arpa/inet.h>
#include <stdbool.h>

/**
 * @brief ESP 关联与 IP 采样结果。
 * @note 本树 lesp_ap_is_connected() 无实现；assoc 由 STA IP 是否非 0 推断。
 */
struct vg_esp_sample
{
  bool assoc;
  bool has_ip;
  char ip[INET_ADDRSTRLEN];
};

/**
 * @brief 若凭据为空，填入 Kconfig 默认 SSID/PSK。
 */
void vg_esp_cred_init(void);

/**
 * @brief 获取 AT UART 互斥锁（lesp 与 sample/status 共享）。
 */
void vg_esp_at_lock(void);

/**
 * @brief 释放 AT UART 互斥锁。
 */
void vg_esp_at_unlock(void);

/**
 * @brief net_mgr 是否已占用 ESP UART。
 * @note vgesp 在 busy 时应拒绝，避免与 lesp worker 抢 /dev/ttyS1。
 * @retval true  已 initialize，UART 归 net_mgr。
 * @retval false 空闲（或未开 VG_NET_FAILOVER）。
 */
bool vg_esp_uart_busy(void);

/**
 * @brief 覆盖 RAM 中的 Wi-Fi 凭据。
 *
 * @param ssid 非空 SSID。
 * @param psk  密码（可为空串，视 AP 而定）。
 * @retval 0        成功。
 * @retval -EINVAL  参数非法。
 */
int vg_esp_set_wifi(FAR const char *ssid, FAR const char *psk);

/**
 * @brief lesp_initialize；成功后标记 UART busy。
 * @retval 0  成功或已初始化。
 * @retval 负值 失败。
 */
int vg_esp_init(void);

/**
 * @brief 用当前凭据 lesp_ap_connect（热备 join）。
 * @retval 0  成功。
 * @retval 负值 失败。
 */
int vg_esp_join(void);

/**
 * @brief ESP 软复位（AT+RST 路径）；未 init 时先 init。
 * @retval 0  成功。
 * @retval 负值 失败。
 */
int vg_esp_soft_reset(void);

/**
 * @brief 采样关联与 STA IP。
 * @param out 输出；不可为 NULL。
 */
void vg_esp_sample(struct vg_esp_sample *out);

/**
 * @brief 当前 SSID（只读指针，指向内部静态缓冲）。
 */
FAR const char *vg_esp_ssid(void);

#endif
