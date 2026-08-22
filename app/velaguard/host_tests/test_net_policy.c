/****************************************************************************
 * app/velaguard/host_tests/test_net_policy.c
 *
 * 主机侧策略单测（AC1–AC7）。直接链接 vg_net_policy.c，无 NuttX。
 ****************************************************************************/

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "vg_net_policy.h"

static int g_fail;

/**
  * @brief  断言辅助：失败时累加 g_fail 并打印消息。
  * @param  cond  非 0 为通过。
  * @param  msg   失败说明。
  * @retval None
  */
static void expect(int cond, const char *msg)
{
  if (!cond)
    {
      fprintf(stderr, "FAIL: %s\n", msg);
      g_fail++;
    }
}

/**
  * @brief  构造「仅 RJ45 健康」采样。
  */
static struct vg_net_sample s_rj45(void)
{
  struct vg_net_sample s;
  memset(&s, 0, sizeof(s));
  s.rj45_link = true;
  s.rj45_has_ip = true;
  s.rj45_ping_ok = true;
  s.rj45_ping_sampled = true;
  return s;
}

/**
  * @brief  构造「RJ45 + Wi-Fi 皆可用」采样。
  */
static struct vg_net_sample s_both(void)
{
  struct vg_net_sample s = s_rj45();
  s.wifi_assoc = true;
  s.wifi_has_ip = true;
  return s;
}

/**
  * @brief  构造「仅 Wi-Fi 可用」采样。
  */
static struct vg_net_sample s_wifi_only(void)
{
  struct vg_net_sample s;
  memset(&s, 0, sizeof(s));
  s.wifi_assoc = true;
  s.wifi_has_ip = true;
  s.rj45_ping_sampled = true;
  return s;
}

/**
  * @brief  构造「双挂」采样。
  */
static struct vg_net_sample s_none(void)
{
  struct vg_net_sample s;
  memset(&s, 0, sizeof(s));
  s.rj45_ping_sampled = true;
  return s;
}

/**
  * @brief  AC1：RJ45 ping 健康 → egress=rj45，tcp_backend=posix。
  * @retval None
  */
static void test_ac1(void)
{
  struct vg_net_policy p;
  struct vg_net_sample s = s_rj45();

  vg_net_policy_init(&p);
  vg_net_policy_step(&p, 0, &s);
  expect(p.active_egress == VG_EGRESS_RJ45, "AC1 egress rj45");
  expect(p.state == VG_NET_ONLINE_RJ45, "AC1 state");
  expect(p.tcp_backend == VG_TCP_POSIX, "AC1 posix");
}

/**
  * @brief  AC2：连续 ping 失败且 Wi-Fi 可用 → egress=wifi，backend=lesp。
  * @note   须置 tcp_reconnect；同时只选一条出口。
  * @retval None
  */
static void test_ac2(void)
{
  struct vg_net_policy p;
  struct vg_net_sample s;
  int i;

  vg_net_policy_init(&p);
  s = s_both();
  vg_net_policy_step(&p, 0, &s);
  expect(p.active_egress == VG_EGRESS_RJ45, "AC2 start rj45");

  /* Drive fail streak to VG_NET_PING_FAIL_N */

  s = s_both();
  s.rj45_ping_ok = false;
  for (i = 0; i < VG_NET_PING_FAIL_N; i++)
    {
      vg_net_policy_step(&p, (uint64_t)(i + 1) * 1000, &s);
    }

  expect(p.active_egress == VG_EGRESS_WIFI, "AC2 failover wifi");
  expect(p.state == VG_NET_ONLINE_WIFI, "AC2 state wifi");
  expect(p.tcp_backend == VG_TCP_LESP, "AC2 lesp");
  expect(p.tcp_reconnect, "AC2 reconnect on switch");
}

/**
  * @brief  AC3：双挂 → NET_DOWN，无 TCP。
  * @retval None
  */
static void test_ac3(void)
{
  struct vg_net_policy p;
  struct vg_net_sample s = s_none();
  int i;

  vg_net_policy_init(&p);
  for (i = 0; i < VG_NET_PING_FAIL_N; i++)
    {
      vg_net_policy_step(&p, (uint64_t)i * 1000, &s);
    }

  expect(p.active_egress == VG_EGRESS_NONE, "AC3 none");
  expect(p.state == VG_NET_DOWN, "AC3 down");
  expect(p.tcp_backend == VG_TCP_NONE, "AC3 no tcp");
}

/**
  * @brief  AC4：恢复未满窗口保持 lesp；满窗口切回 posix；flap 不抖。
  * @retval None
  */
static void test_ac4(void)
{
  struct vg_net_policy p;
  struct vg_net_sample s;
  int i;

  vg_net_policy_init(&p);
  s = s_wifi_only();
  for (i = 0; i < VG_NET_PING_FAIL_N; i++)
    {
      vg_net_policy_step(&p, (uint64_t)i * 1000, &s);
    }

  expect(p.active_egress == VG_EGRESS_WIFI, "AC4 on wifi");

  /* Inside hold window: stay on Wi-Fi even though RJ45 looks healthy */

  s = s_both();
  vg_net_policy_step(&p, 5000, &s);
  expect(p.active_egress == VG_EGRESS_WIFI, "AC4 hold window");
  expect(!p.tcp_reconnect, "AC4 no reconnect in window");

  vg_net_policy_step(&p, 5000 + VG_NET_RJ45_HOLD_MS, &s);
  expect(p.active_egress == VG_EGRESS_RJ45, "AC4 after hold");
  expect(p.tcp_backend == VG_TCP_POSIX, "AC4 back posix");
  expect(p.tcp_reconnect, "AC4 reconnect on recover");

  /* Flap inside a new wifi period must not thrash back to RJ45 */

  s = s_wifi_only();
  for (i = 0; i < VG_NET_PING_FAIL_N; i++)
    {
      vg_net_policy_step(&p, 20000 + (uint64_t)i * 1000, &s);
    }

  s = s_both();
  vg_net_policy_step(&p, 25000, &s);
  s = s_wifi_only();
  vg_net_policy_step(&p, 26000, &s);
  s = s_both();
  vg_net_policy_step(&p, 27000, &s);
  expect(p.active_egress == VG_EGRESS_WIFI, "AC4 flap stays wifi");
}

