#include "vg_pages.h"
#include "widgets/vg_widgets.h"
#include "theme/vg_theme.h"
#include "model/vg_model.h"
#include "vg_display.h"
#include <stdio.h>
#include <string.h>

#define VG_SYS_FIELD_N 8
/* 8 字段 2 列 × 4 行 + OTA 行：4*35 + 3*4 + 4 + 36 = 192 < 232。
 * OTA 状态合并进 OTA 升级行（不再单设网格瓦片）。tile 为纯展示，35px 足够。 */
#define VG_SYS_TILE_H 35

typedef struct {
    lv_obj_t * root;
    lv_obj_t * tile_val[VG_SYS_FIELD_N];
    lv_obj_t * ota_row;
    lv_obj_t * ota_lab;
} sys_ctx_t;

static sys_ctx_t s_sys_ui;

static const char * const s_field_names[VG_SYS_FIELD_N] = {
    "IP", "Bridge/MiMo", "延迟", "RS485", "FS",
    "音频", "版本", "日志容量"
};

static void on_ota_row(lv_event_t * e)
{
    LV_UNUSED(e);
    vg_nav_goto(VG_PAGE_OTA, NULL);
}

static void set_tile(lv_obj_t * val_lab, const char * text, lv_color_t color)
{
    lv_label_set_text(val_lab, text ? text : "--");
    lv_obj_set_style_text_color(val_lab, color, 0);
}

static void refresh_system(void * user)
{
    const vg_sys_status_t * s;
    char buf[32];
    bool offline;
    LV_UNUSED(user);
    if(vg_nav_current() != VG_PAGE_SYSTEM) return;
    if(s_sys_ui.root == NULL || !lv_obj_is_valid(s_sys_ui.root)) return;

    s = vg_model_get_sys_status();
    if(s == NULL) return;
    offline = (vg_model_get_scenario() == VG_SCENARIO_OFFLINE);

    set_tile(s_sys_ui.tile_val[0], s->ip, vg_color_text());
    set_tile(s_sys_ui.tile_val[1], s->mimo_ok ? "正常" : "不可用",
             s->mimo_ok ? vg_color_ok() : vg_color_crit());
    lv_snprintf(buf, sizeof(buf), "%d ms", (int)s->latency_ms);
    set_tile(s_sys_ui.tile_val[2], buf,
             s->latency_ms >= 999 ? vg_color_crit() : vg_color_ok());
    set_tile(s_sys_ui.tile_val[3], s->rs485_ok ? "正常" : "故障",
             s->rs485_ok ? vg_color_ok() : vg_color_crit());
    lv_snprintf(buf, sizeof(buf), "%d%%", (int)s->fs_free_pct);
    set_tile(s_sys_ui.tile_val[4], buf,
             offline ? vg_color_warn() : vg_color_ok());
    set_tile(s_sys_ui.tile_val[5], s->audio_ok ? "正常" : "故障",
             s->audio_ok ? vg_color_ok() : vg_color_crit());
    lv_snprintf(buf, sizeof(buf), "%s · %s", s->version, s->slot);
    set_tile(s_sys_ui.tile_val[6], buf, vg_color_text());
    lv_snprintf(buf, sizeof(buf), "%d/%d", (int)s->log_used, (int)s->log_cap);
    set_tile(s_sys_ui.tile_val[7], buf, vg_color_text());

    if(s_sys_ui.ota_lab) {
        lv_snprintf(buf, sizeof(buf), "%s · %s", s->version, s->ota_label);
        lv_label_set_text(s_sys_ui.ota_lab, buf);
        lv_obj_set_style_text_color(s_sys_ui.ota_lab,
                                    (strcmp(s->ota_label, "无更新") == 0)
                                        ? vg_color_muted() : vg_color_info(), 0);
    }
}

static void on_sys_delete(lv_event_t * e)
{
    LV_UNUSED(e);
    vg_model_off_change(refresh_system, NULL);
    memset(&s_sys_ui, 0, sizeof(s_sys_ui));
}

