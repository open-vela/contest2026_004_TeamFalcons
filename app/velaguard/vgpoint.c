/****************************************************************************
 * app/velaguard/vgpoint.c
 *
 * NSH: host serial point-table editor (candidate / test / apply --confirm).
 ****************************************************************************/

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <nuttx/config.h>

#include "vg_discover.h"

#ifdef CONFIG_VG_CONFIG_STORE
#include "vg_config_store.h"
#endif

#ifndef CONFIG_VG_DISCOVER_DEVPATH
#  define CONFIG_VG_DISCOVER_DEVPATH "/dev/rs485"
#endif

#ifndef CONFIG_VG_DISCOVER_BAUD
#  define CONFIG_VG_DISCOVER_BAUD 9600
#endif

#ifndef CONFIG_VG_DISCOVER_INTER_MS
#  define CONFIG_VG_DISCOVER_INTER_MS 50
#endif

#ifndef CONFIG_VG_DISCOVER_CANDIDATE_PATH
#  define CONFIG_VG_DISCOVER_CANDIDATE_PATH \
          "/data/velaguard/discover/point_table_candidate.json"
#endif

#ifndef CONFIG_VG_DISCOVER_POINTS_PATH
#  define CONFIG_VG_DISCOVER_POINTS_PATH "/data/velaguard/config/points.json"
#endif

#ifndef CONFIG_VG_CONFIG_BASEDIR
#  define CONFIG_VG_CONFIG_BASEDIR "/data/velaguard/config"
#endif

#define VGPOINT_CMDLEN_MAX 120

static void reply_ok(FAR const char *cmd, FAR const char *table, int n)
{
  char line[160];

  vg_point_format_ok(line, sizeof(line), cmd, table, n);
  printf("%s\n", line);
}

static int reply_err(FAR const char *cmd, FAR const char *code,
                     FAR const char *msg)
{
  char line[160];

  vg_point_format_err(line, sizeof(line), cmd, code, msg);
  printf("%s\n", line);
  return 1;
}

static int flag_index(int argc, char *argv[], int start, FAR const char *flag)
{
  int i;

  for (i = start; i < argc; i++)
    {
      if (strcmp(argv[i], flag) == 0)
        {
          return i;
        }
    }

  return -1;
}

static int flag_value(int argc, char *argv[], int start,
                      FAR const char *flag, FAR const char **out)
{
  int i = flag_index(argc, argv, start, flag);

  if (i < 0)
    {
      return 0;
    }

  if (i + 1 >= argc)
    {
      return -1;
    }

  *out = argv[i + 1];
  return 1;
}

static int parse_long_range(FAR const char *s, long minv, long maxv, long *out)
{
  char *end;
  long v;

  if (s == NULL || out == NULL)
    {
      return -1;
    }

  v = strtol(s, &end, 0);
  if (end == s || *end != '\0' || v < minv || v > maxv)
    {
      return -1;
    }

  *out = v;
  return 0;
}

static int parse_float_val(FAR const char *s, float *out)
{
  char *end;
  float v;

  if (s == NULL || out == NULL)
    {
      return -1;
    }

  v = strtof(s, &end);
  if (end == s || *end != '\0' || v != v)
    {
      return -1;
    }

  *out = v;
  return 0;
}

