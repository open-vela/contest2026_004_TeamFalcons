/****************************************************************************
 * app/velaguard/host_tests/test_voice_intent.c
 *
 * BVO-10: the closed three-intent set, point resolution and the rule that a
 * homophone is normalised only when it lands on an existing id.
 ****************************************************************************/

#include <stdio.h>
#include <string.h>

#include "vg_voice_intent.h"

static int g_fail;

#define CHECK(cond) \
  do \
    { \
      if (!(cond)) \
        { \
          printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
          g_fail++; \
        } \
    } \
  while (0)

static void make_points(struct vg_agent_point *p, int *n)
{
  memset(p, 0, sizeof(*p) * 6);

  snprintf(p[0].id, sizeof(p[0].id), "ups_load");
  snprintf(p[0].name, sizeof(p[0].name), "UPS负载");
  snprintf(p[0].unit, sizeof(p[0].unit), "%s", "%");
  p[0].has_sample = true;
  p[0].ok = true;
  p[0].value = 45.0f;

  snprintf(p[1].id, sizeof(p[1].id), "ups_temp");
  snprintf(p[1].name, sizeof(p[1].name), "温度");
  snprintf(p[1].unit, sizeof(p[1].unit), "C");
  p[1].has_sample = true;
  p[1].ok = true;
  p[1].value = 30.0f;

  snprintf(p[2].id, sizeof(p[2].id), "bms_temp");
  snprintf(p[2].name, sizeof(p[2].name), "温度");
  snprintf(p[2].unit, sizeof(p[2].unit), "C");
  p[2].has_sample = true;
  p[2].ok = true;
  p[2].value = 31.0f;

  snprintf(p[3].id, sizeof(p[3].id), "bms_01");
  snprintf(p[3].name, sizeof(p[3].name), "电池01");
  p[3].has_sample = true;
  p[3].ok = true;
  p[3].value = 12.0f;

  *n = 4;
}

static void test_classify(void)
{
  CHECK(vg_vi_classify("现在有哪些异常", NULL) == VG_VI_ANOMALY);
  CHECK(vg_vi_classify("有没有告警", NULL) == VG_VI_ANOMALY);
  CHECK(vg_vi_classify("UPS负载是多少", NULL) == VG_VI_POINT);
  CHECK(vg_vi_classify("UPS负载恢复了吗", NULL) == VG_VI_RECOVERY);
  CHECK(vg_vi_classify("把UPS负载设置成50", NULL) == VG_VI_REFUSED);
  CHECK(vg_vi_classify("清除所有告警", NULL) == VG_VI_REFUSED);
  CHECK(vg_vi_classify("", NULL) == VG_VI_UNKNOWN);
  CHECK(vg_vi_classify(NULL, NULL) == VG_VI_UNKNOWN);

  /* A control word wins over the noun it shares with a query: the voice
   * path cannot perform the action, and answering a reading would imply it
   * had. */

  CHECK(vg_vi_classify("关闭数字量输出", NULL) == VG_VI_REFUSED);

  {
    struct vg_voice_intent_result res;

    CHECK(vg_vi_classify("查一下UPS负载的数值", &res) == VG_VI_POINT);
    CHECK(res.has_token);
    CHECK(strcmp(res.token, "UPS负载") == 0);
  }
}

static void test_token(void)
{
  char buf[64];

  CHECK(vg_vi_point_token("请问 UPS负载 是多少", buf, sizeof(buf)) > 0);
  CHECK(strcmp(buf, "UPS负载") == 0);

  CHECK(vg_vi_point_token("现在电池温度怎么样", buf, sizeof(buf)) > 0);
  CHECK(strcmp(buf, "电池温度") == 0);

  /* Middle text is not a filler and must survive. */

  CHECK(vg_vi_point_token("UPS负载是多少", buf, sizeof(buf)) > 0);
  CHECK(strcmp(buf, "UPS负载") == 0);

  CHECK(vg_vi_point_token("", buf, sizeof(buf)) == 0);
  CHECK(buf[0] == '\0');

  /* Too small a buffer yields no token rather than a truncated name. */

  {
    char tiny[8];

    CHECK(vg_vi_point_token("UPS负载很长很长", tiny, sizeof(tiny)) == 0);
    CHECK(tiny[0] == '\0');
  }
}

