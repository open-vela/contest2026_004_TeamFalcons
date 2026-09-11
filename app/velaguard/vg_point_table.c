/****************************************************************************
 * app/velaguard/vg_point_table.c
 *
 * Point inference, JSON export, state persistence, apply.
 ****************************************************************************/

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#ifdef __NuttX__
#  include <pthread.h>
#endif

#if defined(__NuttX__)
#  include <nuttx/config.h>
#else
#  undef CONFIG_VG_CONFIG_STORE
#endif

#include "vg_discover.h"

#ifdef CONFIG_VG_CONFIG_STORE
#include "vg_config_store.h"
#endif

#define VG_DISC_STATE_MAGIC  0x56474453u
#define VG_DISC_STATE_PATH   "/data/velaguard/discover/discover_state.bin"

struct vg_disc_state_file
{
  uint32_t magic;
  struct vg_discover_summary sum;
};

static struct vg_discover_summary g_sum;
static struct vg_disc_state_file g_sf;

struct vg_discover_summary *vg_discover_state(void)
{
  return &g_sum;
}

void vg_discover_reset(FAR struct vg_discover_summary *sum)
{
  if (sum != NULL)
    {
      memset(sum, 0, sizeof(*sum));
    }
}

float vg_discover_decode_int16_scaled(int16_t raw, float scale)
{
  return (float)raw * scale;
}

bool vg_discover_parse_addr_range(FAR const char *spec,
                                    int *min_out, int *max_out)
{
  int a;
  int b;

  if (spec == NULL || min_out == NULL || max_out == NULL)
    {
      return false;
    }

  if (strchr(spec, '-') != NULL)
    {
      if (sscanf(spec, "%d-%d", &a, &b) != 2)
        {
          return false;
        }
    }
  else
    {
      a = b = (int)strtol(spec, NULL, 0);
    }

  if (a < 1 || b > 247 || a > b)
    {
      return false;
    }

  *min_out = a;
  *max_out = b;
  return true;
}

static int mkdir_p(FAR const char *path)
{
  char tmp[128];
  char *p;
  size_t len;

  if (path == NULL)
    {
      return -EINVAL;
    }

  snprintf(tmp, sizeof(tmp), "%s", path);
  len = strlen(tmp);
  if (len == 0)
    {
      return -EINVAL;
    }

  for (p = tmp + 1; *p != '\0'; p++)
    {
      if (*p == '/')
        {
          *p = '\0';
          (void)mkdir(tmp, 0755);
          *p = '/';
        }
    }

  return mkdir(tmp, 0755);
}

static int mkdir_parent(FAR const char *filepath)
{
  char tmp[160];
  char *slash;

  if (filepath == NULL || filepath[0] == '\0')
    {
      return -EINVAL;
    }

  snprintf(tmp, sizeof(tmp), "%s", filepath);
  slash = strrchr(tmp, '/');
  if (slash == NULL || slash == tmp)
    {
      return 0;
    }

  *slash = '\0';
  return mkdir_p(tmp);
}

int vg_discover_state_save(FAR const struct vg_discover_summary *sum,
                           FAR const char *path)
{
  int fd;
  ssize_t n;
  FAR const char *out = (path != NULL && path[0] != '\0') ?
                        path : VG_DISC_STATE_PATH;

  if (sum == NULL)
    {
      return -EINVAL;
    }

  mkdir_p("/data/velaguard/discover");

  g_sf.magic = VG_DISC_STATE_MAGIC;
  memcpy(&g_sf.sum, sum, sizeof(g_sf.sum));

  fd = open(out, O_WRONLY | O_CREAT | O_TRUNC, 0644);
  if (fd < 0)
    {
      return -errno;
    }

  n = write(fd, &g_sf, sizeof(g_sf));
  close(fd);

  return (n == (ssize_t)sizeof(g_sf)) ? 0 : -EIO;
}

int vg_discover_state_load(FAR struct vg_discover_summary *sum,
                           FAR const char *path)
{
  int fd;
  ssize_t n;
  FAR const char *in = (path != NULL && path[0] != '\0') ?
                       path : VG_DISC_STATE_PATH;

  if (sum == NULL)
    {
      return -EINVAL;
    }

  fd = open(in, O_RDONLY);
  if (fd < 0)
    {
      return -errno;
    }

  n = read(fd, &g_sf, sizeof(g_sf));
  close(fd);

  if (n != (ssize_t)sizeof(g_sf) || g_sf.magic != VG_DISC_STATE_MAGIC)
    {
      return -EINVAL;
    }

  memcpy(sum, &g_sf.sum, sizeof(*sum));
  return 0;
}

