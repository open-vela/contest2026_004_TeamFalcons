/****************************************************************************
 * app/velaguard/vg_config_confirmation.c
 ****************************************************************************/

#include <errno.h>
#include <string.h>

#include "vg_config_confirmation.h"

static bool same_hash(bool has_a, const char *a, bool has_b, const char *b)
{
  return has_a == has_b && (!has_a || strcmp(a, b) == 0);
}

struct confirmation_identity
{
  char task_id[VG_CONFIG_ID_MAX];
  char session_id[VG_CONFIG_HEX32_LEN];
  uint32_t revision;
  bool has_base_hash;
  char base_hash[VG_CONFIG_HASH_LEN];
  char payload_hash[VG_CONFIG_HASH_LEN];
  char candidate_hash[VG_CONFIG_HASH_LEN];
  char confirmation_id[VG_CONFIG_HEX32_LEN];
};

static void capture_identity(const struct vg_config_pending_preview *pending,
                             struct confirmation_identity *identity)
{
  memset(identity, 0, sizeof(*identity));
  memcpy(identity->task_id, pending->task_id, sizeof(identity->task_id));
  memcpy(identity->session_id, pending->session_id,
         sizeof(identity->session_id));
  identity->revision = pending->revision;
  identity->has_base_hash = pending->has_base_hash;
  memcpy(identity->base_hash, pending->base_hash,
         sizeof(identity->base_hash));
  memcpy(identity->payload_hash, pending->payload_hash,
         sizeof(identity->payload_hash));
  memcpy(identity->candidate_hash, pending->candidate_hash,
         sizeof(identity->candidate_hash));
  memcpy(identity->confirmation_id, pending->confirmation_id,
         sizeof(identity->confirmation_id));
}

static bool same_identity(const struct confirmation_identity *a,
                          const struct confirmation_identity *b)
{
  return strcmp(a->task_id, b->task_id) == 0 &&
         strcmp(a->session_id, b->session_id) == 0 &&
         a->revision == b->revision &&
         same_hash(a->has_base_hash, a->base_hash,
                   b->has_base_hash, b->base_hash) &&
         strcmp(a->payload_hash, b->payload_hash) == 0 &&
         strcmp(a->candidate_hash, b->candidate_hash) == 0 &&
         strcmp(a->confirmation_id, b->confirmation_id) == 0;
}

static void invalidate_locked(struct vg_config_confirmation *confirmation)
{
  memset(&confirmation->pending, 0, sizeof(confirmation->pending));
  confirmation->have_pending = false;
  confirmation->visible = false;
  confirmation->suppressed = false;
  confirmation->action_in_flight = false;
  confirmation->action_claimed = false;
  confirmation->generation++;
  if (confirmation->generation == 0)
    {
      confirmation->generation = 1;
    }
}

static void apply_loaded_locked(
  struct vg_config_confirmation *confirmation,
  bool changed,
  bool alarm_active)
{
  bool alarm_changed = confirmation->alarm_blocked != alarm_active;

  if (alarm_changed)
    {
      confirmation->generation++;
      if (confirmation->generation == 0)
        {
          confirmation->generation = 1;
        }
      if (alarm_active)
        {
          confirmation->suppressed = false;
          confirmation->action_in_flight = false;
          confirmation->action_claimed = false;
        }
    }
  confirmation->alarm_blocked = alarm_active;

  if (changed)
    {
      confirmation->have_pending = confirmation->pending.valid;
      confirmation->suppressed = false;
      confirmation->action_in_flight = false;
      confirmation->action_claimed = false;
      confirmation->generation++;
      if (confirmation->generation == 0)
        {
          confirmation->generation = 1;
        }
    }

  confirmation->visible =
    confirmation->have_pending && !confirmation->alarm_blocked &&
    !confirmation->suppressed && !confirmation->action_in_flight;
}

int vg_config_confirmation_init(
  struct vg_config_confirmation *confirmation,
  const struct vg_config_confirmation_ports *ports)
{
  int ret;

  if (confirmation == NULL)
    {
      return -EINVAL;
    }

  memset(confirmation, 0, sizeof(*confirmation));
  ret = pthread_mutex_init(&confirmation->lock, NULL);
  if (ret != 0)
    {
      memset(confirmation, 0, sizeof(*confirmation));
      return -ret;
    }

  confirmation->initialized = true;
  confirmation->generation = 1;
  if (ports != NULL)
    {
      confirmation->ports = *ports;
    }

  return 0;
}

