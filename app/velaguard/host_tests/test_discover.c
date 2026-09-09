#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../vg_discover.h"

static int expect_true(int cond, const char *msg)
{
  if (!cond)
    {
      fprintf(stderr, "FAIL: %s\n", msg);
      return 1;
    }

  return 0;
}

int main(void)
{
  int fails = 0;
  int amin;
  int amax;
  float v;

  fails += expect_true(vg_discover_parse_addr_range("1-8", &amin, &amax),
                       "parse 1-8");
  fails += expect_true(amin == 1 && amax == 8, "range 1-8 values");

  fails += expect_true(vg_discover_parse_addr_range("3", &amin, &amax),
                       "parse single");
  fails += expect_true(amin == 3 && amax == 3, "single addr");

  fails += expect_true(!vg_discover_parse_addr_range("0-1", &amin, &amax),
                       "reject addr 0");

  v = vg_discover_decode_int16_scaled(1000, 0.1f);
  fails += expect_true(v >= 99.9f && v <= 100.1f, "decode 1000*0.1");

  {
    const char *path = "points_read_slaves.json";
    FILE *fp;
    uint8_t addrs[8];
    int n;

    fp = fopen(path, "w");
    fails += expect_true(fp != NULL, "write temp points.json");
    if (fp != NULL)
      {
        fputs("{\"schema_version\":1,\"bus\":{\"device\":\"/dev/rs485\","
              "\"baud\":9600},\"hits\":[3,7,3],\"points\":["
              "{\"tag\":\"t1\",\"addr\":3,\"fc\":3,\"reg\":0,\"qty\":1,"
              "\"dtype\":\"int16\",\"scale\":0.100,\"unit\":\"C\"},"
              "{\"tag\":\"t2\",\"addr\":7,\"fc\":3,\"reg\":2,\"qty\":1,"
              "\"dtype\":\"int16\",\"scale\":0.100,\"unit\":\"C\"}]}\n",
              fp);
        fclose(fp);
      }

    n = vg_point_table_read_slaves(path, addrs, 8);
    fails += expect_true(n == 2, "hits unique count");
    fails += expect_true(n >= 2 && addrs[0] == 3 && addrs[1] == 7,
                         "hits order 3,7");
    remove(path);

    fp = fopen(path, "w");
    if (fp != NULL)
      {
        fputs("{\"schema_version\":1,\"points\":["
              "{\"tag\":\"a\",\"addr\":11,\"fc\":3,\"reg\":0,\"qty\":1,"
              "\"dtype\":\"int16\",\"scale\":0.100,\"unit\":\"C\"},"
              "{\"tag\":\"b\",\"addr\":11,\"fc\":3,\"reg\":2,\"qty\":1,"
              "\"dtype\":\"int16\",\"scale\":0.100,\"unit\":\"C\"},"
              "{\"tag\":\"c\",\"addr\":12,\"fc\":3,\"reg\":0,\"qty\":1,"
              "\"dtype\":\"int16\",\"scale\":0.100,\"unit\":\"C\"}]}\n",
              fp);
        fclose(fp);
      }

    n = vg_point_table_read_slaves(path, addrs, 8);
    fails += expect_true(n == 2, "points unique count");
    fails += expect_true(n >= 2 && addrs[0] == 11 && addrs[1] == 12,
                         "points unique 11,12");
    remove(path);

    fails += expect_true(vg_point_table_read_slaves("no-such-points.json",
                                                    addrs, 8) < 0,
                         "missing file");
  }

  if (fails == 0)
    {
      printf("test_discover: OK\n");
      return 0;
    }

  fprintf(stderr, "test_discover: %d failure(s)\n", fails);
  return 1;
}