static void add_point(FAR struct vg_discover_summary *sum,
                      uint8_t addr, uint8_t fc, uint16_t reg,
                      FAR const char *id, FAR const char *dtype,
                      float scale, FAR const char *unit)
{
  FAR struct vg_point_entry *p;

  if (sum->n_points >= VG_DISCOVER_MAX_POINTS)
    {
      return;
    }

  p = &sum->points[sum->n_points++];
  memset(p, 0, sizeof(*p));
  p->addr = addr;
  p->fc   = fc;
  p->reg  = reg;
  p->qty  = 1;
  snprintf(p->id, sizeof(p->id), "%s", id);
  snprintf(p->name, sizeof(p->name), "%s", id);
  snprintf(p->dtype, sizeof(p->dtype), "%s", dtype);
  p->scale = scale;
  snprintf(p->unit, sizeof(p->unit), "%s", unit);
  p->fail_n = 3;
}

int vg_point_table_infer(FAR struct vg_discover_summary *sum)
{
  int i;

  if (sum == NULL)
    {
      return -EINVAL;
    }

  sum->n_points = 0;

  for (i = 0; i < sum->n_blocks; i++)
    {
      FAR struct vg_reg_block *b = &sum->blocks[i];
      int tag_idx = 0;

      if (b->count == 0)
        {
          continue;
        }

      for (tag_idx = 0; tag_idx < (int)b->count && tag_idx < 4; tag_idx++)
        {
          char id[VG_POINT_ID_MAX];
          float scaled;

          scaled = vg_discover_decode_int16_scaled((int16_t)b->sample[tag_idx],
                                                   0.1f);
          snprintf(id, sizeof(id), "s%u_r%u_%d",
                   (unsigned)b->addr,
                   (unsigned)(b->start + (uint16_t)tag_idx), tag_idx);

          if (scaled >= -50.0f && scaled <= 120.0f)
            {
              add_point(sum, b->addr, b->fc,
                        (uint16_t)(b->start + (uint16_t)tag_idx),
                        id, "int16", 0.1f, (tag_idx == 0) ? "C" : "%RH");
            }
          else
            {
              add_point(sum, b->addr, b->fc,
                        (uint16_t)(b->start + (uint16_t)tag_idx),
                        id, "uint16", 1.0f, "");
            }
        }
    }

  return sum->n_points;
}

int vg_point_table_write_candidate(FAR const struct vg_discover_summary *sum,
                                     FAR const char *path)
{
  FILE *fp;
  int i;
  FAR const char *out = path;

  if (sum == NULL || out == NULL)
    {
      return -EINVAL;
    }

  mkdir_p("/data/velaguard/discover");

  fp = fopen(out, "w");
  if (fp == NULL)
    {
      return -errno;
    }

  fprintf(fp,
          "{\"schema_version\":1,\"bus\":{\"device\":\"%s\",\"baud\":%d},"
          "\"hits\":[",
          sum->devpath, sum->baud);

  for (i = 0; i < sum->n_hits; i++)
    {
      fprintf(fp, "%s%u", (i > 0) ? "," : "",
              (unsigned)sum->hits[i].addr);
    }

  fprintf(fp, "],\"points\":[");

  for (i = 0; i < sum->n_points; i++)
    {
      FAR const struct vg_point_entry *p = &sum->points[i];

      fprintf(fp,
              "%s{\"id\":\"%s\",\"name\":\"%s\",\"addr\":%u,\"fc\":%u,\"reg\":%u,"
              "\"qty\":%u,\"dtype\":\"%s\",\"scale\":%.3f,\"unit\":\"%s\"",
              (i > 0) ? "," : "",
              p->id, p->name[0] ? p->name : p->id,
              (unsigned)p->addr, (unsigned)p->fc,
              (unsigned)p->reg, (unsigned)p->qty,
              p->dtype, (double)p->scale, p->unit);
      if (p->cmp[0] != '\0')
        {
          fprintf(fp, ",\"cmp\":\"%s\"", p->cmp);
        }

      if (p->has_warn)
        {
          fprintf(fp, ",\"warn\":%.6g", (double)p->warn);
        }

      if (p->has_crit)
        {
          fprintf(fp, ",\"crit\":%.6g", (double)p->crit);
        }

      fprintf(fp, ",\"fail_n\":%u}",
              (unsigned)((p->fail_n >= 1) ? p->fail_n : 3));
    }

  fprintf(fp, "]}\n");
  fclose(fp);
  return 0;
}