int vg_config_confirmation_bind(
  struct vg_config_confirmation *confirmation,
  const struct vg_config_confirmation_ports *ports)
{
  if (confirmation == NULL || !confirmation->initialized ||
      ports == NULL || ports->load_pending == NULL ||
      ports->submit == NULL)
    {
      return -EINVAL;
    }

  pthread_mutex_lock(&confirmation->lock);
  confirmation->ports = *ports;
  invalidate_locked(confirmation);
  pthread_mutex_unlock(&confirmation->lock);
  return 0;
}

void vg_config_confirmation_deinit(
  struct vg_config_confirmation *confirmation)
{
  if (confirmation == NULL || !confirmation->initialized)
    {
      return;
    }

  pthread_mutex_destroy(&confirmation->lock);
  memset(confirmation, 0, sizeof(*confirmation));
}

int vg_config_confirmation_sync(struct vg_config_confirmation *confirmation,
                                bool alarm_active)
{
  struct confirmation_identity old_identity;
  struct confirmation_identity new_identity;
  struct vg_config_confirmation_ports ports;
  bool changed;
  bool have_loaded = false;
  bool had_pending;
  int ret;

  if (confirmation == NULL || !confirmation->initialized)
    {
      return -EINVAL;
    }

  pthread_mutex_lock(&confirmation->lock);
  had_pending = confirmation->have_pending;
  if (had_pending)
    {
      capture_identity(&confirmation->pending, &old_identity);
    }

  ports = confirmation->ports;
  memset(&confirmation->pending, 0, sizeof(confirmation->pending));
  ret = ports.load_pending != NULL ?
        ports.load_pending(ports.ctx, &confirmation->pending) : -ENOSYS;
  if (ret == 0 && confirmation->pending.valid)
    {
      have_loaded = true;
    }

  if (!have_loaded)
    {
      if (had_pending || confirmation->visible ||
          confirmation->suppressed || confirmation->action_in_flight)
        {
          invalidate_locked(confirmation);
        }
      confirmation->alarm_blocked = alarm_active;
      pthread_mutex_unlock(&confirmation->lock);
      return ret == -ENOENT || ret == -ENOSYS || ret == -ESTALE ? 0 : ret;
    }

  capture_identity(&confirmation->pending, &new_identity);
  changed = !had_pending || !same_identity(&old_identity, &new_identity);
  apply_loaded_locked(confirmation, changed, alarm_active);
  pthread_mutex_unlock(&confirmation->lock);
  return 0;
}

bool vg_config_confirmation_is_visible(
  struct vg_config_confirmation *confirmation)
{
  bool visible;

  if (confirmation == NULL || !confirmation->initialized)
    {
      return false;
    }

  pthread_mutex_lock(&confirmation->lock);
  visible = confirmation->visible;
  pthread_mutex_unlock(&confirmation->lock);
  return visible;
}

int vg_config_confirmation_snapshot(
  struct vg_config_confirmation *confirmation,
  struct vg_config_pending_preview *out)
{
  if (confirmation == NULL || out == NULL ||
      !confirmation->initialized)
    {
      return -EINVAL;
    }

  pthread_mutex_lock(&confirmation->lock);
  if (!confirmation->have_pending)
    {
      pthread_mutex_unlock(&confirmation->lock);
      return -ENOENT;
    }
  *out = confirmation->pending;
  pthread_mutex_unlock(&confirmation->lock);
  return 0;
}

const struct vg_config_pending_preview *vg_config_confirmation_peek(
  struct vg_config_confirmation *confirmation)
{
  const struct vg_config_pending_preview *pending = NULL;

  if (confirmation == NULL || !confirmation->initialized)
    {
      return NULL;
    }

  pthread_mutex_lock(&confirmation->lock);
  if (confirmation->have_pending)
    {
      pending = &confirmation->pending;
    }
  pthread_mutex_unlock(&confirmation->lock);
  return pending;
}

static void fill_action(
  const struct vg_config_pending_preview *pending,
  enum vg_config_confirmation_action_kind_e kind,
  uint32_t generation,
  struct vg_config_confirmation_action *action)
{
  memset(action, 0, sizeof(*action));
  action->kind = kind;
  action->generation = generation;

