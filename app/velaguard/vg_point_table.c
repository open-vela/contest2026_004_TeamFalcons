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
#include <sys/statfs.h>
#include <time.h>
#include <math.h>
#include <pthread.h>
#include <netutils/cJSON.h>
#include "vg_file_store.h"
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

void vg_mqtt_notify_point_table(void) __attribute__((weak));

#ifdef CONFIG_VG_CONFIG_STORE
#include "vg_config_store.h"
#endif

#ifndef TMPFS_MAGIC
#  define TMPFS_MAGIC 0x01021994
#endif

#define VG_POINTS_JSON_MAX VG_POINT_TABLE_JSON_MAX

#define VG_DISC_STATE_MAGIC  0x56474453u
#define VG_DISC_STATE_PATH   "/data/velaguard/discover/discover_state.bin"

/* NuttX declares this in <sys/statfs.h>; the desktop libc used by the host
 * tests does not, and the value is the same on both.
 */

#ifndef PROC_SUPER_MAGIC
#  define PROC_SUPER_MAGIC 0x9fa0
#endif

struct vg_disc_state_file
{
  uint32_t magic;
  uint32_t version;
  struct vg_discover_summary sum;
};

static struct vg_discover_summary g_sum;
static pthread_mutex_t g_commit_mtx = PTHREAD_MUTEX_INITIALIZER;


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
  char *end;
  long a;
  long b;
  if (spec == NULL || min_out == NULL || max_out == NULL || *spec == '\0')
    return false;
  errno = 0;
  a = strtol(spec, &end, 0);
  if (end == spec || errno != 0) return false;
  b = a;
  if (*end == '-')
    {
      const char *next = end + 1;
      b = strtol(next, &end, 0);
      if (end == next || errno != 0) return false;
    }
  if (*end != '\0' || a < 1 || b > 247 || a > b) return false;
  *min_out = (int)a;
  *max_out = (int)b;
  return true;
}

static int mkdir_p(FAR const char *path)
{
  char tmp[128];
  struct stat st;
  char *p;
  size_t len;

  if (path == NULL)
    {
      return -EINVAL;
    }

  if (strlen(path) >= sizeof(tmp)) return -ENAMETOOLONG;
  memcpy(tmp, path, strlen(path) + 1);
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

  /* An existing leaf directory is success, not failure.  Callers use the
   * return value to tell "cannot create" apart from "already there"; the
   * raw mkdir() result cannot express that.
   */

  if (mkdir(tmp, 0755) != 0)
    {
      if (errno != EEXIST)
        {
          return -errno;
        }

      /* EEXIST only says the name is taken.  When a plain file sits where
       * the directory belongs, the caller's fopen would fail with ENOTDIR
       * and report a bare write failure; naming it here points at the
       * actual cause.
       */

      if (stat(tmp, &st) != 0 || !S_ISDIR(st.st_mode))
        {
          return -ENOTDIR;
        }
    }

  return 0;
}

static int mkdir_parent(FAR const char *filepath)
{
  char tmp[160];
  char *slash;

  if (filepath == NULL || filepath[0] == '\0')
    {
      return -EINVAL;
    }

  if (strlen(filepath) >= sizeof(tmp)) return -ENAMETOOLONG;
  memcpy(tmp, filepath, strlen(filepath) + 1);
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
  const char *out = path && *path ? path : VG_DISC_STATE_PATH;
  struct vg_disc_state_file *file;
  int rc;
  if (sum == NULL) return -EINVAL;
  rc = mkdir_parent(out);
  if (rc) return rc;
  file = malloc(sizeof(*file));
  if (!file) return -ENOMEM;
  file->magic = VG_DISC_STATE_MAGIC;
  file->version = 2;
  file->sum = *sum;
  rc = vg_file_replace(out, file, sizeof(*file), 0644);
  free(file);
  return rc;
}