static int apply_common_fields(int argc, char *argv[], int start,
                               FAR struct vg_point_entry *p, int *touched)
{
  FAR const char *val;
  int rc;
  long v;
  float f;

  rc = flag_value(argc, argv, start, "-a", &val);
  if (rc < 0)
    {
      return -1;
    }

  if (rc > 0)
    {
      if (parse_long_range(val, 1, 247, &v) != 0)
        {
          return -1;
        }

      p->addr = (uint8_t)v;
      if (touched)
        {
          *touched = 1;
        }
    }

  rc = flag_value(argc, argv, start, "-r", &val);
  if (rc < 0)
    {
      return -1;
    }

  if (rc > 0)
    {
      if (parse_long_range(val, 0, 65535, &v) != 0)
        {
          return -1;
        }

      p->reg = (uint16_t)v;
      if (touched)
        {
          *touched = 1;
        }
    }

  rc = flag_value(argc, argv, start, "-f", &val);
  if (rc < 0)
    {
      return -1;
    }

  if (rc > 0)
    {
      if (parse_long_range(val, 3, 4, &v) != 0 || (v != 3 && v != 4))
        {
          return -1;
        }

      p->fc = (uint8_t)v;
      if (touched)
        {
          *touched = 1;
        }
    }

  rc = flag_value(argc, argv, start, "-q", &val);
  if (rc < 0)
    {
      return -1;
    }

  if (rc > 0)
    {
      if (parse_long_range(val, 1, 4, &v) != 0)
        {
          return -1;
        }

      p->qty = (uint16_t)v;
      if (touched)
        {
          *touched = 1;
        }
    }

  rc = flag_value(argc, argv, start, "-d", &val);
  if (rc < 0)
    {
      return -1;
    }

  if (rc > 0)
    {
      if (strcmp(val, "int16") != 0 && strcmp(val, "uint16") != 0)
        {
          return -1;
        }

      snprintf(p->dtype, sizeof(p->dtype), "%s", val);
      if (touched)
        {
          *touched = 1;
        }
    }

  rc = flag_value(argc, argv, start, "-s", &val);
  if (rc < 0)
    {
      return -1;
    }

  if (rc > 0)
    {
      if (parse_float_val(val, &f) != 0)
        {
          return -1;
        }

      p->scale = f;
      if (touched)
        {
          *touched = 1;
        }
    }

  rc = flag_value(argc, argv, start, "-u", &val);
  if (rc < 0)
    {
      return -1;
    }

  if (rc > 0)
    {
      if (strlen(val) > 7)
        {
          return -1;
        }

      snprintf(p->unit, sizeof(p->unit), "%s", val);
      if (touched)
        {
          *touched = 1;
        }
    }

  rc = flag_value(argc, argv, start, "-k", &val);
  if (rc < 0)
    {
      return -1;
    }

  if (rc > 0)
    {
      if (strcmp(val, "-") == 0)
        {
          p->cmp[0] = '\0';
        }
      else if (strcmp(val, "ge") == 0 || strcmp(val, "le") == 0 ||
               strcmp(val, "eq") == 0)
        {
          snprintf(p->cmp, sizeof(p->cmp), "%s", val);
        }
      else
        {
          return -1;
        }

      if (touched)
        {
          *touched = 1;
        }
    }

  rc = flag_value(argc, argv, start, "-w", &val);
  if (rc < 0)
    {
      return -1;
    }

  if (rc > 0)
    {
      if (strcmp(val, "-") == 0)
        {
          p->has_warn = 0;
          p->warn = 0.0f;
        }
      else if (parse_float_val(val, &f) != 0)
        {
          return -1;
        }
      else
        {
          p->has_warn = 1;
          p->warn = f;
        }

      if (touched)
        {
          *touched = 1;
        }
    }

  rc = flag_value(argc, argv, start, "-C", &val);
  if (rc < 0)
    {
      return -1;
    }

  if (rc > 0)
    {
      if (strcmp(val, "-") == 0)
        {
          p->has_crit = 0;
          p->crit = 0.0f;
        }
      else if (parse_float_val(val, &f) != 0)
        {
          return -1;
        }
      else
        {
          p->has_crit = 1;
          p->crit = f;
        }

      if (touched)
        {
          *touched = 1;
        }
    }

  rc = flag_value(argc, argv, start, "-N", &val);
  if (rc < 0)
    {
      return -1;
    }

  if (rc > 0)
    {
      if (!vg_point_validate_name(val))
        {
          return -1;
        }

      snprintf(p->name, sizeof(p->name), "%s", val);
      if (touched)
        {
          *touched = 1;
        }
    }

  rc = flag_value(argc, argv, start, "-n", &val);
  if (rc < 0)
    {
      return -1;
    }

  if (rc > 0)
    {
      if (parse_long_range(val, 1, 20, &v) != 0)
        {
          return -1;
        }

      p->fail_n = (uint8_t)v;
      if (touched)
        {
          *touched = 1;
        }
    }

  if (p->fail_n < 1)
    {
      p->fail_n = 3;
    }

  if (p->fc == 0)
    {
      p->fc = 3;
    }

  if (p->qty == 0)
    {
      p->qty = 1;
    }

  if (p->dtype[0] == '\0')
    {
      snprintf(p->dtype, sizeof(p->dtype), "int16");
    }

  return 0;
}