/**
  * @brief  AC5：指数退避、link 上升沿立即探测、未采样不计 streak。
  * @retval None
  */
static void test_ac5(void)
{
  uint32_t b0 = vg_net_policy_backoff_ms(0, 0);
  uint32_t b1 = vg_net_policy_backoff_ms(1, 0);
  uint32_t b2 = vg_net_policy_backoff_ms(2, 0);
  uint32_t b10 = vg_net_policy_backoff_ms(10, 0);
  struct vg_net_policy p;
  struct vg_net_sample s = s_none();
  uint64_t next;
  unsigned exp;
  int i;

  expect(b0 == 1000, "AC5 backoff 1s");
  expect(b1 == 2000, "AC5 backoff 2s");
  expect(b2 == 4000, "AC5 backoff 4s");
  expect(b10 == 60000, "AC5 backoff cap");

  vg_net_policy_init(&p);
  vg_net_policy_step(&p, 0, &s);
  expect(p.next_rj45_probe_ms >= 1000, "AC5 schedules backoff");

  /* Link rising edge clears deferred probe */

  s.rj45_link = true;
  vg_net_policy_step(&p, 100, &s);
  expect(p.next_rj45_probe_ms <= 100, "AC5 link rise immediate probe");

  vg_net_policy_init(&p);
  s = s_rj45();
  s.rj45_ping_ok = false;
  s.rj45_ping_sampled = false;
  vg_net_policy_step(&p, 0, &s);
  expect(p.rj45_ping_fail_streak == 0, "AC5 unsampled ping does not count");
  expect(p.active_egress == VG_EGRESS_RJ45, "AC5 unsampled keeps rj45");

  /* Cable up, gateway dead: waiting ticks must not stretch backoff. */

  vg_net_policy_init(&p);
  s = s_both();
  s.rj45_ping_ok = false;
  for (i = 0; i < VG_NET_PING_FAIL_N; i++)
    {
      vg_net_policy_step(&p, (uint64_t)i * 1000, &s);
    }

  expect(p.active_egress == VG_EGRESS_WIFI, "AC5b failover wifi");
  next = p.next_rj45_probe_ms;
  exp = p.rj45_backoff_exp;
  expect(next > 2000, "AC5b probe deferred after sampled fails");

  s.rj45_ping_sampled = false;
  for (i = 0; i < 8; i++)
    {
      vg_net_policy_step(&p, 2100 + (uint64_t)i * 50, &s);
    }

  expect(p.next_rj45_probe_ms == next, "AC5b wait does not extend probe");
  expect(p.rj45_backoff_exp == exp, "AC5b wait does not raise exp");
}

/**
  * @brief  AC6/AC7：Wi-Fi 失败达阈值 → request_esp_reset；切出口置 reconnect。
  * @retval None
  */
static void test_ac6(void)
{
  struct vg_net_policy p;
  int i;

  vg_net_policy_init(&p);
  for (i = 0; i < VG_NET_ESP_RESET_TRIES - 1; i++)
    {
      vg_net_policy_note_wifi_join(&p, false);
      expect(!p.request_esp_reset, "AC6 no reset yet");
    }

  vg_net_policy_note_wifi_join(&p, false);
  expect(p.request_esp_reset, "AC6 reset at threshold");

  /* Counter cleared after latching the reset request */

  vg_net_policy_note_wifi_join(&p, false);
  expect(!p.request_esp_reset, "AC6 counter cleared");
}

/**
  * @brief  AC6/7 TCP：出口变化产生 tcp_reconnect，backend 切到 lesp。
  * @retval None
  */
static void test_ac7_tcp(void)
{
  struct vg_net_policy p;
  struct vg_net_sample s;
  vg_tcp_backend_t old_backend;
  int i;

  vg_net_policy_init(&p);
  s = s_both();
  vg_net_policy_step(&p, 0, &s);
  old_backend = p.tcp_backend;
  expect(old_backend == VG_TCP_POSIX, "AC6/7 start posix");

  s = s_both();
  s.rj45_ping_ok = false;
  for (i = 0; i < VG_NET_PING_FAIL_N; i++)
    {
      vg_net_policy_step(&p, (uint64_t)(i + 1) * 1000, &s);
    }

  expect(p.tcp_backend == VG_TCP_LESP, "switch to lesp");
  expect(p.tcp_reconnect, "reconnect flag");
}

/**
  * @brief  主机单测入口：跑 AC1–AC7。
  * @retval 0  全部通过。
  * @retval 1  有断言失败。
  */
int main(void)
{
  test_ac1();
  test_ac2();
  test_ac3();
  test_ac4();
  test_ac5();
  test_ac6();
  test_ac7_tcp();

  if (g_fail)
    {
      fprintf(stderr, "%d assertion(s) failed\n", g_fail);
      return 1;
    }

  printf("test_net_policy: all passed\n");
  return 0;
}