static void test_digits(void)
{
  char buf[32];

  CHECK(vg_vi_normalize_digits("零幺", buf, sizeof(buf)) > 0);
  CHECK(strcmp(buf, "01") == 0);

  CHECK(vg_vi_normalize_digits("bms_零一", buf, sizeof(buf)) > 0);
  CHECK(strcmp(buf, "bms_01") == 0);

  CHECK(vg_vi_normalize_digits("ups_load", buf, sizeof(buf)) > 0);
  CHECK(strcmp(buf, "ups_load") == 0);
}

static void test_resolve(void)
{
  struct vg_agent_point p[6];
  struct vg_agent_query_result res;
  char token[64];
  int n;

  make_points(p, &n);

  /* Exact id. */

  CHECK(vg_vi_resolve_point("ups_load", p, n, &res, token, sizeof(token)) ==
        VG_AGENT_QUERY_OK);
  CHECK(res.index == 0);
  CHECK(strcmp(token, "ups_load") == 0);

  /* Exact name. */

  CHECK(vg_vi_resolve_point("查一下 UPS负载 是多少", p, n, &res, token,
                            sizeof(token)) == VG_AGENT_QUERY_OK);
  CHECK(res.index == 0);

  /* Two points share the name 温度: ambiguous, not first match. */

  CHECK(vg_vi_resolve_point("温度是多少", p, n, &res, token, sizeof(token)) ==
        VG_AGENT_QUERY_AMBIG);
  CHECK(res.cand_n == 2);

  /* The id disambiguates. */

  CHECK(vg_vi_resolve_point("bms_temp 是多少", p, n, &res, token,
                            sizeof(token)) == VG_AGENT_QUERY_OK);
  CHECK(res.index == 2);

  /* Homophone id: 零一 becomes 01 and lands on bms_01. */

  CHECK(vg_vi_resolve_point("bms_零幺 是多少", p, n, &res, token,
                            sizeof(token)) == VG_AGENT_QUERY_OK);
  CHECK(res.index == 3);

  /* A homophone that matches no id stays not-found; it must not be guessed
   * into a different point. */

  CHECK(vg_vi_resolve_point("ups_零九", p, n, &res, token, sizeof(token)) ==
        VG_AGENT_QUERY_NOT_FOUND);

  /* Unknown name. */

  CHECK(vg_vi_resolve_point("风机转速", p, n, &res, token, sizeof(token)) ==
        VG_AGENT_QUERY_NOT_FOUND);

  /* A name ending in a filler word still resolves: the stripper retries the
   * untouched sentence before reporting not-found. */

  {
    struct vg_agent_point q[1];

    memset(q, 0, sizeof(q));
    snprintf(q[0].id, sizeof(q[0].id), "x1");
    snprintf(q[0].name, sizeof(q[0].name), "备用电池");
    q[0].has_sample = true;
    q[0].ok = true;

    CHECK(vg_vi_resolve_point("备用电池", q, 1, &res, token, sizeof(token)) ==
          VG_AGENT_QUERY_OK);
    CHECK(res.index == 0);
  }

  /* Bad arguments. */

  CHECK(vg_vi_resolve_point(NULL, p, n, &res, token, sizeof(token)) ==
        VG_AGENT_QUERY_ARG);
  CHECK(vg_vi_resolve_point("ups_load", NULL, n, &res, token,
                            sizeof(token)) == VG_AGENT_QUERY_ARG);
}

int main(void)
{
  test_classify();
  test_token();
  test_digits();
  test_resolve();

  if (g_fail == 0)
    {
      printf("test_voice_intent: ok\n");
      return 0;
    }

  printf("test_voice_intent: %d failure(s)\n", g_fail);
  return 1;
}