static lv_obj_t * make_tile(lv_obj_t * parent, int idx)
{
    lv_obj_t * tile = lv_obj_create(parent);
    lv_obj_t * lab;

    lv_obj_remove_flag(tile, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_width(tile, lv_pct(49));
    lv_obj_set_height(tile, VG_SYS_TILE_H);
    lv_obj_set_style_bg_color(tile, vg_color_surface(), 0);
    lv_obj_set_style_bg_opa(tile, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(tile, vg_color_border(), 0);
    lv_obj_set_style_border_width(tile, 1, 0);
    lv_obj_set_style_radius(tile, VG_CARD_RADIUS, 0);
    lv_obj_set_style_pad_hor(tile, 8, 0);
    lv_obj_set_style_pad_ver(tile, 0, 0);
    lv_obj_set_flex_flow(tile, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(tile, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);

    lab = lv_label_create(tile);
    lv_label_set_text(lab, s_field_names[idx]);
    vg_style_apply_label(lab, true);
    lv_obj_set_style_text_font(lab, vg_font_small(), 0);

    s_sys_ui.tile_val[idx] = lv_label_create(tile);
    lv_label_set_text(s_sys_ui.tile_val[idx], "--");
    lv_obj_set_style_text_font(s_sys_ui.tile_val[idx], vg_font_ui(), 0);

    return tile;
}

void vg_page_system_create(lv_obj_t * parent, const void * args)
{
    lv_obj_t * grid;
    lv_obj_t * btn;
    lv_obj_t * lab;
    int i;
    LV_UNUSED(args);
    memset(&s_sys_ui, 0, sizeof(s_sys_ui));
    s_sys_ui.root = parent;
    lv_obj_add_event_cb(parent, on_sys_delete, LV_EVENT_DELETE, NULL);

    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(parent, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(parent, VG_GAP, 0);
    lv_obj_remove_flag(parent, LV_OBJ_FLAG_SCROLLABLE);

    /* 8 fields, 2 columns x 4 rows */
    grid = lv_obj_create(parent);
    lv_obj_remove_flag(grid, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(grid, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(grid, 0, 0);
    lv_obj_set_style_pad_all(grid, 0, 0);
    lv_obj_set_width(grid, lv_pct(100));
    lv_obj_set_height(grid, VG_SYS_TILE_H * 4 + VG_GAP * 3);
    lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(grid, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(grid, VG_GAP, 0);
    lv_obj_set_style_pad_column(grid, VG_GAP, 0);

    for(i = 0; i < VG_SYS_FIELD_N; i++) make_tile(grid, i);

    /* OTA entry row */
    s_sys_ui.ota_row = lv_obj_create(parent);
    lv_obj_set_style_flex_grow(s_sys_ui.ota_row, 0, 0);
    vg_style_apply_card(s_sys_ui.ota_row);
    lv_obj_set_width(s_sys_ui.ota_row, lv_pct(100));
    lv_obj_set_height(s_sys_ui.ota_row, VG_MIN_TOUCH_H);
    lv_obj_set_style_pad_hor(s_sys_ui.ota_row, 8, 0);
    lv_obj_set_style_pad_ver(s_sys_ui.ota_row, 0, 0);
    lv_obj_set_flex_flow(s_sys_ui.ota_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(s_sys_ui.ota_row, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lab = lv_label_create(s_sys_ui.ota_row);
    lv_label_set_text(lab, "OTA 升级");
    vg_style_apply_label(lab, false);

    s_sys_ui.ota_lab = lv_label_create(s_sys_ui.ota_row);
    lv_label_set_text(s_sys_ui.ota_lab, "");
    vg_style_apply_label(s_sys_ui.ota_lab, true);
    lv_obj_set_style_text_font(s_sys_ui.ota_lab, vg_font_small(), 0);
    lv_obj_set_flex_grow(s_sys_ui.ota_lab, 1);
    lv_label_set_long_mode(s_sys_ui.ota_lab, LV_LABEL_LONG_DOT);

    btn = lv_button_create(s_sys_ui.ota_row);
    lv_obj_set_size(btn, 96, VG_MIN_TOUCH_H);
    vg_style_apply_btn(btn, true);
    lab = lv_label_create(btn);
    lv_label_set_text(lab, "进入 OTA");
    lv_obj_center(lab);
    lv_obj_add_event_cb(btn, on_ota_row, LV_EVENT_CLICKED, NULL);

    vg_model_on_change(refresh_system, NULL);
    refresh_system(NULL);
}