int vg_point_table_apply(FAR const struct vg_discover_summary *sum,
                         FAR const char *points_path,
                         FAR const char *config_basedir,
                         bool confirm)
{
  if (sum == NULL || points_path == NULL)
    {
      return -EINVAL;
    }

  if (!confirm)
    {
      printf("vgdiscover: dry-run apply → %d points to %s (use --confirm)\n",
             sum->n_points, points_path);
      return 0;
    }

  if (vg_point_table_write_candidate(sum, points_path) != 0)
    {
      return -EIO;
    }

#ifdef CONFIG_VG_CONFIG_STORE
  if (config_basedir != NULL)
    {
      struct vg_config cfg;
      int ret;

      vg_config_set_basedir(config_basedir);
      vg_config_factory_default(&cfg);
      snprintf(cfg.device_name, sizeof(cfg.device_name), "discovered");
      ret = vg_config_commit(&cfg);
      if (ret != 0)
        {
          printf("vgdiscover: vg_config_commit failed %d (%s)\n",
                 ret, vg_config_last_error());
          return ret;
        }
    }
#else
  (void)config_basedir;
#endif

  printf("vgdiscover: applied %d points → %s\n", sum->n_points, points_path);
  (void)vg_live_points_replace(sum);
  return 0;
}

static int append_unique_addr(uint8_t *addrs, int *n, int max, unsigned v)
{
  int i;

  if (v < 1 || v > 247 || *n >= max)
    {
      return 0;
    }

  for (i = 0; i < *n; i++)
    {
      if (addrs[i] == (uint8_t)v)
        {
          return 0;
        }
    }

  addrs[(*n)++] = (uint8_t)v;
  return 1;
}

static int parse_hits_array(FAR const char *json, uint8_t *addrs, int max)
{
  FAR const char *hits;
  FAR const char *p;
  char *end;
  int n = 0;

  hits = strstr(json, "\"hits\":");
  if (hits == NULL)
    {
      return 0;
    }

  p = strchr(hits, '[');
  if (p == NULL)
    {
      return 0;
    }

  p++;
  while (*p != '\0' && *p != ']' && n < max)
    {
      while (*p == ' ' || *p == '\t' || *p == ',' || *p == '\n' || *p == '\r')
        {
          p++;
        }

      if (*p == ']' || *p == '\0')
        {
          break;
        }

      if (*p >= '0' && *p <= '9')
        {
          unsigned v = (unsigned)strtoul(p, &end, 10);
          if (end == p)
            {
              break;
            }

          append_unique_addr(addrs, &n, max, v);
          p = end;
        }
      else
        {
          p++;
        }
    }

  return n;
}

static int parse_point_addrs(FAR const char *json, uint8_t *addrs, int max)
{
  FAR const char *p = json;
  int n = 0;

  while ((p = strstr(p, "\"addr\":")) != NULL && n < max)
    {
      char *end;
      unsigned v;

      p += 7;
      v = (unsigned)strtoul(p, &end, 10);
      if (end == p)
        {
          p++;
          continue;
        }

      append_unique_addr(addrs, &n, max, v);
      p = end;
    }

  return n;
}

int vg_point_table_read_slaves(FAR const char *path, uint8_t *addrs, int max)
{
  FILE *fp;
  char buf[4096];
  size_t nread;
  int n;

  if (path == NULL || path[0] == '\0' || addrs == NULL || max <= 0)
    {
      return -EINVAL;
    }

  fp = fopen(path, "r");
  if (fp == NULL)
    {
      return -errno;
    }

  nread = fread(buf, 1, sizeof(buf) - 1, fp);
  fclose(fp);
  buf[nread] = '\0';

  n = parse_hits_array(buf, addrs, max);
  if (n <= 0)
    {
      n = parse_point_addrs(buf, addrs, max);
    }

  return n;
}

