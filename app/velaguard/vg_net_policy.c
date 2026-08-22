/****************************************************************************
 * app/velaguard/vg_net_policy.c
 *
 * 纯策略实现：无 NuttX / 套接字。主机单测直接链本文件。
 * API 说明见 vg_net_policy.h。
 ****************************************************************************/

#include "vg_net_policy.h"

#include <string.h>

/**
  * @brief  将退避毫秒数钳位到 VG_NET_BACKOFF_MAX_MS。
  * @param  ms  原始等待时间。
  * @retval 钳位后的毫秒数。
  */
static uint32_t clamp_backoff(uint32_t ms)
{
  if (ms > VG_NET_BACKOFF_MAX_MS)
    {
      return VG_NET_BACKOFF_MAX_MS;
    }

  return ms;
}

/**
  * @brief  计算指数退避毫秒数：base×2^exp，封顶 VG_NET_BACKOFF_MAX_MS。
  * @note   jitter_pct 预留；当前故意忽略 RNG，避免纯策略层依赖随机源。
  *         主机单测传 jitter_pct=0；运行时可传 20 但此处仍不抖动。
  * @param  exp         退避指数（0 → 1s，1 → 2s，…）。
  * @param  jitter_pct  计划抖动百分比（未实现）。
  * @retval 退避等待毫秒数。
  */
uint32_t vg_net_policy_backoff_ms(unsigned exp, int jitter_pct)
{
  uint32_t ms = VG_NET_BACKOFF_BASE_MS;
  unsigned i;

  /* Double base until exp exhausted or cap reached */

  for (i = 0; i < exp; i++)
    {
      if (ms > VG_NET_BACKOFF_MAX_MS / 2)
        {
          ms = VG_NET_BACKOFF_MAX_MS;
          break;
        }

      ms *= 2;
    }

  ms = clamp_backoff(ms);

  /* Host tests use jitter_pct=0. Runtime may pass 20; skip RNG here. */

  (void)jitter_pct;
  return ms;
}

/**
  * @brief  清零并标记策略已初始化。
  * @param  p  策略对象。
  * @retval None
  */
void vg_net_policy_init(struct vg_net_policy *p)
{
  memset(p, 0, sizeof(*p));
  p->jitter_pct = 0;
  p->initialized = true;
}

/**
  * @brief  原始 RJ45 健康：link ∧ IP ∧ 本拍 ping_ok。
  * @note   AUTO 模式下最终健康仍受失败 streak 阈值约束。
  */
static bool rj45_raw_ok(const struct vg_net_sample *s)
{
  return s->rj45_link && s->rj45_has_ip && s->rj45_ping_ok;
}

/**
  * @brief  原始 Wi-Fi 健康：已关联且有 STA IP。
  */
static bool wifi_raw_ok(const struct vg_net_sample *s)
{
  return s->wifi_assoc && s->wifi_has_ip;
}

/**
  * @brief  按 inject 覆盖原始健康位。
  * @param  inj  AUTO / FORCE_DOWN / FORCE_UP。
  * @param  raw  采样得到的原始健康。
  * @param  out  覆盖后的输出。
  * @retval None
  */
static void apply_inject(vg_inject_t inj, bool raw, bool *out)
{
  if (inj == VG_INJECT_FORCE_DOWN)
    {
      *out = false;
    }
  else if (inj == VG_INJECT_FORCE_UP)
    {
      *out = true;
    }
  else
    {
      *out = raw;
    }
}

/**
  * @brief  记录一次 Wi-Fi join 结果；失败达阈值时请求软复位。
  * @note   成功清零 fail_streak / backoff_exp。
  *         失败累加；达 VG_NET_ESP_RESET_TRIES 置 request_esp_reset 并清零 streak。
  * @param  p   策略对象。
  * @param  ok  true=join 成功；false=失败。
  * @retval None
  */
void vg_net_policy_note_wifi_join(struct vg_net_policy *p, bool ok)
{
  p->request_esp_reset = false;
  if (ok)
    {
      /* Join recovered — clear failure accounting */

      p->wifi_fail_streak = 0;
      p->wifi_backoff_exp = 0;
      return;
    }

  p->wifi_fail_streak++;
  if (p->wifi_fail_streak >= VG_NET_ESP_RESET_TRIES)
    {
      /* Threshold hit: ask mgr to soft-reset ESP, then restart streak */

      p->request_esp_reset = true;
      p->wifi_fail_streak = 0;
    }

  if (p->wifi_backoff_exp < 16)
    {
      p->wifi_backoff_exp++;
    }
}

/**
  * @brief  一拍策略步进：更新健康判定、活动出口、tcp_backend、tcp_reconnect。
  * @note   规则摘要：
  *         - RJ45 健康 = link ∧ DHCP ∧（连续 ping 失败 < VG_NET_PING_FAIL_N）
  *         - RJ45 挂且 Wi-Fi 可用 → egress=wifi、backend=lesp
  *         - 从 wifi 抢回 rj45 需连续健康满 VG_NET_RJ45_HOLD_MS
  *         - 出口变化时 tcp_reconnect=true；调用方负责关旧 TCP 再开新
  *         - 仅在真实 ping 采样失败时调度退避；等待 tick 不得拉长探测周期
  *         - link 沿 down→up 立即探测（next_rj45_probe_ms = now）
  * @param  p       策略对象（输入/输出）。
  * @param  now_ms  单调时钟毫秒。
  * @param  sample  本拍采样；不可为 NULL。
  * @retval None
  */
