#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

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
  struct vg_discover_summary sum;
  char line[256];
  const char *path = "vgpoint_roundtrip.json";
  FILE *fp;
  int idx;
  char *argv_ok[] = {"vgpoint", "add", "-i", "temp", "-a", "1", "-r", "0"};
  char *argv_long[] =
    {
      "vgpoint", "add", "-i", "temp", "-a", "1", "-r", "0",
      "-u", "CelsiusX", "-k", "ge", "-w", "40.5", "-C", "55.25",
      "-n", "3", "-d", "int16", "-s", "0.1"
    };

  fails += expect_true(vg_point_validate_id("temp"), "id temp");
  fails += expect_true(!vg_point_validate_id("temp-1"), "reject hyphen id");
  fails += expect_true(!vg_point_validate_id(""), "reject empty id");
  fails += expect_true(vg_point_validate_name("温度"), "utf8 name");
  fails += expect_true(!vg_point_validate_name("a b"), "reject space name");
  fails += expect_true(!vg_point_validate_name("a=b"), "reject eq name");
  fails += expect_true(vg_point_cmdline_len(8, argv_ok) <= 120, "short cmd");
  fails += expect_true(vg_point_cmdline_len(21, argv_long) > 0, "long cmd len");

  vg_point_format_err(line, sizeof(line), "apply", "need_confirm",
                      "need_confirm");
  fails += expect_true(strstr(line, "vgpoint: ERR cmd=apply code=need_confirm")
                       != NULL,
                       "need_confirm line");

  vg_point_format_ok(line, sizeof(line), "list", "committed", 0);
  fails += expect_true(strncmp(line, "vgpoint: OK ", 12) == 0, "ok prefix");

  memset(&sum, 0, sizeof(sum));
  sum.baud = 9600;
  snprintf(sum.devpath, sizeof(sum.devpath), "/dev/rs485");
  snprintf(sum.points[0].id, sizeof(sum.points[0].id), "temp");
  snprintf(sum.points[0].name, sizeof(sum.points[0].name), "温度");
  sum.points[0].addr = 1;
  sum.points[0].fc = 3;
  sum.points[0].reg = 0;
  sum.points[0].qty = 1;
  snprintf(sum.points[0].dtype, sizeof(sum.points[0].dtype), "int16");
  sum.points[0].scale = 0.1f;
  snprintf(sum.points[0].unit, sizeof(sum.points[0].unit), "C");
  snprintf(sum.points[0].cmp, sizeof(sum.points[0].cmp), "ge");
  sum.points[0].has_warn = 1;
  sum.points[0].warn = 40.0f;
  sum.points[0].has_crit = 1;
  sum.points[0].crit = 55.0f;
  sum.points[0].fail_n = 3;
  snprintf(sum.points[1].id, sizeof(sum.points[1].id), "flood");
  snprintf(sum.points[1].name, sizeof(sum.points[1].name), "水浸");
  sum.points[1].addr = 2;
  sum.points[1].fc = 3;
  sum.points[1].reg = 2;
  sum.points[1].qty = 1;
  snprintf(sum.points[1].dtype, sizeof(sum.points[1].dtype), "uint16");
  sum.points[1].scale = 1.0f;
  snprintf(sum.points[1].cmp, sizeof(sum.points[1].cmp), "eq");
  sum.points[1].has_crit = 1;
  sum.points[1].crit = 1.0f;
  sum.points[1].fail_n = 3;
  snprintf(sum.points[2].id, sizeof(sum.points[2].id), "flood2");
  snprintf(sum.points[2].name, sizeof(sum.points[2].name), "水浸");
  sum.points[2].addr = 3;
  sum.points[2].fc = 3;
  sum.points[2].reg = 2;
  sum.points[2].qty = 1;
  snprintf(sum.points[2].dtype, sizeof(sum.points[2].dtype), "uint16");
  sum.points[2].scale = 1.0f;
  sum.points[2].fail_n = 3;
  sum.n_points = 3;

  fails += expect_true(vg_point_table_write_candidate(&sum, path) == 0,
                       "write json");
  fp = fopen(path, "r");
  fails += expect_true(fp != NULL, "open written json");
  if (fp != NULL)
    {
      char body[2048];
      size_t n = fread(body, 1, sizeof(body) - 1, fp);
      body[n] = '\0';
      fclose(fp);
      fails += expect_true(strstr(body, "\"warn\":") != NULL, "warn present");
      fails += expect_true(strstr(body, "\"null\"") == NULL, "no null token");
      fails += expect_true(strstr(body, "\"id\":\"flood\"") != NULL, "flood id");
      fails += expect_true(strstr(body, "\"name\":\"水浸\"") != NULL,
                           "cn name");
      fails += expect_true(strstr(body, "\"tag\"") == NULL, "no tag key");
    }

  memset(&sum, 0, sizeof(sum));
  fails += expect_true(vg_point_table_read(&sum, path) == 0, "read json");
  fails += expect_true(sum.n_points == 3, "three points");
  idx = vg_point_table_find_id(&sum, "temp");
  fails += expect_true(idx == 0, "find temp");
  fails += expect_true(strcmp(sum.points[0].name, "温度") == 0, "temp name");
  fails += expect_true(sum.points[0].has_warn && sum.points[0].warn > 39.0f,
                       "temp warn");
  fails += expect_true(sum.points[1].has_crit && !sum.points[1].has_warn,
                       "flood crit only");
  fails += expect_true(strcmp(sum.points[1].cmp, "eq") == 0, "flood cmp");
  fails += expect_true(strcmp(sum.points[1].name, sum.points[2].name) == 0,
                       "dup name ok");
  fails += expect_true(vg_point_table_find_id(&sum, "水浸") < 0,
                       "name is not key");

  vg_point_format_point(line, sizeof(line), &sum.points[0]);
  fails += expect_true(strstr(line, "vgpoint: POINT id=temp") != NULL,
                       "point line id");
  fails += expect_true(strstr(line, "name=温度") != NULL, "point line name");
  fails += expect_true(strstr(line, "tag=") == NULL, "no tag= in point");
  fails += expect_true(strstr(line, "cmp=ge") != NULL, "point cmp");

  {
    const char *oldp = "vgpoint_old.json";
    fp = fopen(oldp, "w");
    fails += expect_true(fp != NULL, "old json");
    if (fp != NULL)
      {
        fputs("{\"schema_version\":1,\"points\":["
              "{\"tag\":\"t1\",\"addr\":3,\"fc\":3,\"reg\":0,\"qty\":1,"
              "\"dtype\":\"int16\",\"scale\":0.100,\"unit\":\"C\"}]}\n",
              fp);
        fclose(fp);
      }

    memset(&sum, 0, sizeof(sum));
    fails += expect_true(vg_point_table_read(&sum, oldp) == 0, "read old");
    fails += expect_true(sum.n_points == 0, "old tag-only dropped");
    remove(oldp);
  }

  {
    const char *noidp = "vgpoint_id_only.json";
    fp = fopen(noidp, "w");
    fails += expect_true(fp != NULL, "id-only json");
    if (fp != NULL)
      {
        fputs("{\"schema_version\":1,\"points\":["
              "{\"id\":\"t1\",\"addr\":3,\"fc\":3,\"reg\":0,\"qty\":1,"
              "\"dtype\":\"int16\",\"scale\":0.100,\"unit\":\"C\"}]}\n",
              fp);
        fclose(fp);
      }

    memset(&sum, 0, sizeof(sum));
    fails += expect_true(vg_point_table_read(&sum, noidp) == 0, "read id-only");
    fails += expect_true(sum.n_points == 1, "id-only n");
    fails += expect_true(strcmp(sum.points[0].name, "t1") == 0,
                         "name defaults to id");
    fails += expect_true(sum.points[0].cmp[0] == '\0', "id-only cmp empty");
    fails += expect_true(!sum.points[0].has_warn && !sum.points[0].has_crit,
                         "id-only no thresh");
    remove(noidp);
  }

  vg_live_points_replace(&sum);
  fails += expect_true(vg_live_points_gen() != 0, "live gen");

  {
    const char *emptyp = "vgpoint_empty.json";
    memset(&sum, 0, sizeof(sum));
    sum.baud = 9600;
    snprintf(sum.devpath, sizeof(sum.devpath), "/dev/rs485");
    fails += expect_true(vg_point_table_write_candidate(&sum, emptyp) == 0,
                         "write empty");
    memset(&sum, 0, sizeof(sum));
    fails += expect_true(vg_point_table_read(&sum, emptyp) == 0, "read empty");
    fails += expect_true(sum.n_points == 0, "empty n");
    remove(emptyp);
  }

  remove(path);
  if (fails == 0)
    {
      printf("test_vgpoint: OK\n");
      return 0;
    }

  fprintf(stderr, "test_vgpoint: %d failure(s)\n", fails);
  return 1;
}
