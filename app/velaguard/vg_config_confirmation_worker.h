/****************************************************************************
 * app/velaguard/vg_config_confirmation_worker.h
 *
 * Bounded local worker for actions submitted by the confirmation surface.
 ****************************************************************************/

#ifndef __APP_VELAGUARD_VG_CONFIG_CONFIRMATION_WORKER_H
#define __APP_VELAGUARD_VG_CONFIG_CONFIRMATION_WORKER_H

#include <pthread.h>
#include <stdbool.h>

#include "vg_config_confirmation.h"

struct vg_config_confirmation_worker
{
  pthread_mutex_t lock;
  pthread_cond_t changed;
  pthread_t thread;
  struct vg_config_service *service;
  struct vg_config_confirmation *confirmation;
  bool (*alarm_active)(void *ctx);
  void *alarm_ctx;
  struct vg_config_confirmation_action action;
  bool initialized;
  bool started;
  bool stopping;
  bool queued;
};

int vg_config_confirmation_worker_init(
  struct vg_config_confirmation_worker *worker,
  struct vg_config_confirmation *confirmation,
  struct vg_config_service *service,
  bool (*alarm_active)(void *ctx),
  void *alarm_ctx);
int vg_config_confirmation_worker_ports(
  struct vg_config_confirmation_worker *worker,
  struct vg_config_confirmation_ports *ports);
int vg_config_confirmation_worker_start(
  struct vg_config_confirmation_worker *worker);
int vg_config_confirmation_worker_stop(
  struct vg_config_confirmation_worker *worker);

#endif
