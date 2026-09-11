#include "vg_pages.h"
#include "widgets/vg_widgets.h"
#include "theme/vg_theme.h"
#include "model/vg_model.h"
#include "model/vg_ui_backend.h"
#include "vg_display.h"
#include <string.h>

typedef struct {
    lv_obj_t * root;
    lv_obj_t * title_lab;
    lv_obj_t * body_lab;
} report_ctx_t;

static report_ctx_t s_report;

static void on_report_delete(lv_event_t * e)
{
    LV_UNUSED(e);
    memset(&s_report, 0, sizeof(s_report));
}

void vg_page_report_create(lv_obj_t * parent, const void * args)
{
    const vg_ui_backend_t * be = vg_ui_backend_get();
    char body[1024];
    char path[128];
    lv_obj_t * card;
    LV_UNUSED(args);

    body[0] = '\0';
    path[0] = '\0';
    if(be != NULL && be->read_latest_report != NULL) {
        (void)be->read_latest_report(body, sizeof(body), path, sizeof(path));
    }

    memset(&s_report, 0, sizeof(s_report));
    s_report.root = parent;
    lv_obj_add_event_cb(parent, on_report_delete, LV_EVENT_DELETE, NULL);

    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(parent, 6, 0);
    lv_obj_remove_flag(parent, LV_OBJ_FLAG_SCROLLABLE);

    s_report.title_lab = lv_label_create(parent);
    vg_style_apply_label(s_report.title_lab, false);
    if(path[0] != '\0') {
        lv_label_set_text_fmt(s_report.title_lab, "最新日报 · %s", path);
    }
    else {
        lv_label_set_text(s_report.title_lab, "运行报告");
    }

    card = lv_obj_create(parent);
    vg_style_apply_card(card);
    lv_obj_set_width(card, lv_pct(100));
    lv_obj_set_flex_grow(card, 1);
    lv_obj_set_style_min_height(card, 0, 0);
    lv_obj_add_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(card, LV_DIR_VER);

    s_report.body_lab = lv_label_create(card);
    lv_label_set_long_mode(s_report.body_lab, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_report.body_lab, lv_pct(100));
    vg_style_apply_label(s_report.body_lab, true);
    lv_obj_set_style_text_font(s_report.body_lab, vg_font_small(), 0);
    if(body[0] != '\0') {
        lv_label_set_text(s_report.body_lab, body);
    }
    else {
        lv_label_set_text(s_report.body_lab,
            "暂无日报。\n\n(板端: /data/velaguard/reports/daily-*.md)");
    }
}
