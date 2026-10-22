/****************************************************************************
 * app/velaguard/vg_voice_facts.c
 ****************************************************************************/

#include "vg_voice_facts.h"

#include <pthread.h>
#include <string.h>

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static struct vg_voice_facts g_facts;
static bool g_published;
static uint32_t g_seq;
static uint32_t g_config_version;

void vg_voice_facts_set_config_version(uint32_t version)
{
  pthread_mutex_lock(&g_lock);
  g_config_version = version;
  g_facts.config_version = version;
  pthread_mutex_unlock(&g_lock);
}

static struct vg_voice_facts *facts_begin_locked(void)
{
  memset(&g_facts, 0, sizeof(g_facts));
  g_facts.seq = ++g_seq;
  g_facts.boot_id = vg_runtime_boot_id();
  g_facts.config_version = g_config_version;
  g_published = true;
  return &g_facts;
}

void vg_voice_facts_publish_anomalies(const struct vg_runtime_anomaly *items,
                                      int n, int total, uint32_t age_ms)
{
  struct vg_voice_facts *f;
  int i;

  if (n < 0)
    {
      n = 0;
    }

  if (n > VG_RUNTIME_MAX_POINTS)
    {
      n = VG_RUNTIME_MAX_POINTS;
    }

  pthread_mutex_lock(&g_lock);
  f = facts_begin_locked();
  f->data_age_ms = age_ms;
  f->anomaly_total = total;
  f->anomaly_n = n;

  for (i = 0; i < n; i++)
    {
      f->anomalies[i] = items[i];
    }

  pthread_mutex_unlock(&g_lock);
}

void vg_voice_facts_publish_recovery(const struct vg_runtime_recovery *rec,
                                     uint32_t age_ms)
{
  pthread_mutex_lock(&g_lock);
  facts_begin_locked();

  if (rec != NULL)
    {
      g_facts.has_recovery = true;
      g_facts.recovery = *rec;
    }

  g_facts.data_age_ms = age_ms;
  pthread_mutex_unlock(&g_lock);
}

void vg_voice_facts_publish_point(const char *id, const char *name,
                                  const char *unit, bool online,
                                  bool has_value, float value,
                                  uint32_t age_ms, bool has_warn, float warn,
                                  bool has_crit, float crit)
{
  pthread_mutex_lock(&g_lock);
  facts_begin_locked();
  g_facts.has_point = true;
  g_facts.point_online = online;
  g_facts.point_has_value = has_value;
  g_facts.point_value = value;
  g_facts.point_age_ms = age_ms;
  g_facts.point_has_warn = has_warn;
  g_facts.point_warn = warn;
  g_facts.point_has_crit = has_crit;
  g_facts.point_crit = crit;

  if (id != NULL)
    {
      strncpy(g_facts.point_id, id, sizeof(g_facts.point_id) - 1);
    }

  if (name != NULL)
    {
      strncpy(g_facts.point_name, name, sizeof(g_facts.point_name) - 1);
    }

  if (unit != NULL)
    {
      strncpy(g_facts.point_unit, unit, sizeof(g_facts.point_unit) - 1);
    }

  pthread_mutex_unlock(&g_lock);
}

bool vg_voice_facts_copy(struct vg_voice_facts *out)
{
  bool ok;

  if (out == NULL)
    {
      return false;
    }

  pthread_mutex_lock(&g_lock);
  ok = g_published;
  if (ok)
    {
      *out = g_facts;
    }

  pthread_mutex_unlock(&g_lock);
  return ok;
}

bool vg_voice_facts_copy_seq(uint32_t seq, struct vg_voice_facts *out)
{
  bool ok;

  if (out == NULL)
    {
      return false;
    }

  pthread_mutex_lock(&g_lock);
  ok = g_published && g_facts.seq == seq;
  if (ok)
    {
      *out = g_facts;
    }

  pthread_mutex_unlock(&g_lock);
  return ok;
}

uint32_t vg_voice_facts_seq(void)
{
  uint32_t seq;

  pthread_mutex_lock(&g_lock);
  seq = g_published ? g_facts.seq : 0;
  pthread_mutex_unlock(&g_lock);
  return seq;
}