static int cmd_list(int argc, char *argv[])
{
  struct vg_discover_summary sum;
  FAR const char *path = CONFIG_VG_DISCOVER_POINTS_PATH;
  FAR const char *table = "committed";
  char line[256];
  int i;
  int ret;

  if (flag_index(argc, argv, 2, "-c") >= 0)
    {
      path = CONFIG_VG_DISCOVER_CANDIDATE_PATH;
      table = "candidate";
    }

  ret = vg_point_table_read(&sum, path);
  if (ret != 0)
    {
      memset(&sum, 0, sizeof(sum));
    }

  for (i = 0; i < sum.n_points; i++)
    {
      vg_point_format_point(line, sizeof(line), &sum.points[i]);
      printf("%s\n", line);
    }

  reply_ok("list", table, sum.n_points);
  return 0;
}

static int cmd_add(int argc, char *argv[])
{
  struct vg_discover_summary sum;
  FAR const char *id = NULL;
  FAR struct vg_point_entry *p;
  int rc;

  if (flag_value(argc, argv, 2, "-i", &id) <= 0 ||
      !vg_point_validate_id(id))
    {
      return reply_err("add", "bad_arg", "bad_id");
    }

  rc = vg_point_table_ensure_candidate(&sum,
                                       CONFIG_VG_DISCOVER_CANDIDATE_PATH,
                                       CONFIG_VG_DISCOVER_POINTS_PATH);
  if (rc != 0)
    {
      return reply_err("add", "io", "candidate_io");
    }

  if (vg_point_table_find_id(&sum, id) >= 0)
    {
      return reply_err("add", "dup_id", "id_exists");
    }

  if (sum.n_points >= VG_DISCOVER_MAX_POINTS)
    {
      return reply_err("add", "full", "table_full");
    }

  p = &sum.points[sum.n_points];
  memset(p, 0, sizeof(*p));
  snprintf(p->id, sizeof(p->id), "%s", id);
  p->fc = 3;
  p->qty = 1;
  p->scale = 1.0f;
  p->fail_n = 3;
  snprintf(p->dtype, sizeof(p->dtype), "int16");

  if (apply_common_fields(argc, argv, 2, p, NULL) != 0 ||
      p->addr < 1)
    {
      return reply_err("add", "bad_arg", "bad_field");
    }

  if (flag_index(argc, argv, 2, "-a") < 0 ||
      flag_index(argc, argv, 2, "-r") < 0)
    {
      return reply_err("add", "bad_arg", "need_addr_reg");
    }

  if (p->name[0] == '\0')
    {
      memcpy(p->name, p->id, strlen(p->id) + 1);
    }

  sum.n_points++;
  if (vg_point_table_write_candidate(&sum,
                                     CONFIG_VG_DISCOVER_CANDIDATE_PATH) != 0)
    {
      return reply_err("add", "io", "write_fail");
    }

  reply_ok("add", "candidate", sum.n_points);
  return 0;
}

