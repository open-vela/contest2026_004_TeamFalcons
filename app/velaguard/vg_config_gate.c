/****************************************************************************
 * app/velaguard/vg_config_gate.c
 ****************************************************************************/

#include <errno.h>
#include <pthread.h>

#include "vg_config_gate.h"

static pthread_mutex_t g_gate_mutex = PTHREAD_MUTEX_INITIALIZER;
static enum vg_config_gate_owner_e g_gate_owner = VG_CONFIG_GATE_NONE;
static uint32_t g_gate_token;
static uint32_t g_next_token = 1;

static bool valid_owner(enum vg_config_gate_owner_e owner)
{
  return owner >= VG_CONFIG_GATE_NSH && owner <= VG_CONFIG_GATE_OTA;
}

int vg_config_gate_acquire(enum vg_config_gate_owner_e owner,
                           uint32_t *token, bool *created)
{
  int ret = 0;

  if (token == NULL || created == NULL || !valid_owner(owner))
    {
      return -EINVAL;
    }

  pthread_mutex_lock(&g_gate_mutex);
  *created = false;
  if (g_gate_owner == VG_CONFIG_GATE_NONE)
    {
      g_gate_owner = owner;
      g_gate_token = g_next_token++;
      if (g_gate_token == 0)
        {
          g_gate_token = g_next_token++;
        }

      if (g_next_token == 0)
        {
          g_next_token = 1;
        }

      *created = true;
    }
  else if (g_gate_owner != owner)
    {
      ret = -EBUSY;
    }

  *token = g_gate_token;
  pthread_mutex_unlock(&g_gate_mutex);
  return ret;
}

int vg_config_gate_release(enum vg_config_gate_owner_e owner,
                           uint32_t token)
{
  int ret = 0;

  if (!valid_owner(owner) || token == 0)
    {
      return -EINVAL;
    }

  pthread_mutex_lock(&g_gate_mutex);
  if (g_gate_owner == VG_CONFIG_GATE_NONE)
    {
      ret = -EALREADY;
    }
  else if (g_gate_owner != owner || g_gate_token != token)
    {
      ret = -ESTALE;
    }
  else
    {
      g_gate_owner = VG_CONFIG_GATE_NONE;
      g_gate_token = 0;
    }

  pthread_mutex_unlock(&g_gate_mutex);
  return ret;
}

int vg_config_gate_token(enum vg_config_gate_owner_e owner,
                         uint32_t *token)
{
  int ret;

  if (token == NULL || !valid_owner(owner))
    {
      return -EINVAL;
    }

  pthread_mutex_lock(&g_gate_mutex);
  if (g_gate_owner == owner)
    {
      *token = g_gate_token;
      ret = 0;
    }
  else
    {
      *token = 0;
      ret = -EBUSY;
    }

  pthread_mutex_unlock(&g_gate_mutex);
  return ret;
}

bool vg_config_gate_owns(enum vg_config_gate_owner_e owner,
                         uint32_t token)
{
  bool owned;

  if (!valid_owner(owner) || token == 0)
    {
      return false;
    }

  pthread_mutex_lock(&g_gate_mutex);
  owned = g_gate_owner == owner && g_gate_token == token;
  pthread_mutex_unlock(&g_gate_mutex);
  return owned;
}

enum vg_config_gate_owner_e vg_config_gate_current(void)
{
  enum vg_config_gate_owner_e owner;

  pthread_mutex_lock(&g_gate_mutex);
  owner = g_gate_owner;
  pthread_mutex_unlock(&g_gate_mutex);
  return owner;
}

bool vg_config_gate_is_idle(void)
{
  return vg_config_gate_current() == VG_CONFIG_GATE_NONE;
}

const char *vg_config_gate_owner_name(enum vg_config_gate_owner_e owner)
{
  switch (owner)
    {
      case VG_CONFIG_GATE_NONE:
        return "none";
      case VG_CONFIG_GATE_NSH:
        return "nsh";
      case VG_CONFIG_GATE_REMOTE:
        return "remote";
      case VG_CONFIG_GATE_SCAN:
        return "scan";
      case VG_CONFIG_GATE_OTA:
        return "ota";
      default:
        return "invalid";
    }
}
