/****************************************************************************
 * app/velaguard/host_tests/test_time_sync.c
 *
 * vg_sntp 纯函数用例：查询组包、应答解析（合法/告警位/模式/纪元）。
 ****************************************************************************/

#include "../vg_sntp.h"

#include <stdio.h>
#include <string.h>

static int g_fail;

static void expect(bool cond, const char *what)
{
  if (!cond)
    {
      fprintf(stderr, "FAIL: %s\n", what);
      g_fail++;
    }
}

static void put_be32(uint8_t *p, uint32_t v)
{
  p[0] = (uint8_t)(v >> 24);
  p[1] = (uint8_t)(v >> 16);
  p[2] = (uint8_t)(v >> 8);
  p[3] = (uint8_t)v;
}

static void test_build_query(void)
{
  uint8_t buf[48];

  memset(buf, 0xAA, sizeof(buf));
  expect(vg_sntp_build_query(buf) == 0, "build rc");
  expect(buf[0] == 0x23, "LI=0 VN=4 Mode=3");
  expect(buf[1] == 0 && buf[12] == 0 && buf[47] == 0, "rest zeroed");
  expect(vg_sntp_build_query(NULL) == -1, "build null");
}

static void test_parse_ok(void)
{
  uint8_t buf[48];
  int64_t utc = -1;

  memset(buf, 0, sizeof(buf));
  buf[0] = 0x24; /* LI=0 VN=4 Mode=4(server) */
  put_be32(&buf[32], (uint32_t)(2208988800ull + 1789000000ull));
  expect(vg_sntp_parse_response(buf, &utc) == 0, "parse rc");
  expect(utc == 1789000000, "utc value");

  /* broadcast 模式也接受 */
  buf[0] = 0x25;
  expect(vg_sntp_parse_response(buf, &utc) == 0, "parse bcast");
}

static void test_parse_reject(void)
{
  uint8_t buf[48];
  int64_t utc = -1;

  memset(buf, 0, sizeof(buf));
  buf[0] = 0x24;
  put_be32(&buf[32], (uint32_t)(2208988800ull + 1789000000ull));

  buf[0] = 0xE4; /* LI=3 告警/KoD */
  expect(vg_sntp_parse_response(buf, &utc) == -1, "reject LI=3");

  buf[0] = 0x23; /* Mode=3 客户端报文 */
  expect(vg_sntp_parse_response(buf, &utc) == -1, "reject mode=client");

  buf[0] = 0x24;
  put_be32(&buf[32], (uint32_t)2208988800ull); /* 1970 纪元 */
  expect(vg_sntp_parse_response(buf, &utc) == -1, "reject pre-epoch");

  expect(vg_sntp_parse_response(NULL, &utc) == -1, "reject null buf");
  expect(vg_sntp_parse_response(buf, NULL) == -1, "reject null out");
}

static void test_wall_math(void)
{
  expect(vg_time_utc_to_wall(1789000000) == 1789000000 + 28800,
         "utc->wall +8h");
  expect(vg_time_wall_plausible(VG_TIME_WALL_MIN), "wall min ok");
  expect(vg_time_wall_plausible(1789000000 + 28800), "wall 2026 ok");
  expect(!vg_time_wall_plausible(VG_TIME_WALL_MIN - 1), "wall below min");
  expect(!vg_time_wall_plausible(VG_TIME_WALL_MAX), "wall at max");
}

int main(void)
{
  test_build_query();
  test_parse_ok();
  test_parse_reject();
  test_wall_math();

  if (g_fail != 0)
    {
      fprintf(stderr, "test_time_sync: %d failure(s)\n", g_fail);
      return 1;
    }

  printf("test_time_sync: all passed\n");
  return 0;
}
