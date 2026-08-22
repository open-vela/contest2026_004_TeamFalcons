/****************************************************************************
 * app/velaguard/vg_net_policy.h
 *
 * Pure failover policy. No NuttX, POSIX, or ESP types.
 * Host tests compile this file with gcc.
 ****************************************************************************/

#ifndef __VELAGUARD_VG_NET_POLICY_H
#define __VELAGUARD_VG_NET_POLICY_H

#include <stdbool.h>
#include <stdint.h>

/** 连续 ping 失败达到该次数才判 RJ45 不健康。 */
#define VG_NET_PING_FAIL_N       3
/** 从 Wi-Fi 抢回 RJ45 前，RJ45 需持续健康的窗口（毫秒）。 */
#define VG_NET_RJ45_HOLD_MS      10000u
/** 退避基数：1s，每次指数 ×2。 */
#define VG_NET_BACKOFF_BASE_MS   1000u
/** 退避上限：60s。 */
#define VG_NET_BACKOFF_MAX_MS    60000u
/** Wi-Fi join 连续失败达到该次数时请求 ESP 软复位。 */
#define VG_NET_ESP_RESET_TRIES   5
/** RJ45 稳定在线达到该时长后重置退避指数。 */
#define VG_NET_STABLE_RESET_MS   (5u * 60u * 1000u)

/**
  * @brief  策略层对外状态机（供日志 / vgnet status）。
  * @note   VG_NET_DEGRADED 预留；当前实现主要用 DOWN / ONLINE_*。
  */
typedef enum
{
  VG_NET_DOWN = 0,
  VG_NET_CONNECTING,
  VG_NET_ONLINE_RJ45,
  VG_NET_ONLINE_WIFI,
  VG_NET_DEGRADED
} vg_net_state_t;

/**
  * @brief  活动业务出口（同时只应有一条）。
  */
typedef enum
{
  VG_EGRESS_NONE = 0,
  VG_EGRESS_RJ45,
  VG_EGRESS_WIFI
} vg_egress_t;

/**
  * @brief  TCP 传输后端，与 active_egress 一一对应。
  * @note   rj45 → posix（内核 eth0）；wifi → lesp（ESP AT 套接字）；none → 无 TCP。
  */
typedef enum
{
  VG_TCP_NONE = 0,
  VG_TCP_POSIX,
  VG_TCP_LESP
} vg_tcp_backend_t;

/**
  * @brief  测试注入：覆盖真实采样，便于无拔线验收。
  */
typedef enum
{
  VG_INJECT_AUTO = 0,
  VG_INJECT_FORCE_DOWN,
  VG_INJECT_FORCE_UP
} vg_inject_t;

/**
  * @brief  一拍采样输入（由 vg_net_mgr 填充后交给 step）。
  * @note   rj45_ping_sampled 为 false 时不得累加失败 streak，
  *         否则退避窗口内的空转会提前判挂。
  */
struct vg_net_sample
{
  bool rj45_link;
  bool rj45_has_ip;
  bool rj45_ping_ok;
  bool rj45_ping_sampled;
  bool wifi_assoc;
  bool wifi_has_ip;
};

/**
  * @brief  策略状态与输出标志。
  * @note   调用方每拍读：
  *         - active_egress / tcp_backend：该走哪条 TCP
  *         - tcp_reconnect：本拍是否发生出口切换（须关旧开新）
  *         - request_esp_reset：是否该软复位 ESP（由 note_wifi_join 置位）
  *         - next_rj45_probe_ms / next_wifi_join_ms：下次动作时间
  */
struct vg_net_policy
{
  vg_net_state_t     state;
  vg_egress_t        active_egress;
  vg_tcp_backend_t   tcp_backend;
  bool               tcp_reconnect;
  bool               request_esp_reset;
  vg_inject_t        inject_rj45;
  vg_inject_t        inject_wifi;
  int                jitter_pct;
  unsigned           rj45_ping_fail_streak;
  unsigned           wifi_fail_streak;
  unsigned           rj45_backoff_exp;
  unsigned           wifi_backoff_exp;
  uint64_t           rj45_healthy_since_ms;
  uint64_t           next_rj45_probe_ms;
  uint64_t           next_wifi_join_ms;
  uint64_t           online_since_ms;
  bool               prev_rj45_link;
  bool               initialized;
};

/**
  * @brief  清零并标记策略已初始化。
  * @param  p  策略对象。
  * @retval None
  */
void vg_net_policy_init(struct vg_net_policy *p);

/**
  * @brief  根据本拍采样推进故障转移策略。
  * @note   规则摘要：
  *         - RJ45 健康 = link ∧ DHCP ∧（连续 ping 失败 < 3）
  *         - RJ45 挂且 Wi-Fi 可用 → egress=wifi、backend=lesp
  *         - 从 wifi 抢回 rj45 需连续健康满 VG_NET_RJ45_HOLD_MS
  *         - 出口变化时 tcp_reconnect=true；调用方负责关旧 TCP 再开新
  *         - 仅在真实 ping 采样失败时调度退避；等待 tick 不得拉长探测周期
  * @param  p       策略对象（输入/输出）。
  * @param  now_ms  单调时钟毫秒。
  * @param  sample  本拍采样；不可为 NULL。
  * @retval None
  */
void vg_net_policy_step(struct vg_net_policy *p,
                        uint64_t now_ms,
                        const struct vg_net_sample *sample);

/**
  * @brief  记录一次 Wi-Fi join 结果，并在失败达阈值时请求软复位。
  * @param  p   策略对象。
  * @param  ok  true=join 成功；false=失败（累加 streak）。
  * @retval None
  */
void vg_net_policy_note_wifi_join(struct vg_net_policy *p, bool ok);

/**
  * @brief  计算指数退避毫秒数：base×2^exp，封顶 VG_NET_BACKOFF_MAX_MS。
  * @note   jitter_pct 预留；主机单测传 0。当前实现故意忽略 RNG。
  * @param  exp         退避指数（0 → 1s，1 → 2s，…）。
  * @param  jitter_pct  计划抖动百分比（未实现）。
  * @retval 退避等待毫秒数。
  */
uint32_t vg_net_policy_backoff_ms(unsigned exp, int jitter_pct);

/**
  * @brief  状态枚举转日志字符串。
  * @param  s  策略状态。
  * @retval 静态 C 字符串。
  */
const char *vg_net_state_str(vg_net_state_t s);

/**
  * @brief  出口枚举转日志字符串（rj45|wifi|none）。
  * @param  e  活动出口。
  * @retval 静态 C 字符串。
  */
const char *vg_egress_str(vg_egress_t e);

/**
  * @brief  TCP 后端枚举转日志字符串（posix|lesp|none）。
  * @param  b  传输后端。
  * @retval 静态 C 字符串。
  */
const char *vg_tcp_backend_str(vg_tcp_backend_t b);

#endif /* __VELAGUARD_VG_NET_POLICY_H */