static int cmd_set(int argc, char *argv[])
{
  struct vg_discover_summary sum;
  FAR const char *id;
  int idx;
  int rc;

  if (argc < 3 || argv[2][0] == '-')
    {
      return reply_err("set", "bad_arg", "need_id");
    }

  id = argv[2];
  if (!vg_point_validate_id(id))
    {
      return reply_err("set", "bad_arg", "bad_id");
    }

  rc = vg_point_table_ensure_candidate(&sum,
                                       CONFIG_VG_DISCOVER_CANDIDATE_PATH,
                                       CONFIG_VG_DISCOVER_POINTS_PATH);
  if (rc != 0)
    {
      return reply_err("set", "io", "candidate_io");
    }

  idx = vg_point_table_find_id(&sum, id);
  if (idx < 0)
    {
      return reply_err("set", "no_id", "not_found");
    }

  if (apply_common_fields(argc, argv, 3, &sum.points[idx], NULL) != 0)
    {
      return reply_err("set", "bad_arg", "bad_field");
    }

  if (vg_point_table_write_candidate(&sum,
                                     CONFIG_VG_DISCOVER_CANDIDATE_PATH) != 0)
    {
      return reply_err("set", "io", "write_fail");
    }

  reply_ok("set", "candidate", sum.n_points);
  return 0;
}

static int cmd_del(int argc, char *argv[])
{
  struct vg_discover_summary sum;
  FAR const char *id;
  int idx;
  int i;
  int rc;

  if (argc < 3)
    {
      return reply_err("del", "bad_arg", "need_id");
    }

  id = argv[2];
  rc = vg_point_table_ensure_candidate(&sum,
                                       CONFIG_VG_DISCOVER_CANDIDATE_PATH,
                                       CONFIG_VG_DISCOVER_POINTS_PATH);
  if (rc != 0)
    {
      return reply_err("del", "io", "candidate_io");
    }

  idx = vg_point_table_find_id(&sum, id);
  if (idx < 0)
    {
      return reply_err("del", "no_id", "not_found");
    }

  for (i = idx; i + 1 < sum.n_points; i++)
    {
      sum.points[i] = sum.points[i + 1];
    }

  sum.n_points--;
  if (vg_point_table_write_candidate(&sum,
                                     CONFIG_VG_DISCOVER_CANDIDATE_PATH) != 0)
    {
      return reply_err("del", "io", "write_fail");
    }

  reply_ok("del", "candidate", sum.n_points);
  return 0;
}

static int cmd_test(int argc, char *argv[])
{
  struct vg_discover_summary sum;
  uint16_t raw[VG_DISCOVER_MAX_POINTS];
  uint8_t oks[VG_DISCOVER_MAX_POINTS];
  FAR const char *id = NULL;
  FAR const char *dev;
  int i;
  int n_ok = 0;
  int n_try = 0;
  int rc;
  int idx = -1;

  if (argc >= 3 && argv[2][0] != '-')
    {
      id = argv[2];
      if (!vg_point_validate_id(id))
        {
          return reply_err("test", "bad_arg", "bad_id");
        }
    }

  rc = vg_point_table_read(&sum, CONFIG_VG_DISCOVER_CANDIDATE_PATH);
  if (rc != 0 || sum.n_points <= 0)
    {
      return reply_err("test", "no_candidate", "empty");
    }

  if (id != NULL)
    {
      idx = vg_point_table_find_id(&sum, id);
      if (idx < 0)
        {
          return reply_err("test", "no_id", "not_found");
        }
    }

  if (vg_bus_try_lock() != 0)
    {
      return reply_err("test", "bus_busy", "rs485_busy");
    }

  dev = (sum.devpath[0] != '\0') ? sum.devpath : CONFIG_VG_DISCOVER_DEVPATH;
  printf("vgpoint: test bus=%s\n", dev);

  rc = vg_discover_poll_points(dev, CONFIG_VG_DISCOVER_BAUD,
                               sum.points, sum.n_points,
                               raw, oks, CONFIG_VG_DISCOVER_INTER_MS);
  vg_bus_unlock();
  if (rc != 0)
    {
      return reply_err("test", "io", "read_fail");
    }

  for (i = 0; i < sum.n_points; i++)
    {
      FAR const struct vg_point_entry *p = &sum.points[i];
      float value;
      int signed_v = (strcmp(p->dtype, "uint16") != 0);

      if (id != NULL && i != idx)
        {
          continue;
        }

      n_try++;
      if (oks[i])
        {
          n_ok++;
          value = signed_v ? ((float)(int16_t)raw[i] * p->scale)
                           : ((float)raw[i] * p->scale);
          printf("vgpoint: READ id=%s raw=%u value=%.6g ok=1\n",
                 p->id, (unsigned)raw[i], (double)value);
        }
      else
        {
          printf("vgpoint: READ id=%s raw=- value=- ok=0\n", p->id);
        }
    }

  if (n_try > 0 && n_ok == 0)
    {
      return reply_err("test", "test_fail", "all_failed");
    }

  reply_ok("test", "candidate", n_ok);
  return 0;
}

