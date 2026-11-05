/****************************************************************************
 * app/velaguard/vg_config_confirmation.h
 *
 * Host-testable arbitration for the local candidate confirmation surface.
 ****************************************************************************/

#ifndef __APP_VELAGUARD_VG_CONFIG_CONFIRMATION_H
#define __APP_VELAGUARD_VG_CONFIG_CONFIRMATION_H

#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>

#include "vg_config_service.h"

enum vg_config_confirmation_action_kind_e
{
  VG_CONFIG_CONFIRMATION_CONFIRM = 0,
  VG_CONFIG_CONFIRMATION_CANCEL
};

struct vg_config_confirmation_action
{
  enum vg_config_confirmation_action_kind_e kind;
  uint32_t generation;

  union
  {
    struct vg_config_confirm_request confirm;
    struct vg_config_local_cancel_request cancel;
  } request;
};

struct vg_config_confirmation_ports
{
  /* Must copy one current AWAITING_CONFIRM snapshot into out. */
  int (*load_pending)(void *ctx, struct vg_config_pending_preview *out);

  /* Must only enqueue the action and leave commit work to the service worker. */
  int (*submit)(void *ctx,
                const struct vg_config_confirmation_action *action);
  void *ctx;
};

struct vg_config_confirmation
{
  pthread_mutex_t lock;
  struct vg_config_confirmation_ports ports;
  struct vg_config_pending_preview pending;
  bool initialized;
  bool have_pending;
  bool visible;
  bool alarm_blocked;
  bool suppressed;
  bool action_in_flight;
  bool action_claimed;
  uint32_t generation;
};

int vg_config_confirmation_init(
  struct vg_config_confirmation *confirmation,
  const struct vg_config_confirmation_ports *ports);
int vg_config_confirmation_bind(
  struct vg_config_confirmation *confirmation,
  const struct vg_config_confirmation_ports *ports);
void vg_config_confirmation_deinit(
  struct vg_config_confirmation *confirmation);

int vg_config_confirmation_sync(struct vg_config_confirmation *confirmation,
                                bool alarm_active);
bool vg_config_confirmation_is_visible(
  struct vg_config_confirmation *confirmation);
int vg_config_confirmation_snapshot(
  struct vg_config_confirmation *confirmation,
  struct vg_config_pending_preview *out);
const struct vg_config_pending_preview *vg_config_confirmation_peek(
  struct vg_config_confirmation *confirmation);

int vg_config_confirmation_submit(
  struct vg_config_confirmation *confirmation,
  enum vg_config_confirmation_action_kind_e kind,
  bool alarm_active);

/* The worker calls this immediately before touching the configuration service.
 * A matching action can be claimed once; alarm, expiry, a new candidate or a
 * replacement action invalidates every older action. */
bool vg_config_confirmation_claim(
  struct vg_config_confirmation *confirmation,
  const struct vg_config_confirmation_action *action);

#endif