#define VG_POINTS_JSON_MAX 8192

#ifdef __NuttX__
static pthread_mutex_t g_bus_mtx = PTHREAD_MUTEX_INITIALIZER;
#else
static int g_bus_lock;
#endif

static struct vg_discover_summary g_live;
static uint32_t g_live_gen;

int vg_bus_try_lock(void)
{
#ifdef __NuttX__
  if (pthread_mutex_trylock(&g_bus_mtx) != 0)
    {
      return -EBUSY;
    }

  return 0;
#else
  if (g_bus_lock)
    {
      return -EBUSY;
    }

  g_bus_lock = 1;
  return 0;
#endif
}

void vg_bus_unlock(void)
{
#ifdef __NuttX__
  pthread_mutex_unlock(&g_bus_mtx);
#else
  g_bus_lock = 0;
#endif
}

int vg_bus_is_locked(void)
{
#ifdef __NuttX__
  if (pthread_mutex_trylock(&g_bus_mtx) != 0)
    {
      return 1;
    }

  pthread_mutex_unlock(&g_bus_mtx);
  return 0;
#else
  return g_bus_lock != 0;
#endif
}

int vg_live_points_replace(FAR const struct vg_discover_summary *sum)
{
  if (sum == NULL)
    {
      memset(&g_live, 0, sizeof(g_live));
      g_live_gen++;
      return 0;
    }

  memcpy(&g_live, sum, sizeof(g_live));
  g_live_gen++;
  return 0;
}

int vg_live_points_load(FAR const char *path)
{
  int ret;

  if (path == NULL || path[0] == '\0')
    {
      memset(&g_live, 0, sizeof(g_live));
      g_live_gen++;
      return -EINVAL;
    }

  ret = vg_point_table_read(&g_live, path);
  if (ret != 0)
    {
      memset(&g_live, 0, sizeof(g_live));
    }

  g_live_gen++;
  return ret;
}

uint32_t vg_live_points_gen(void)
{
  return g_live_gen;
}

int vg_live_points_copy(FAR struct vg_discover_summary *out)
{
  if (out == NULL)
    {
      return -EINVAL;
    }

  memcpy(out, &g_live, sizeof(*out));
  return g_live.n_points;
}

int vg_point_validate_id(FAR const char *id)
{
  size_t n;
  size_t i;

  if (id == NULL)
    {
      return 0;
    }

  n = strlen(id);
  if (n < 1 || n > 23)
    {
      return 0;
    }

  for (i = 0; i < n; i++)
    {
      if (!(isalnum((unsigned char)id[i]) || id[i] == '_'))
        {
          return 0;
        }
    }

  return 1;
}

int vg_point_validate_name(FAR const char *name)
{
  size_t n;
  size_t i;
  unsigned char c;

  if (name == NULL)
    {
      return 0;
    }

  n = strlen(name);
  if (n < 1 || n > (VG_POINT_NAME_MAX - 1))
    {
      return 0;
    }

  for (i = 0; i < n; i++)
    {
      c = (unsigned char)name[i];
      if (c <= 0x20 || c == 0x7f || c == '=' || c == '"' || c == '\\')
        {
          return 0;
        }
    }

  return 1;
}

int vg_point_cmdline_len(int argc, char *argv[])
{
  int n = 0;
  int i;

  if (argc < 0 || argv == NULL)
    {
      return 0;
    }

  for (i = 0; i < argc; i++)
    {
      if (argv[i] == NULL)
        {
          continue;
        }

      if (n > 0)
        {
          n++;
        }

      n += (int)strlen(argv[i]);
    }

  return n;
}

int vg_point_format_ok(FAR char *buf, size_t bufsz,
                       FAR const char *cmd, FAR const char *table, int n)
{
  if (buf == NULL || bufsz == 0)
    {
      return -EINVAL;
    }

  snprintf(buf, bufsz, "vgpoint: OK cmd=%s table=%s n=%d",
           (cmd != NULL) ? cmd : "-",
           (table != NULL) ? table : "-",
           n);
  return 0;
}

