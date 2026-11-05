#ifndef VG_CONFIG_CONFIRMATION_UI_H
#define VG_CONFIG_CONFIRMATION_UI_H

#include <stdbool.h>

#include "vg_config_confirmation.h"

void vg_config_confirmation_ui_init(void);
int vg_config_confirmation_ui_attach_runtime(void);
int vg_config_confirmation_ui_bind(
    const struct vg_config_confirmation_ports *ports);
void vg_config_confirmation_ui_poll(void);
bool vg_config_confirmation_ui_is_open(void);

/* Back/Escape on this modal queues a local cancel for the exact displayed
 * candidate and dismisses the surface. */
void vg_config_confirmation_ui_back(void);

#endif