void vg_net_policy_step(struct vg_net_policy *p,
                        uint64_t now_ms,
                        const struct vg_net_sample *sample)
{
  bool healthy_rj45;
  bool healthy_wifi;
  vg_egress_t desired;
  vg_egress_t prev;
  bool link_rise;

  if (!p->initialized)
    {
      vg_net_policy_init(p);
    }

  prev = p->active_egress;
  link_rise = sample->rj45_link && !p->prev_rj45_link;
  p->prev_rj45_link = sample->rj45_link;

  /* Link rising edge forces an immediate eth0 probe */

  if (link_rise)
    {
      p->next_rj45_probe_ms = now_ms;
    }

  apply_inject(p->inject_rj45, rj45_raw_ok(sample), &healthy_rj45);
  apply_inject(p->inject_wifi, wifi_raw_ok(sample), &healthy_wifi);

  /* AUTO: apply ping-fail hysteresis; FORCE_*: pin health and streak */

  if (p->inject_rj45 == VG_INJECT_AUTO)
    {
      if (sample->rj45_ping_sampled)
        {
          if (!sample->rj45_ping_ok)
            {
              if (p->rj45_ping_fail_streak < 255)
                {
                  p->rj45_ping_fail_streak++;
                }
            }
          else
            {
              p->rj45_ping_fail_streak = 0;
            }
        }

      healthy_rj45 = sample->rj45_link && sample->rj45_has_ip &&
                     (p->rj45_ping_fail_streak < VG_NET_PING_FAIL_N);
    }
  else if (p->inject_rj45 == VG_INJECT_FORCE_UP)
    {
      p->rj45_ping_fail_streak = 0;
      healthy_rj45 = true;
    }
  else
    {
      p->rj45_ping_fail_streak = VG_NET_PING_FAIL_N;
      healthy_rj45 = false;
    }

  if (healthy_rj45)
    {
      /* Keep eth0 probes running during hysteresis and the 10s hold.
       * After VG_NET_STABLE_RESET_MS online, clear RJ45 backoff exp.
       */

      p->next_rj45_probe_ms = now_ms;
      if (p->rj45_healthy_since_ms == 0)
        {
          p->rj45_healthy_since_ms = now_ms;
        }

      if (p->online_since_ms == 0)
        {
          p->online_since_ms = now_ms;
        }
      else if (now_ms - p->online_since_ms >= VG_NET_STABLE_RESET_MS)
        {
          p->rj45_backoff_exp = 0;
        }
    }
  else
    {
      p->rj45_healthy_since_ms = 0;

      /* Only a real probe schedules backoff. Waiting ticks must not
       * stretch next_rj45_probe_ms or recovery never runs (link stays
       * up, gateway returns, no link-rise edge).
       */

      if (p->inject_rj45 == VG_INJECT_AUTO && sample->rj45_ping_sampled &&
          !sample->rj45_ping_ok && !link_rise)
        {
          uint32_t wait = vg_net_policy_backoff_ms(p->rj45_backoff_exp,
                                                   p->jitter_pct);
          if (p->rj45_backoff_exp < 16)
            {
              p->rj45_backoff_exp++;
            }

          p->next_rj45_probe_ms = now_ms + wait;
        }
    }

  /* Prefer RJ45; hold Wi-Fi until RJ45 healthy window elapses */

  if (healthy_rj45)
    {
      if (p->active_egress == VG_EGRESS_WIFI)
        {
          if (now_ms - p->rj45_healthy_since_ms >= VG_NET_RJ45_HOLD_MS)
            {
              desired = VG_EGRESS_RJ45;
            }
          else
            {
              desired = VG_EGRESS_WIFI;
            }
        }
      else
        {
          desired = VG_EGRESS_RJ45;
        }
    }
  else if (healthy_wifi)
    {
      desired = VG_EGRESS_WIFI;
    }
  else
    {
      desired = VG_EGRESS_NONE;
    }

  /* Publish egress / backend / reconnect for the transport layer */

  p->active_egress = desired;
  p->tcp_reconnect = (desired != prev);

  if (desired == VG_EGRESS_RJ45)
    {
      p->tcp_backend = VG_TCP_POSIX;
      p->state = VG_NET_ONLINE_RJ45;
    }
  else if (desired == VG_EGRESS_WIFI)
    {
      p->tcp_backend = VG_TCP_LESP;
      p->state = VG_NET_ONLINE_WIFI;
    }
  else
    {
      p->tcp_backend = VG_TCP_NONE;
      p->state = VG_NET_DOWN;
    }
}

/**
  * @brief  状态枚举转日志字符串。
  * @param  s  策略状态。
  * @retval 静态 C 字符串（connecting|online_rj45|…|down）。
  */
const char *vg_net_state_str(vg_net_state_t s)
{
  switch (s)
    {
      case VG_NET_CONNECTING:
        return "connecting";
      case VG_NET_ONLINE_RJ45:
        return "online_rj45";
      case VG_NET_ONLINE_WIFI:
        return "online_wifi";
      case VG_NET_DEGRADED:
        return "degraded";
      case VG_NET_DOWN:
      default:
        return "down";
    }
}

/**
  * @brief  出口枚举转日志字符串。
  * @param  e  活动出口。
  * @retval 静态 C 字符串（rj45|wifi|none）。
  */
const char *vg_egress_str(vg_egress_t e)
{
  switch (e)
    {
      case VG_EGRESS_RJ45:
        return "rj45";
      case VG_EGRESS_WIFI:
        return "wifi";
      case VG_EGRESS_NONE:
      default:
        return "none";
    }
}

/**
  * @brief  TCP 后端枚举转日志字符串。
  * @param  b  传输后端。
  * @retval 静态 C 字符串（posix|lesp|none）。
  */
const char *vg_tcp_backend_str(vg_tcp_backend_t b)
{
  switch (b)
    {
      case VG_TCP_POSIX:
        return "posix";
      case VG_TCP_LESP:
        return "lesp";
      case VG_TCP_NONE:
      default:
        return "none";
    }
}