int vg_point_format_err(FAR char *buf, size_t bufsz,
                        FAR const char *cmd, FAR const char *code,
                        FAR const char *msg)
{
  if (buf == NULL || bufsz == 0)
    {
      return -EINVAL;
    }

  snprintf(buf, bufsz, "vgpoint: ERR cmd=%s code=%s msg=%s",
           (cmd != NULL) ? cmd : "-",
           (code != NULL) ? code : "bad_arg",
           (msg != NULL) ? msg : "-");
  return 0;
}

int vg_point_format_point(FAR char *buf, size_t bufsz,
                          FAR const struct vg_point_entry *p)
{
  char warn_s[32];
  char crit_s[32];

  if (buf == NULL || bufsz == 0 || p == NULL)
    {
      return -EINVAL;
    }

  if (p->has_warn)
    {
      snprintf(warn_s, sizeof(warn_s), "%.6g", (double)p->warn);
    }
  else
    {
      snprintf(warn_s, sizeof(warn_s), "-");
    }

  if (p->has_crit)
    {
      snprintf(crit_s, sizeof(crit_s), "%.6g", (double)p->crit);
    }
  else
    {
      snprintf(crit_s, sizeof(crit_s), "-");
    }

  snprintf(buf, bufsz,
           "vgpoint: POINT id=%s name=%s addr=%u fc=%u reg=%u qty=%u dtype=%s "
           "scale=%.6g unit=%s cmp=%s warn=%s crit=%s fail_n=%u",
           p->id[0] ? p->id : "-",
           p->name[0] ? p->name : (p->id[0] ? p->id : "-"),
           (unsigned)p->addr, (unsigned)p->fc, (unsigned)p->reg,
           (unsigned)((p->qty != 0) ? p->qty : 1),
           p->dtype[0] ? p->dtype : "int16",
           (double)p->scale,
           p->unit[0] ? p->unit : "-",
           p->cmp[0] ? p->cmp : "-",
           warn_s, crit_s,
           (unsigned)((p->fail_n >= 1) ? p->fail_n : 3));
  return 0;
}

int vg_point_format_value(FAR char *buf, size_t bufsz,
                          FAR const char *id, float value, int ok,
                          FAR const char *unit, uint32_t age_ms)
{
  char value_s[32];
  FAR const char *unit_s;

  if (buf == NULL || bufsz == 0)
    {
      return -EINVAL;
    }

  if (ok)
    {
      snprintf(value_s, sizeof(value_s), "%.6g", (double)value);
    }
  else
    {
      snprintf(value_s, sizeof(value_s), "-");
    }

  if (unit != NULL && unit[0] != '\0' && strcmp(unit, "-") != 0)
    {
      unit_s = unit;
    }
  else
    {
      unit_s = "-";
    }

  snprintf(buf, bufsz, "vgpoint: VALUE id=%s value=%s ok=%d unit=%s age_ms=%u",
           (id != NULL && id[0] != '\0') ? id : "-",
           value_s, ok ? 1 : 0, unit_s, (unsigned)age_ms);
  return 0;
}

uint32_t vg_live_now_ms(void)
{
  struct timespec ts;

  if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
    {
      return 0;
    }

  return (uint32_t)((uint64_t)ts.tv_sec * 1000ull +
                    (uint64_t)ts.tv_nsec / 1000000ull);
}

uint32_t vg_live_age_ms(uint32_t tick_ms, uint32_t now_ms)
{
  return now_ms - tick_ms;
}