int vg_discover_state_load(FAR struct vg_discover_summary *sum,
                           FAR const char *path)
{
  const char *in = path && *path ? path : VG_DISC_STATE_PATH;
  struct vg_disc_state_file *file;
  FILE *fp;
  int rc = -EINVAL;
  if (!sum) return -EINVAL;
  file = malloc(sizeof(*file));
  if (!file) return -ENOMEM;
  fp = fopen(in, "r");
  if (!fp) { rc = -errno; goto done; }
  size_t n = fread(file, 1, sizeof(*file), fp);
  if (ferror(fp)) rc = -EIO;
  else if (n == sizeof(*file) && fgetc(fp) == EOF &&
           file->magic == VG_DISC_STATE_MAGIC && file->version == 2 &&
           file->sum.n_hits >= 0 && file->sum.n_hits <= VG_DISCOVER_MAX_SLAVES &&
           file->sum.n_blocks >= 0 && file->sum.n_blocks <= VG_DISCOVER_MAX_BLOCKS &&
           file->sum.n_points >= 0 && file->sum.n_points <= VG_DISCOVER_MAX_POINTS)
    { *sum = file->sum; rc = 0; }
  fclose(fp);
 done:
  free(file);
  return rc;
}

static void add_point(FAR struct vg_discover_summary *sum,
                      uint8_t addr, uint8_t fc, uint16_t reg,
                      FAR const char *id, FAR const char *dtype,
                      float scale, FAR const char *unit)
{
  FAR struct vg_point_entry *p;

  if (sum->n_points >= VG_DISCOVER_MAX_POINTS)
    {
      sum->flags |= VG_DISCOVER_TRUNCATED;
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
          snprintf(id, sizeof(id), "s%u_r%u_%u",
                   (unsigned)b->addr,
                   (unsigned)(uint16_t)(b->start + (uint16_t)tag_idx),
                   (unsigned)(uint8_t)tag_idx);

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

static int valid_point(const struct vg_point_entry *p)
{
  if (!memchr(p->unit, 0, sizeof(p->unit))) return 0;
  for (const unsigned char *u = (const unsigned char *)p->unit; *u; u++)
    if (*u <= 0x20 || *u == 0x7f) return 0;
  if (p->has_warn && p->has_crit && memchr(p->cmp, 0, sizeof(p->cmp)))
    {
      if (!strcmp(p->cmp, "ge") && p->warn > p->crit) return 0;
      if (!strcmp(p->cmp, "le") && p->warn < p->crit) return 0;
    }
  return memchr(p->id, 0, sizeof(p->id)) && vg_point_validate_id(p->id) &&
    memchr(p->name, 0, sizeof(p->name)) &&
    (p->name[0] == 0 || vg_point_validate_name(p->name)) &&
    memchr(p->unit, 0, sizeof(p->unit)) &&
    memchr(p->dtype, 0, sizeof(p->dtype)) &&
    (!strcmp(p->dtype, "int16") || !strcmp(p->dtype, "uint16")) &&
    memchr(p->cmp, 0, sizeof(p->cmp)) &&
    (!p->cmp[0] || !strcmp(p->cmp, "ge") || !strcmp(p->cmp, "le") ||
     !strcmp(p->cmp, "eq")) &&
    p->addr >= 1 && p->addr <= 247 && (p->fc == 3 || p->fc == 4) &&
    p->qty >= 1 && p->qty <= 4 && isfinite(p->scale) &&
    (!p->has_warn || isfinite(p->warn)) &&
    (!p->has_crit || isfinite(p->crit)) && p->fail_n <= 20;
}

static int write_point_table(FAR const struct vg_discover_summary *sum,
                              FAR const char *path, int create_only)
{
  cJSON *doc = NULL;
  cJSON *bus;
  cJSON *points;
  cJSON *hits;
  char *body = NULL;
  int rc = -ENOMEM;
  int i;
  int j;
  if (sum == NULL || path == NULL || sum->n_points < 0 ||
      sum->n_points > VG_DISCOVER_MAX_POINTS || sum->n_hits < 0 ||
      sum->n_hits > VG_DISCOVER_MAX_SLAVES ||
      !memchr(sum->devpath, 0, sizeof(sum->devpath))) return -EINVAL;
  for (i = 0; i < sum->n_points; i++)
    {
      if (!valid_point(&sum->points[i])) return -EINVAL;
      for (j = 0; j < i; j++)
        if (!strcmp(sum->points[j].id, sum->points[i].id)) return -EINVAL;
    }
  rc = mkdir_parent(path);
  if (rc != 0) return rc;
  rc = -ENOMEM;
  doc = cJSON_CreateObject();
  if (doc == NULL) goto done;
#define ADD(x) do { if (!(x)) goto done; } while (0)
  ADD(cJSON_AddNumberToObject(doc, "schema_version", 1));
  ADD(bus = cJSON_AddObjectToObject(doc, "bus"));
  ADD(cJSON_AddStringToObject(bus, "device", sum->devpath[0] ? sum->devpath : "/dev/rs485"));
  ADD(cJSON_AddNumberToObject(bus, "baud", sum->baud ? sum->baud : 9600));
  ADD(hits = cJSON_AddArrayToObject(doc, "hits"));
  for (i = 0; i < sum->n_hits; i++)
    {
      cJSON *hit = cJSON_CreateNumber(sum->hits[i].addr);
      if (hit == NULL) goto done;
      if (!cJSON_AddItemToArray(hits, hit)) { cJSON_Delete(hit); goto done; }
    }
  ADD(points = cJSON_AddArrayToObject(doc, "points"));
  for (i = 0; i < sum->n_points; i++)
    {
      const struct vg_point_entry *p = &sum->points[i];
      cJSON *obj = cJSON_CreateObject();
      if (obj == NULL) goto done;
      if (!cJSON_AddItemToArray(points, obj)) { cJSON_Delete(obj); goto done; }
      ADD(cJSON_AddStringToObject(obj, "id", p->id));
      ADD(cJSON_AddStringToObject(obj, "name", p->name[0] ? p->name : p->id));
      ADD(cJSON_AddNumberToObject(obj, "addr", p->addr));
      ADD(cJSON_AddNumberToObject(obj, "fc", p->fc));
      ADD(cJSON_AddNumberToObject(obj, "reg", p->reg));
      ADD(cJSON_AddNumberToObject(obj, "qty", p->qty));
      ADD(cJSON_AddStringToObject(obj, "dtype", p->dtype));
      ADD(cJSON_AddNumberToObject(obj, "scale", p->scale));
      ADD(cJSON_AddStringToObject(obj, "unit", p->unit));
      if (p->cmp[0]) ADD(cJSON_AddStringToObject(obj, "cmp", p->cmp));
      if (p->has_warn) ADD(cJSON_AddNumberToObject(obj, "warn", p->warn));
      if (p->has_crit) ADD(cJSON_AddNumberToObject(obj, "crit", p->crit));
      ADD(cJSON_AddNumberToObject(obj, "fail_n", p->fail_n ? p->fail_n : 3));
    }
  ADD(body = cJSON_PrintUnformatted(doc));
  rc = strlen(body) < VG_POINTS_JSON_MAX - 1 ?
       (create_only ? vg_file_create(path, body, strlen(body), 0644) :
                      vg_file_replace(path, body, strlen(body), 0644)) : -EFBIG;
#undef ADD
 done:
  cJSON_free(body);
  cJSON_Delete(doc);
  return rc;
}

int vg_point_table_write_candidate(const struct vg_discover_summary *sum,
                                   const char *path)
{
  return write_point_table(sum, path, 0);
}

int vg_point_table_create_candidate(const struct vg_discover_summary *sum,
                                    const char *path)
{
  return write_point_table(sum, path, 1);
}

int vg_point_table_commit(FAR const struct vg_discover_summary *sum,
                          FAR const char *points_path)
{
  int rc;
  /* Keep disk replacement, RAM generation and notification in the same
   * order for every submitter. The file-store mutex alone ends too early. */
  pthread_mutex_lock(&g_commit_mtx);
  rc = vg_point_table_write_candidate(sum, points_path);
  if (rc == 0)
    {
      (void)vg_live_points_replace(sum);
      if (vg_mqtt_notify_point_table != NULL) vg_mqtt_notify_point_table();
    }
  pthread_mutex_unlock(&g_commit_mtx);
  return rc;
}

int vg_point_table_apply(FAR const struct vg_discover_summary *sum,
                         FAR const char *points_path,
                         FAR const char *config_basedir,
                         bool confirm)
{
  if (sum == NULL || points_path == NULL || sum->n_points <= 0)
    {
      return -EINVAL;
    }

  if (!confirm)
    {
      printf("vgdiscover: dry-run apply → %d points to %s (use --confirm)\n",
             sum->n_points, points_path);
      return 0;
    }

  if (vg_point_table_commit(sum, points_path) != 0)
    {
      return -EIO;
    }

  (void)config_basedir;

  printf("vgdiscover: applied %d points → %s\n", sum->n_points, points_path);
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

int vg_point_table_read_slaves(FAR const char *path, uint8_t *addrs, int max)
{
  struct vg_discover_summary *table;
  int rc;
  int n = 0;
  if (!path || !addrs || max <= 0) return -EINVAL;
  table = malloc(sizeof(*table));
  if (!table) return -ENOMEM;
  rc = vg_point_table_read(table, path);
  if (rc == 0)
    {
      for (int i = 0; i < table->n_hits; i++)
        append_unique_addr(addrs, &n, max, table->hits[i].addr);
      for (int i = 0; i < table->n_points; i++)
        append_unique_addr(addrs, &n, max, table->points[i].addr);
    }
  free(table);
  return rc == 0 ? n : rc;
}

static pthread_mutex_t g_bus_mtx = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t g_live_mtx = PTHREAD_MUTEX_INITIALIZER;
static struct vg_discover_summary g_live;
static uint32_t g_live_gen;
static struct vg_live_snapshot g_memory_snapshot;
static uint32_t g_memory_generation;

int vg_bus_try_lock(void)
{
  return pthread_mutex_trylock(&g_bus_mtx) == 0 ? 0 : -EBUSY;
}

int vg_bus_lock_timeout(uint32_t timeout_ms)
{
  uint32_t start = vg_live_now_ms();
  do
    {
      if (vg_bus_try_lock() == 0) return 0;
      if (vg_live_now_ms() - start >= timeout_ms) break;
      usleep(1000);
    }
  while (1);
  return -EBUSY;
}

void vg_bus_unlock(void)
{
  pthread_mutex_unlock(&g_bus_mtx);
}

int vg_bus_is_locked(void)
{
  if (vg_bus_try_lock() != 0) return 1;
  vg_bus_unlock();
  return 0;
}

int vg_live_points_replace(FAR const struct vg_discover_summary *sum)
{
  pthread_mutex_lock(&g_live_mtx);
  if (sum == NULL) memset(&g_live, 0, sizeof(g_live));
  else memcpy(&g_live, sum, sizeof(g_live));
  if (++g_live_gen == 0) ++g_live_gen;
  memset(&g_memory_snapshot, 0, sizeof(g_memory_snapshot));
  g_memory_generation = 0;
  pthread_mutex_unlock(&g_live_mtx);
  return 0;
}

int vg_live_points_load(FAR const char *path)
{
  struct vg_discover_summary *next = malloc(sizeof(*next));
  int rc;
  if (next == NULL) return -ENOMEM;
  pthread_mutex_lock(&g_commit_mtx);
  rc = vg_point_table_read(next, path);
  /* Failed storage reads must not erase an already running configuration. */
  if (rc == 0) vg_live_points_replace(next);
  pthread_mutex_unlock(&g_commit_mtx);
  free(next);
  return rc;
}

uint32_t vg_live_points_gen(void)
{
  uint32_t gen;
  pthread_mutex_lock(&g_live_mtx);
  gen = g_live_gen;
  pthread_mutex_unlock(&g_live_mtx);
  return gen;
}

int vg_live_points_copy(FAR struct vg_discover_summary *out)
{
  return vg_live_points_copy_versioned(out, NULL);
}

int vg_live_points_copy_versioned(FAR struct vg_discover_summary *out,
                                 FAR uint32_t *generation)
{
  int n;
  if (out == NULL) return -EINVAL;
  pthread_mutex_lock(&g_live_mtx);
  memcpy(out, &g_live, sizeof(*out));
  if (generation != NULL) *generation = g_live_gen;
  n = g_live.n_points;
  pthread_mutex_unlock(&g_live_mtx);
  return n;
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

int vg_point_store_root_ok(FAR const char *root)
{
  struct statfs st;

  if (root == NULL || root[0] == '\0')
    {
      return -EINVAL;
    }

  if (statfs(root, &st) != 0)
    {
      return (errno != 0) ? -errno : -EIO;
    }

  /* The pseudo filesystem accepts mkdir() and file creation, backed by RAM.
   * A store that fell back there - eMMC not mounted, /data never linked -
   * answers every write until the next reboot discards it.
   */

  return (st.f_type == PROC_SUPER_MAGIC || st.f_type == TMPFS_MAGIC) ? -ENODEV : 0;
}

FAR const char *vg_point_table_err_token(int ret)
{
  /* Stable tokens, not numbers: errno values differ across the board, the
   * host and the desktop tests.  Only [A-Za-z0-9_.-] may appear here.
   */

  switch (-ret)
    {
      case ENOENT:
        return "enoent";

      case ENOTDIR:
        return "enotdir";

      case EROFS:
        return "erofs";

      case ENOSPC:
        return "enospc";

      case ENOMEM:
        return "enomem";

      case EACCES:
        return "eacces";

      case ENODEV:
        return "enodev";

      case EFBIG:
        return "efbig";

      case EINVAL:
        return "einval";

      default:
        return "eio";
    }
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

  if (fclose(fp) != 0) { unlink(tmp); return -EIO; }
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
  int have_tick = 0;
  int have_count = 0;
  int rc = -EINVAL;
  if (!path || !*path || !snap) return -EINVAL;
  memset(snap, 0, sizeof(*snap));
  fp = fopen(path, "r");
  if (!fp) return -errno;
  while (fgets(line, sizeof(line), fp))
    {
      int end = 0;
      unsigned tick;
      unsigned ok;
      char id[VG_POINT_ID_MAX];
      char value[32];
      char unit[8];
      size_t len = strlen(line);
      if (len == sizeof(line) - 1 && line[len - 1] != '\n') goto done;
      while (len && (line[len - 1] == '\n' || line[len - 1] == '\r')) line[--len] = 0;
      if (!len || line[0] == '#') continue;
      if (sscanf(line, "tick_ms=%u%n", &tick, &end) == 1 && line[end] == 0)
        {
          if (have_tick || declared_n >= 0) goto done;
          snap->tick_ms = tick; have_tick = 1; continue;
        }
      end = 0;
      if (sscanf(line, "n=%d%n", &declared_n, &end) == 1 && line[end] == 0)
        {
          if (!have_tick || have_count || snap->n || declared_n < 0 || declared_n > VG_DISCOVER_MAX_POINTS) goto done;
          have_count = 1;
          continue;
        }
      end = 0;
      if (!have_tick || declared_n < 0 || snap->n >= declared_n ||
          sscanf(line, "id=%23s value=%31s ok=%u unit=%7s%n", id, value, &ok, unit, &end) != 4 ||
          line[end] != 0 || !vg_point_validate_id(id) || ok > 1 ||
          vg_live_snapshot_find_id(snap, id) >= 0) goto done;
      struct vg_live_sample *sample = &snap->samples[snap->n];
      strcpy(sample->id, id);
      if (strcmp(unit, "-")) strcpy(sample->unit, unit);
      if (ok)
        {
          char *tail;
          errno = 0;
          sample->value = strtof(value, &tail);
          if (tail == value || *tail || errno || !isfinite(sample->value)) goto done;
          sample->ok = 1;
        }
      else if (strcmp(value, "-")) goto done;
      snap->n++;
    }
  if (have_tick && declared_n == snap->n) rc = 0;
 done:
  if (ferror(fp)) rc = -EIO;
  if (fclose(fp) && rc == 0) rc = -EIO;
  if (rc) memset(snap, 0, sizeof(*snap));
  return rc;
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

static int json_string(const cJSON *obj, const char *key, char *out,
                       size_t cap, int required)
{
  const cJSON *v = cJSON_GetObjectItemCaseSensitive(obj, key);
  if (v == NULL) return required ? -EINVAL : 0;
  if (!cJSON_IsString(v) || strlen(v->valuestring) >= cap) return -EINVAL;
  memcpy(out, v->valuestring, strlen(v->valuestring) + 1);
  return 0;
}

static int json_number(const cJSON *obj, const char *key, double *out,
                       double min, double max, int integer, int required)
{
  const cJSON *v = cJSON_GetObjectItemCaseSensitive(obj, key);
  if (v == NULL) return required ? -EINVAL : 0;
  if (!cJSON_IsNumber(v) || !isfinite(v->valuedouble) ||
      v->valuedouble < min || v->valuedouble > max ||
      (integer && v->valuedouble != (double)(long)v->valuedouble)) return -EINVAL;
  *out = v->valuedouble;
  return 0;
}

static int parse_one_point(const cJSON *obj, struct vg_point_entry *p)
{
  double v;
  const cJSON *field;
  memset(p, 0, sizeof(*p));
  p->fc = 3; p->qty = 1; p->scale = 1; p->fail_n = 3;
  strcpy(p->dtype, "int16");
  if (!cJSON_IsObject(obj) ||
      json_string(obj, "id", p->id, sizeof(p->id), 1) ||
      json_string(obj, "name", p->name, sizeof(p->name), 0) ||
      json_string(obj, "unit", p->unit, sizeof(p->unit), 0) ||
      json_string(obj, "dtype", p->dtype, sizeof(p->dtype), 0) ||
      json_string(obj, "cmp", p->cmp, sizeof(p->cmp), 0)) return -EINVAL;
  if (p->name[0] == 0) strcpy(p->name, p->id);
#define NUM(key, dest, low, high, integral, required) do { \
  v = (dest); \
  if (json_number(obj, key, &v, low, high, integral, required)) return -EINVAL; \
  (dest) = v; } while (0)
  NUM("addr", p->addr, 1, 247, 1, 1);
  NUM("reg", p->reg, 0, 65535, 1, 1);
  NUM("fc", p->fc, 3, 4, 1, 0);
  NUM("qty", p->qty, 1, 4, 1, 0);
  NUM("scale", p->scale, -3.402823466e38, 3.402823466e38, 0, 0);
  NUM("fail_n", p->fail_n, 1, 20, 1, 0);
  field = cJSON_GetObjectItemCaseSensitive(obj, "warn");
  if (field != NULL) { NUM("warn", p->warn, -3.402823466e38, 3.402823466e38, 0, 1); p->has_warn = 1; }
  field = cJSON_GetObjectItemCaseSensitive(obj, "crit");
  if (field != NULL) { NUM("crit", p->crit, -3.402823466e38, 3.402823466e38, 0, 1); p->has_crit = 1; }
#undef NUM
  return valid_point(p) ? 0 : -EINVAL;
}

int vg_point_table_read(FAR struct vg_discover_summary *sum,
                        FAR const char *path)
{
  FILE *fp;
  char *buf;
  cJSON *doc = NULL;
  const cJSON *points;
  const cJSON *item;
  const cJSON *bus;
  const cJSON *hits;
  size_t n;
  int rc = -EINVAL;
  int i;
  double baud = 9600;
  if (sum == NULL || path == NULL || path[0] == 0) return -EINVAL;
  memset(sum, 0, sizeof(*sum));
  buf = malloc(VG_POINTS_JSON_MAX);
  if (buf == NULL) return -ENOMEM;
  fp = fopen(path, "r");
  if (fp == NULL) { rc = errno == ENOTDIR ? -ENOENT : -errno; goto done; }
  n = fread(buf, 1, VG_POINTS_JSON_MAX - 1, fp);
  rc = ferror(fp) ? -EIO : 0;
  if (fclose(fp) != 0 && rc == 0) rc = -EIO;
  if (rc != 0) goto done;
  if (n == VG_POINTS_JSON_MAX - 1) { rc = -EFBIG; goto done; }
  buf[n] = 0;
  rc = -EINVAL;
  doc = cJSON_ParseWithOpts(buf, NULL, 1);
  if (!cJSON_IsObject(doc)) goto done;
  points = cJSON_GetObjectItemCaseSensitive(doc, "points");
  if (!cJSON_IsArray(points)) goto done;
  if (cJSON_GetArraySize(points) > VG_DISCOVER_MAX_POINTS) { rc = -E2BIG; goto done; }
  strcpy(sum->devpath, "/dev/rs485");
  bus = cJSON_GetObjectItemCaseSensitive(doc, "bus");
  if (bus != NULL)
    {
      if (!cJSON_IsObject(bus) ||
          json_string(bus, "device", sum->devpath, sizeof(sum->devpath), 0) ||
          json_number(bus, "baud", &baud, 9600, 9600, 1, 0)) goto done;
    }
  sum->baud = (int)baud;
  hits = cJSON_GetObjectItemCaseSensitive(doc, "hits");
  if (hits != NULL)
    {
      if (!cJSON_IsArray(hits) || cJSON_GetArraySize(hits) > VG_DISCOVER_MAX_SLAVES) goto done;
      cJSON_ArrayForEach(item, hits)
        {
          double addr = item->valuedouble;
          if (!cJSON_IsNumber(item) || addr < 1 || addr > 247 || addr != (int)addr) goto done;
          sum->hits[sum->n_hits].addr = (uint8_t)addr;
          sum->hits[sum->n_hits++].alive = true;
        }
    }
  cJSON_ArrayForEach(item, points)
    {
      if (parse_one_point(item, &sum->points[sum->n_points])) goto done;
      for (i = 0; i < sum->n_points; i++)
        if (!strcmp(sum->points[i].id, sum->points[sum->n_points].id)) goto done;
      sum->n_points++;
    }
  rc = 0;
 done:
  cJSON_Delete(doc);
  free(buf);
  if (rc != 0) memset(sum, 0, sizeof(*sum));
  return rc;
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

  /* Only "no candidate yet" falls through to seeding from the committed
   * table.  A candidate that exists but cannot be read must not be treated
   * as an empty one: the caller would hand back a fresh table, and the next
   * apply --confirm would write it over the committed points.
   */

  if (ret != -ENOENT)
    {
      return ret;
    }

  if (committed_path != NULL)
    {
      ret = vg_point_table_read(sum, committed_path);
      if (ret == 0)
        {
          return vg_point_table_write_candidate(sum, cand_path);
        }

      if (ret != -ENOENT)
        {
          return ret;
        }
    }

  memset(sum, 0, sizeof(*sum));
  sum->baud = 9600;
  snprintf(sum->devpath, sizeof(sum->devpath), "/dev/rs485");
  return vg_point_table_write_candidate(sum, cand_path);
}


int vg_live_snapshot_publish(uint32_t generation,
                             FAR const struct vg_live_snapshot *snap)
{
  if (!snap || snap->n < 0 || snap->n > VG_DISCOVER_MAX_POINTS || !snap->tick_ms)
    return -EINVAL;
  pthread_mutex_lock(&g_live_mtx);
  int ret = -ESTALE;
  if (generation == g_live_gen && generation != 0)
    {
      ret = -EINVAL;
      bool valid = snap->n == g_live.n_points;
      for (int i = 0; valid && i < snap->n; i++)
        valid = strncmp(snap->samples[i].id, g_live.points[i].id,
                        sizeof(snap->samples[i].id)) == 0;
      if (valid)
        {
          g_memory_snapshot = *snap;
          g_memory_generation = generation;
          ret = 0;
        }
    }
  pthread_mutex_unlock(&g_live_mtx);
  return ret;
}

int vg_live_points_view(FAR struct vg_discover_summary *table,
                        FAR struct vg_live_snapshot *snap,
                        FAR uint32_t *generation)
{
  if (!table || !snap) return -EINVAL;
  pthread_mutex_lock(&g_live_mtx);
  *table = g_live;
  if (generation) *generation = g_live_gen;
  if (g_memory_generation == g_live_gen && g_memory_generation != 0)
    *snap = g_memory_snapshot;
  else
    memset(snap, 0, sizeof(*snap));
  int n = g_live.n_points;
  pthread_mutex_unlock(&g_live_mtx);
  return n;
}