static int cmd_apply(int argc, char *argv[])
{
  struct vg_discover_summary sum;
  int confirm = 0;
  int i;
  int rc;

  for (i = 2; i < argc; i++)
    {
      if (strcmp(argv[i], "--confirm") == 0)
        {
          confirm = 1;
        }
    }

  if (!confirm)
    {
      return reply_err("apply", "need_confirm", "need_confirm");
    }

  rc = vg_point_table_read(&sum, CONFIG_VG_DISCOVER_CANDIDATE_PATH);
  if (rc != 0)
    {
      return reply_err("apply", "no_candidate", "missing");
    }

  if (vg_point_table_write_candidate(&sum,
                                     CONFIG_VG_DISCOVER_POINTS_PATH) != 0)
    {
      return reply_err("apply", "io", "write_points");
    }

#ifdef CONFIG_VG_CONFIG_STORE
  {
    struct vg_config cfg;

    vg_config_set_basedir(CONFIG_VG_CONFIG_BASEDIR);
    vg_config_factory_default(&cfg);
    snprintf(cfg.device_name, sizeof(cfg.device_name), "vgpoint");
    rc = vg_config_commit(&cfg);
    if (rc != 0)
      {
        return reply_err("apply", "io", "commit_fail");
      }
  }
#endif

  (void)vg_live_points_replace(&sum);
  reply_ok("apply", "committed", sum.n_points);
  return 0;
}

static int cmd_abort(void)
{
  int rc;

  rc = unlink(CONFIG_VG_DISCOVER_CANDIDATE_PATH);
  if (rc != 0 && errno != ENOENT)
    {
      return reply_err("abort", "io", "unlink_fail");
    }

  reply_ok("abort", "candidate", 0);
  return 0;
}

int main(int argc, char *argv[])
{
  FAR const char *verb;

  if (vg_point_cmdline_len(argc, argv) > VGPOINT_CMDLEN_MAX)
    {
      return reply_err((argc >= 2) ? argv[1] : "-", "too_long", "too_long");
    }

  if (argc < 2)
    {
      return reply_err("-", "bad_arg", "need_verb");
    }

  verb = argv[1];
  if (strcmp(verb, "list") == 0)
    {
      return cmd_list(argc, argv);
    }

  if (strcmp(verb, "add") == 0)
    {
      return cmd_add(argc, argv);
    }

  if (strcmp(verb, "set") == 0)
    {
      return cmd_set(argc, argv);
    }

  if (strcmp(verb, "del") == 0)
    {
      return cmd_del(argc, argv);
    }

  if (strcmp(verb, "test") == 0)
    {
      return cmd_test(argc, argv);
    }

  if (strcmp(verb, "apply") == 0)
    {
      return cmd_apply(argc, argv);
    }

  if (strcmp(verb, "abort") == 0)
    {
      return cmd_abort();
    }

  return reply_err(verb, "bad_arg", "unknown_verb");
}
