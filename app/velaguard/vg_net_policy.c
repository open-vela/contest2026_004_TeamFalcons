/****************************************************************************
 * app/velaguard/vg_net_policy.c
 *
 * 纯策略实现：无 NuttX / 套接字。主机单测直接链本文件。
 * API 说明见 vg_net_policy.h。
 ****************************************************************************/

#include "vg_net_policy.h"

#include <string.h>

static uint32_t clamp_backoff(uint32_t ms)
{
  if (ms > VG_NET_BACKOFF_MAX_MS)
    {
      return VG_NET_BACKOFF_MAX_MS;
    }

  return ms;
}

/**
 * @brief 计算指数退避毫秒数：base×2^exp，封顶 VG_NET_BACKOFF_MAX_MS。
 * @note jitter_pct 预留；当前故意忽略 RNG。详见头文件。
 */
uint32_t vg_net_policy_backoff_ms(unsigned exp, int jitter_pct)
{
  uint32_t ms = VG_NET_BACKOFF_BASE_MS;
  unsigned i;

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
 * @brief 清零并标记策略已初始化。
 */
void vg_net_policy_init(struct vg_net_policy *p)
{
  memset(p, 0, sizeof(*p));
  p->jitter_pct = 0;
  p->initialized = true;
}

static bool rj45_raw_ok(const struct vg_net_sample *s)
{
  return s->rj45_link && s->rj45_has_ip && s->rj45_ping_ok;
}

static bool wifi_raw_ok(const struct vg_net_sample *s)
{
  return s->wifi_assoc && s->wifi_has_ip;
}

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
 * @brief 记录 Wi-Fi join 结果；失败达 VG_NET_ESP_RESET_TRIES 置 request_esp_reset。
 */
void vg_net_policy_note_wifi_join(struct vg_net_policy *p, bool ok)
{
  p->request_esp_reset = false;
  if (ok)
    {
      p->wifi_fail_streak = 0;
      p->wifi_backoff_exp = 0;
      return;
    }

  p->wifi_fail_streak++;
  if (p->wifi_fail_streak >= VG_NET_ESP_RESET_TRIES)
    {
      p->request_esp_reset = true;
      p->wifi_fail_streak = 0;
    }

  if (p->wifi_backoff_exp < 16)
    {
      p->wifi_backoff_exp++;
    }
}

/**
 * @brief 一拍策略步进：更新健康判定、活动出口、tcp_backend、tcp_reconnect。
 * @note 详见 vg_net_policy.h；切出口本身由 vg_net_mgr 执行关旧开新。
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

  if (link_rise)
    {
      p->next_rj45_probe_ms = now_ms;
    }

  apply_inject(p->inject_rj45, rj45_raw_ok(sample), &healthy_rj45);
  apply_inject(p->inject_wifi, wifi_raw_ok(sample), &healthy_wifi);

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
      /* Keep eth0 probes running during hysteresis and the 10s hold. */

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