int vg_live_snapshot_write(FAR const char *path,
                           FAR const struct vg_live_snapshot *snap)
{
  char tmp[160];
  FILE *fp;
  int i;
  int n;

  if (path == NULL || path[0] == '\0' || snap == NULL)
    {
      return -EINVAL;
    }

  n = snap->n;
  if (n < 0)
    {
      n = 0;
    }

  if (n > VG_DISCOVER_MAX_POINTS)
    {
      n = VG_DISCOVER_MAX_POINTS;
    }

  (void)mkdir_parent(path);
  snprintf(tmp, sizeof(tmp), "%s.tmp", path);
  fp = fopen(tmp, "w");
  if (fp == NULL)
    {
      return -errno;
    }

  if (fprintf(fp, "tick_ms=%u\nn=%d\n", (unsigned)snap->tick_ms, n) < 0)
    {
      fclose(fp);
      unlink(tmp);
      return -EIO;
    }

  for (i = 0; i < n; i++)
    {
      FAR const struct vg_live_sample *s = &snap->samples[i];
      FAR const char *unit = (s->unit[0] != '\0') ? s->unit : "-";
      int rc;

      if (s->ok)
        {
          rc = fprintf(fp, "id=%s value=%.6g ok=1 unit=%s\n",
                       s->id[0] ? s->id : "-", (double)s->value, unit);
        }
      else
        {
          rc = fprintf(fp, "id=%s value=- ok=0 unit=%s\n",
                       s->id[0] ? s->id : "-", unit);
        }

      if (rc < 0)
        {
          fclose(fp);
          unlink(tmp);
          return -EIO;
        }
    }

  if (fflush(fp) != 0)
    {
      fclose(fp);
      unlink(tmp);
      return -EIO;
    }

  fclose(fp);
  if (rename(tmp, path) != 0)
    {
      int err = errno;

      unlink(tmp);
      return -err;
    }

  return 0;
}

int vg_live_snapshot_read(FAR const char *path,
                          FAR struct vg_live_snapshot *snap)
{
  FILE *fp;
  char line[128];
  int declared_n = -1;

  if (path == NULL || path[0] == '\0' || snap == NULL)
    {
      return -EINVAL;
    }

  memset(snap, 0, sizeof(*snap));
  fp = fopen(path, "r");
  if (fp == NULL)
    {
      return (errno == ENOENT) ? -ENOENT : -errno;
    }

  while (fgets(line, sizeof(line), fp) != NULL)
    {
      size_t len = strlen(line);
      unsigned tick;
      unsigned ok;
      char id[VG_POINT_ID_MAX];
      char valstr[32];
      char unit[8];
      struct vg_live_sample *s;

      while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r'))
        {
          line[--len] = '\0';
        }

      if (len == 0 || line[0] == '#')
        {
          continue;
        }

      if (sscanf(line, "tick_ms=%u", &tick) == 1)
        {
          snap->tick_ms = tick;
          continue;
        }

      if (sscanf(line, "n=%d", &declared_n) == 1)
        {
          continue;
        }

      if (sscanf(line, "id=%23s value=%31s ok=%u unit=%7s",
                 id, valstr, &ok, unit) != 4)
        {
          continue;
        }

      if (!vg_point_validate_id(id) || snap->n >= VG_DISCOVER_MAX_POINTS)
        {
          continue;
        }

      s = &snap->samples[snap->n];
      memset(s, 0, sizeof(*s));
      snprintf(s->id, sizeof(s->id), "%s", id);
      if (unit[0] != '\0' && strcmp(unit, "-") != 0)
        {
          snprintf(s->unit, sizeof(s->unit), "%s", unit);
        }

      if (ok != 0 && strcmp(valstr, "-") != 0)
        {
          char *end = NULL;
          float v = strtof(valstr, &end);

          if (end != valstr && end != NULL && *end == '\0' && v == v)
            {
              s->ok = 1;
              s->value = v;
            }
        }

      snap->n++;
    }

  fclose(fp);
  (void)declared_n;
  return 0;
}

int vg_live_snapshot_find_id(FAR const struct vg_live_snapshot *snap,
                             FAR const char *id)
{
  int i;

  if (snap == NULL || id == NULL)
    {
      return -1;
    }

  for (i = 0; i < snap->n; i++)
    {
      if (strcmp(snap->samples[i].id, id) == 0)
        {
          return i;
        }
    }

  return -1;
}

int vg_point_table_find_id(FAR const struct vg_discover_summary *sum,
                           FAR const char *id)
{
  int i;

  if (sum == NULL || id == NULL)
    {
      return -1;
    }

  for (i = 0; i < sum->n_points; i++)
    {
      if (strcmp(sum->points[i].id, id) == 0)
        {
          return i;
        }
    }

  return -1;
}

