/****************************************************************************
 * gui/main/ui/widgets/vg_voice_overlay.h
 *
 * BVO-12: cross-page voice panel on lv_layer_top().
 *
 * The panel only reads vg_voice_round_ui() and posts two commands back, so it
 * can be created with the shell and outlive every page switch.
 ****************************************************************************/

#ifndef VG_VOICE_OVERLAY_H
#define VG_VOICE_OVERLAY_H

#include <stddef.h>

void vg_voice_overlay_create(void);

/* BVO-12 diagnostics: geometry, scroll range and hit tests into a buffer.
 * Returns bytes written.  Read-only; it never moves or styles an object. */
int vg_voice_overlay_debug(char *out, size_t cap);
void vg_voice_overlay_destroy(void);

#endif /* VG_VOICE_OVERLAY_H */