  if (kind == VG_CONFIG_CONFIRMATION_CONFIRM)
    {
      memcpy(action->request.confirm.task_id, pending->task_id,
             sizeof(action->request.confirm.task_id));
      memcpy(action->request.confirm.session_id, pending->session_id,
             sizeof(action->request.confirm.session_id));
      action->request.confirm.revision = pending->revision;
      action->request.confirm.has_base_hash = pending->has_base_hash;
      memcpy(action->request.confirm.base_hash, pending->base_hash,
             sizeof(action->request.confirm.base_hash));
      memcpy(action->request.confirm.payload_hash, pending->payload_hash,
             sizeof(action->request.confirm.payload_hash));
      memcpy(action->request.confirm.candidate_hash, pending->candidate_hash,
             sizeof(action->request.confirm.candidate_hash));
      memcpy(action->request.confirm.confirmation_id,
             pending->confirmation_id,
             sizeof(action->request.confirm.confirmation_id));
    }
  else
    {
      memcpy(action->request.cancel.task_id, pending->task_id,
             sizeof(action->request.cancel.task_id));
      memcpy(action->request.cancel.session_id, pending->session_id,
             sizeof(action->request.cancel.session_id));
      action->request.cancel.revision = pending->revision;
      memcpy(action->request.cancel.confirmation_id,
             pending->confirmation_id,
             sizeof(action->request.cancel.confirmation_id));
    }
}

int vg_config_confirmation_submit(
  struct vg_config_confirmation *confirmation,
  enum vg_config_confirmation_action_kind_e kind,
  bool alarm_active)
{
  struct vg_config_confirmation_ports ports;
  struct vg_config_confirmation_action action;
  int ret;

  if (confirmation == NULL || !confirmation->initialized ||
      (kind != VG_CONFIG_CONFIRMATION_CONFIRM &&
       kind != VG_CONFIG_CONFIRMATION_CANCEL))
    {
      return -EINVAL;
    }

  ret = vg_config_confirmation_sync(confirmation, alarm_active);
  if (ret != 0)
    {
      return ret;
    }

  pthread_mutex_lock(&confirmation->lock);
  if (!confirmation->visible || confirmation->alarm_blocked ||
      confirmation->suppressed || confirmation->action_in_flight)
    {
      pthread_mutex_unlock(&confirmation->lock);
      return -EALREADY;
    }

  fill_action(&confirmation->pending, kind, confirmation->generation,
              &action);
  confirmation->visible = false;
  confirmation->suppressed = true;
  confirmation->action_in_flight = true;
  confirmation->action_claimed = false;
  ports = confirmation->ports;
  pthread_mutex_unlock(&confirmation->lock);

  ret = ports.submit(ports.ctx, &action);
  if (ret != 0)
    {
      pthread_mutex_lock(&confirmation->lock);
      confirmation->suppressed = false;
      confirmation->action_in_flight = false;
      confirmation->visible = confirmation->have_pending &&
                              !confirmation->alarm_blocked;
      pthread_mutex_unlock(&confirmation->lock);
    }

  memset(&action, 0, sizeof(action));
  return ret;
}

static bool same_action(
  const struct vg_config_confirmation_action *a,
  const struct vg_config_pending_preview *pending)
{
  if (a->kind == VG_CONFIG_CONFIRMATION_CONFIRM)
    {
      return strcmp(a->request.confirm.task_id, pending->task_id) == 0 &&
             strcmp(a->request.confirm.session_id, pending->session_id) == 0 &&
             a->request.confirm.revision == pending->revision &&
             same_hash(a->request.confirm.has_base_hash,
                       a->request.confirm.base_hash,
                       pending->has_base_hash, pending->base_hash) &&
             strcmp(a->request.confirm.payload_hash,
                    pending->payload_hash) == 0 &&
             strcmp(a->request.confirm.candidate_hash,
                    pending->candidate_hash) == 0 &&
             strcmp(a->request.confirm.confirmation_id,
                    pending->confirmation_id) == 0;
    }

  return strcmp(a->request.cancel.task_id, pending->task_id) == 0 &&
         strcmp(a->request.cancel.session_id, pending->session_id) == 0 &&
         a->request.cancel.revision == pending->revision &&
         strcmp(a->request.cancel.confirmation_id,
                pending->confirmation_id) == 0;
}

bool vg_config_confirmation_claim(
  struct vg_config_confirmation *confirmation,
  const struct vg_config_confirmation_action *action)
{
  bool claimed = false;

  if (confirmation == NULL || action == NULL ||
      !confirmation->initialized)
    {
      return false;
    }

  pthread_mutex_lock(&confirmation->lock);
  if (confirmation->action_in_flight &&
      !confirmation->action_claimed &&
      !confirmation->alarm_blocked &&
      action->generation == confirmation->generation &&
      confirmation->have_pending &&
      same_action(action, &confirmation->pending))
    {
      confirmation->action_claimed = true;
      claimed = true;
    }
  pthread_mutex_unlock(&confirmation->lock);
  return claimed;
}