static int json_copy_object(FAR const char *start, FAR char *out, size_t outsz)
{
  int depth = 0;
  size_t n = 0;
  FAR const char *p = start;

  if (start == NULL || *start != '{' || out == NULL || outsz < 2)
    {
      return -EINVAL;
    }

  do
    {
      if (n + 1 >= outsz)
        {
          return -ENOSPC;
        }

      if (*p == '{')
        {
          depth++;
        }
      else if (*p == '}')
        {
          depth--;
        }

      out[n++] = *p++;
    }
  while (depth > 0 && *p != '\0');

  if (depth != 0)
    {
      return -EINVAL;
    }

  out[n] = '\0';
  return 0;
}

static FAR const char *json_key(FAR const char *obj, FAR const char *key)
{
  char pat[40];
  FAR const char *p;

  if (obj == NULL || key == NULL)
    {
      return NULL;
    }

  snprintf(pat, sizeof(pat), "\"%s\"", key);
  p = strstr(obj, pat);
  if (p == NULL)
    {
      return NULL;
    }

  p += strlen(pat);
  while (*p == ' ' || *p == '\t')
    {
      p++;
    }

  if (*p != ':')
    {
      return NULL;
    }

  p++;
  while (*p == ' ' || *p == '\t')
    {
      p++;
    }

  return p;
}

static int json_str(FAR const char *obj, FAR const char *key,
                    FAR char *out, size_t outsz)
{
  FAR const char *p = json_key(obj, key);
  size_t n = 0;

  if (p == NULL || *p != '"' || out == NULL || outsz == 0)
    {
      return 0;
    }

  p++;
  while (*p != '\0' && *p != '"' && n + 1 < outsz)
    {
      out[n++] = *p++;
    }

  out[n] = '\0';
  return 1;
}

static int json_long(FAR const char *obj, FAR const char *key, long *out)
{
  FAR const char *p = json_key(obj, key);
  char *end;

  if (p == NULL || out == NULL)
    {
      return 0;
    }

  *out = strtol(p, &end, 10);
  return (end != p);
}

static int json_float(FAR const char *obj, FAR const char *key, float *out)
{
  FAR const char *p = json_key(obj, key);
  char *end;

  if (p == NULL || out == NULL)
    {
      return 0;
    }

  *out = strtof(p, &end);
  return (end != p);
}

static int vg_finite_f(float f)
{
  return (f == f) && (f <= 1.0e30f) && (f >= -1.0e30f);
}

static void point_defaults(FAR struct vg_point_entry *p)
{
  memset(p, 0, sizeof(*p));
  p->fc = 3;
  p->qty = 1;
  snprintf(p->dtype, sizeof(p->dtype), "int16");
  p->scale = 1.0f;
  p->fail_n = 3;
}

static int parse_one_point(FAR const char *obj, FAR struct vg_point_entry *p)
{
  long v;
  float f;
  char tmp[24];

  if (obj == NULL || p == NULL)
    {
      return -EINVAL;
    }

  point_defaults(p);
  if (!json_str(obj, "id", p->id, sizeof(p->id)) ||
      !vg_point_validate_id(p->id))
    {
      return -EINVAL;
    }

  if (json_str(obj, "name", p->name, sizeof(p->name)))
    {
      if (!vg_point_validate_name(p->name))
        {
          return -EINVAL;
        }
    }
  else
    {
      memcpy(p->name, p->id, strlen(p->id) + 1);
    }

  if (!json_long(obj, "addr", &v) || v < 1 || v > 247)
    {
      return -EINVAL;
    }

  p->addr = (uint8_t)v;
  if (json_long(obj, "fc", &v) && (v == 3 || v == 4))
    {
      p->fc = (uint8_t)v;
    }

  if (!json_long(obj, "reg", &v) || v < 0 || v > 65535)
    {
      return -EINVAL;
    }

  p->reg = (uint16_t)v;
  if (json_long(obj, "qty", &v) && v >= 1 && v <= 4)
    {
      p->qty = (uint16_t)v;
    }

  if (json_str(obj, "dtype", tmp, sizeof(tmp)))
    {
      if (strcmp(tmp, "int16") == 0 || strcmp(tmp, "uint16") == 0)
        {
          memcpy(p->dtype, tmp, strlen(tmp) + 1);
        }
    }

  if (json_float(obj, "scale", &f) && vg_finite_f(f))
    {
      p->scale = f;
    }

  (void)json_str(obj, "unit", p->unit, sizeof(p->unit));
  if (json_str(obj, "cmp", tmp, sizeof(tmp)))
    {
      if (strcmp(tmp, "ge") == 0 || strcmp(tmp, "le") == 0 ||
          strcmp(tmp, "eq") == 0)
        {
          memcpy(p->cmp, tmp, strlen(tmp) + 1);
        }
    }

  if (json_float(obj, "warn", &f) && vg_finite_f(f))
    {
      p->has_warn = 1;
      p->warn = f;
    }

  if (json_float(obj, "crit", &f) && vg_finite_f(f))
    {
      p->has_crit = 1;
      p->crit = f;
    }

  if (json_long(obj, "fail_n", &v) && v >= 1 && v <= 20)
    {
      p->fail_n = (uint8_t)v;
    }

  return 0;
}

