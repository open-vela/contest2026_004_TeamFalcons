/****************************************************************************
 * app/velaguard/vg_config_confirmation_worker.c
 ****************************************************************************/

#include <errno.h>
#include <string.h>

#include "vg_config_confirmation_worker.h"

#define VG_CONFIG_CONFIRMATION_WORKER_STACK 16384

static int worker_load_pending(
  void *ctx,
  struct vg_config_pending_preview *out)
{
  struct vg_config_confirmation_worker *worker = ctx;

  if (worker == NULL || !worker->initialized || worker->service == NULL)
    {
      return -ENOENT;
    }

  return vg_config_service_pending_preview(worker->service, out);
}

static int worker_submit(
  void *ctx,
  const struct vg_config_confirmation_action *action)
{
  struct vg_config_confirmation_worker *worker = ctx;
  int ret = 0;

  if (worker == NULL || action == NULL || !worker->initialized)
    {
      return -EINVAL;
    }

  pthread_mutex_lock(&worker->lock);
  if (worker->stopping || !worker->started)
    {
      ret = -EPIPE;
    }
  else if (worker->queued)
    {
      ret = -EBUSY;
    }
  else
    {
      worker->action = *action;
      worker->queued = true;
      pthread_cond_signal(&worker->changed);
    }

  pthread_mutex_unlock(&worker->lock);
  return ret;
}

static void *worker_main(void *ctx)
{
  struct vg_config_confirmation_worker *worker = ctx;

  for (;;)
    {
      struct vg_config_confirmation_action action;
      struct vg_config_task_status status;
      bool alarm_active;
      bool stop;

      pthread_mutex_lock(&worker->lock);
      while (!worker->queued && !worker->stopping)
        {
          pthread_cond_wait(&worker->changed, &worker->lock);
        }

      stop = worker->stopping && !worker->queued;
      if (stop)
        {
          pthread_mutex_unlock(&worker->lock);
          break;
        }

      action = worker->action;
      memset(&worker->action, 0, sizeof(worker->action));
      worker->queued = false;
      pthread_cond_broadcast(&worker->changed);
      pthread_mutex_unlock(&worker->lock);

      alarm_active = worker->alarm_active != NULL &&
                     worker->alarm_active(worker->alarm_ctx);
      if (alarm_active)
        {
          (void)vg_config_confirmation_sync(worker->confirmation, true);
          continue;
        }

      if (!vg_config_confirmation_claim(worker->confirmation, &action))
        {
          continue;
        }

      memset(&status, 0, sizeof(status));
      if (action.kind == VG_CONFIG_CONFIRMATION_CONFIRM)
        {
          (void)vg_config_service_confirm(worker->service,
                                          &action.request.confirm,
                                          &status);
        }
      else
        {
          (void)vg_config_service_cancel_local(worker->service,
                                               &action.request.cancel,
                                               &status);
        }

      memset(&action, 0, sizeof(action));
      memset(&status, 0, sizeof(status));
    }

  return NULL;
}

int vg_config_confirmation_worker_init(
  struct vg_config_confirmation_worker *worker,
  struct vg_config_confirmation *confirmation,
  struct vg_config_service *service,
  bool (*alarm_active)(void *ctx),
  void *alarm_ctx)
{
  int ret;

  if (worker == NULL || confirmation == NULL || service == NULL)
    {
      return -EINVAL;
    }

  memset(worker, 0, sizeof(*worker));
  ret = pthread_mutex_init(&worker->lock, NULL);
  if (ret != 0)
    {
      return -ret;
    }

  ret = pthread_cond_init(&worker->changed, NULL);
  if (ret != 0)
    {
      pthread_mutex_destroy(&worker->lock);
      memset(worker, 0, sizeof(*worker));
      return -ret;
    }

  worker->confirmation = confirmation;
  worker->service = service;
  worker->alarm_active = alarm_active;
  worker->alarm_ctx = alarm_ctx;
  worker->initialized = true;
  return 0;
}

int vg_config_confirmation_worker_ports(
  struct vg_config_confirmation_worker *worker,
  struct vg_config_confirmation_ports *ports)
{
  if (worker == NULL || ports == NULL || !worker->initialized)
    {
      return -EINVAL;
    }

  memset(ports, 0, sizeof(*ports));
  ports->load_pending = worker_load_pending;
  ports->submit = worker_submit;
  ports->ctx = worker;
  return 0;
}

int vg_config_confirmation_worker_start(
  struct vg_config_confirmation_worker *worker)
{
  pthread_attr_t attr;
  int ret;

  if (worker == NULL || !worker->initialized)
    {
      return -EINVAL;
    }

  pthread_mutex_lock(&worker->lock);
  if (worker->started)
    {
      pthread_mutex_unlock(&worker->lock);
      return -EALREADY;
    }
  worker->stopping = false;
  worker->started = true;
  pthread_mutex_unlock(&worker->lock);

  ret = pthread_attr_init(&attr);
  if (ret == 0)
    {
      (void)pthread_attr_setstacksize(
        &attr, VG_CONFIG_CONFIRMATION_WORKER_STACK);
      ret = pthread_create(&worker->thread, &attr, worker_main, worker);
      pthread_attr_destroy(&attr);
    }

  if (ret != 0)
    {
      pthread_mutex_lock(&worker->lock);
      worker->started = false;
      pthread_mutex_unlock(&worker->lock);
      return -ret;
    }

  return 0;
}

int vg_config_confirmation_worker_stop(
  struct vg_config_confirmation_worker *worker)
{
  bool started;

  if (worker == NULL || !worker->initialized)
    {
      return -EINVAL;
    }

  pthread_mutex_lock(&worker->lock);
  started = worker->started;
  worker->stopping = true;
  pthread_cond_broadcast(&worker->changed);
  pthread_mutex_unlock(&worker->lock);

  if (started && pthread_join(worker->thread, NULL) != 0)
    {
      return -EIO;
    }

  pthread_mutex_lock(&worker->lock);
  worker->started = false;
  pthread_mutex_unlock(&worker->lock);
  return 0;
}
