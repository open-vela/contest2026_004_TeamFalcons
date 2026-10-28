#include <errno.h>
#include <math.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/resource.h>
#include <unistd.h>

#include "../vg_discover.h"

static int failures;
#define CHECK(c, msg) do { if (!(c)) { fprintf(stderr, "FAIL: %s\n", msg); failures++; } } while (0)

static void valid_table(struct vg_discover_summary *s)
{
  memset(s, 0, sizeof(*s));
  s->baud = 9600;
  strcpy(s->devpath, "/dev/rs485");
  s->n_points = 1;
  strcpy(s->points[0].id, "temp");
  strcpy(s->points[0].name, "温度");
  strcpy(s->points[0].dtype, "int16");
  s->points[0].addr = 1;
  s->points[0].fc = 3;
  s->points[0].qty = 1;
  s->points[0].scale = 0.000123456789f;
  s->points[0].fail_n = 3;
}

static void *replace_live(void *tables)
{
  struct vg_discover_summary *pair = tables;
  for (int i = 0; i < 10000; i++) vg_live_points_replace(&pair[i % 2]);
  return NULL;
}

int main(void)
{
  struct vg_discover_summary s;
  struct vg_discover_summary out;
  struct rlimit saved;
  struct rlimit small;
  FILE *fp;
  int lo;
  int hi;
  const char *path = "maintenance-points.json";

  CHECK(!vg_discover_parse_addr_range("1-3junk", &lo, &hi), "range rejects suffix");
  CHECK(!vg_discover_parse_addr_range("2junk", &lo, &hi), "address rejects suffix");
  valid_table(&s);
  CHECK(vg_point_table_write_candidate(&s, path) == 0, "seed table");
  CHECK(vg_point_table_read(&out, path) == 0 &&
        out.points[0].scale == s.points[0].scale, "scale preserves float precision");

  getrlimit(RLIMIT_FSIZE, &saved);
  small = saved;
  small.rlim_cur = 64;
  signal(SIGXFSZ, SIG_IGN);
  setrlimit(RLIMIT_FSIZE, &small);
  CHECK(vg_point_table_write_candidate(&s, path) < 0, "short write fails");
  setrlimit(RLIMIT_FSIZE, &saved);
  CHECK(vg_point_table_read(&out, path) == 0 && out.n_points == 1,
        "failed replacement preserves old table");

  s.points[0].scale = NAN;
  CHECK(vg_point_table_write_candidate(&s, path) < 0, "NaN cannot be persisted");
  valid_table(&s);
  strcpy(s.points[0].unit, "C\"\\");
  CHECK(vg_point_table_write_candidate(&s, path) == 0 &&
        vg_point_table_read(&out, path) == 0 &&
        strcmp(out.points[0].unit, s.points[0].unit) == 0, "JSON string escape roundtrip");

  fp = fopen(path, "w");
  fputs("{\"points\":[{\"id\":\"bad\",\"addr\":999,\"reg\":0}]}", fp);
  fclose(fp);
  CHECK(vg_point_table_read(&out, path) < 0 && out.n_points == 0,
        "invalid point rejects entire table");
  fp = fopen(path, "w");
  fputs("{\"points\":[", fp);
  fclose(fp);
  CHECK(vg_point_table_read(&out, path) < 0, "truncated JSON is not empty table");
  valid_table(&s);
  for (int i = 0; i < VG_DISCOVER_MAX_POINTS; i++) {
    s.points[i] = s.points[0];
    snprintf(s.points[i].id, sizeof(s.points[i].id), "point_%d", i);
    memset(s.points[i].name, 'a', sizeof(s.points[i].name) - 1);
    s.points[i].name[sizeof(s.points[i].name) - 1] = 0;
    s.points[i].has_warn = s.points[i].has_crit = 1;
    s.points[i].warn = 1.23456789e20f;
    s.points[i].crit = 1.23456789e30f;
    strcpy(s.points[i].cmp, "ge");
  }
  s.n_points = VG_DISCOVER_MAX_POINTS;
  CHECK(vg_point_table_write_candidate(&s, path) == 0 &&
        vg_point_table_read(&out, path) == 0 && out.n_points == s.n_points,
        "full capacity and long names roundtrip");
  struct vg_live_snapshot snap;
  fp = fopen(path, "w");
  fputs("tick_ms=10\nn=1\n", fp); fclose(fp);
  CHECK(vg_live_snapshot_read(path, &snap) < 0, "incomplete snapshot is an error");
  CHECK(vg_bus_try_lock() == 0, "acquire bus");
  uint32_t start = vg_live_now_ms();
  CHECK(vg_bus_lock_timeout(20) == -EBUSY && vg_live_now_ms() - start >= 20,
        "bus wait is bounded");
  vg_bus_unlock();
  CHECK(vg_bus_lock_timeout(20) == 0, "released bus is reusable");
  vg_bus_unlock();
  vg_live_points_replace(&s);
  CHECK(vg_live_points_load("nonexistent-maintenance-file") < 0 &&
        vg_live_points_copy(&out) == s.n_points, "failed reload preserves RAM table");
  struct vg_discover_summary pair[2];
  valid_table(&pair[0]); valid_table(&pair[1]);
  pair[0].points[0].scale = 1;
  pair[1].points[0].scale = 2;
  pair[1].n_points = 2;
  pair[1].points[1] = pair[1].points[0];
  strcpy(pair[1].points[1].id, "second");
  vg_live_points_replace(&pair[0]);
  pthread_t worker;
  CHECK(pthread_create(&worker, NULL, replace_live, pair) == 0, "start concurrent table writer");
  for (int i = 0; i < 10000; i++) {
    uint32_t gen;
    int n = vg_live_points_copy_versioned(&out, &gen);
    CHECK((n == 1 || n == 2) && out.points[0].scale == n && gen,
          "concurrent table and generation form one snapshot");
  }
  pthread_join(worker, NULL);
  memset(&s, 0, sizeof(s));
  CHECK(vg_point_table_apply(&s, path, NULL, true) < 0,
        "empty discovery result cannot replace table");
  unlink(path);
  unlink("maintenance-points.json.bak");
  unlink("maintenance-points.json.tmp");
  printf("test_maintenance: %d failure(s)\n", failures);
  return failures != 0;
}
