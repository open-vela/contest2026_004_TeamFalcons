#ifndef VG_FONTS_H
#define VG_FONTS_H

#include "lvgl/lvgl.h"

/* Generated subset: ASCII + GB2312 L1 CJK + UI extras (cjk_symbols.txt).
 * Regen: bash gui/main/ui/fonts/regen_cjk_font.sh
 */
extern const lv_font_t vg_font_ui_14;

static inline const lv_font_t * vg_fonts_cjk(void)
{
    return &vg_font_ui_14;
}

#endif