int vg_point_table_read(FAR struct vg_discover_summary *sum,
                        FAR const char *path)
{
  FILE *fp;
  char *buf;
  char obj[512];
  size_t nread;
  FAR const char *p;
  FAR const char *arr;
  long baud;
  int ret = 0;

  if (sum == NULL || path == NULL || path[0] == '\0')
    {
      return -EINVAL;
    }

  memset(sum, 0, sizeof(*sum));
  sum->baud = 9600;
  snprintf(sum->devpath, sizeof(sum->devpath), "/dev/rs485");

  /* Heap: NSH vgpoint/vgdiscover stacks are 16K; a local 8K JSON buffer plus
   * vg_discover_summary overflows and panics in IDLE. */
  buf = (char *)malloc(VG_POINTS_JSON_MAX);
  if (buf == NULL)
    {
      return -ENOMEM;
    }

  fp = fopen(path, "r");
  if (fp == NULL)
    {
      ret = -errno;
      goto out;
    }

  nread = fread(buf, 1, VG_POINTS_JSON_MAX - 1, fp);
  fclose(fp);
  buf[nread] = '\0';
  if (nread == VG_POINTS_JSON_MAX - 1)
    {
      ret = -EFBIG;
      goto out;
    }

  if (json_str(buf, "device", sum->devpath, sizeof(sum->devpath)) == 0)
    {
      snprintf(sum->devpath, sizeof(sum->devpath), "/dev/rs485");
    }

  if (json_long(buf, "baud", &baud) && baud > 0)
    {
      sum->baud = (int)baud;
    }

  arr = strstr(buf, "\"points\"");
  if (arr == NULL)
    {
      goto out;
    }

  arr = strchr(arr, '[');
  if (arr == NULL)
    {
      goto out;
    }

  p = arr + 1;
  while (*p != '\0' && *p != ']' && sum->n_points < VG_DISCOVER_MAX_POINTS)
    {
      while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r' || *p == ',')
        {
          p++;
        }

      if (*p == ']' || *p == '\0')
        {
          break;
        }

      if (*p != '{')
        {
          p++;
          continue;
        }

      ret = json_copy_object(p, obj, sizeof(obj));
      if (ret != 0)
        {
          goto out;
        }

      if (parse_one_point(obj, &sum->points[sum->n_points]) == 0 &&
          vg_point_table_find_id(sum, sum->points[sum->n_points].id) < 0)
        {
          sum->n_points++;
        }

      p++;
      {
        int depth = 1;

        while (*p != '\0' && depth > 0)
          {
            if (*p == '{')
              {
                depth++;
              }
            else if (*p == '}')
              {
                depth--;
              }

            p++;
          }
      }
    }

out:
  free(buf);
  return ret;
}

int vg_point_table_ensure_candidate(FAR struct vg_discover_summary *sum,
                                    FAR const char *cand_path,
                                    FAR const char *committed_path)
{
  int ret;

  if (sum == NULL || cand_path == NULL)
    {
      return -EINVAL;
    }

  ret = vg_point_table_read(sum, cand_path);
  if (ret == 0)
    {
      return 0;
    }

  if (committed_path != NULL)
    {
      ret = vg_point_table_read(sum, committed_path);
      if (ret == 0)
        {
          return vg_point_table_write_candidate(sum, cand_path);
        }
    }

  memset(sum, 0, sizeof(*sum));
  sum->baud = 9600;
  snprintf(sum->devpath, sizeof(sum->devpath), "/dev/rs485");
  return vg_point_table_write_candidate(sum, cand_path);
}
