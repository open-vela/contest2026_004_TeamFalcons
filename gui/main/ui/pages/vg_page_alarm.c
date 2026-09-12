#include "vg_pages.h"
#include "widgets/vg_widgets.h"
#include "theme/vg_theme.h"
#include "model/vg_model.h"
#include "vg_display.h"
#include <stdio.h>
#include <string.h>

typedef struct {
    lv_obj_t * root;
    lv_obj_t * title;
    lv_obj_t * chip;
    lv_obj_t * rows[6];
    lv_obj_t * hist_lab;
    lv_obj_t * ai_head;
    lv_obj_t * ai_lab;
    lv_obj_t * btn_mute;
    lv_obj_t * btn_ack;
} alarm_ctx_t;

static alarm_ctx_t s_alarm_ui;

static void fmt_f1(char * buf, size_t n, float v)
{
    int vi = (int)v;
    int vf = (int)((v - (float)vi) * 10.0f);
    if(vf < 0) vf = -vf;
    lv_snprintf(buf, n, "%d.%d", vi, vf);
}

static void set_ai_text(const char * text)
{
    if(s_alarm_ui.ai_lab == NULL) {
        return;
    }
    lv_label_set_text(s_alarm_ui.ai_lab, text);
    if(s_alarm_ui.ai_head) {
        lv_obj_clear_flag(s_alarm_ui.ai_head, LV_OBJ_FLAG_HIDDEN);
    }
    lv_obj_clear_flag(s_alarm_ui.ai_lab, LV_OBJ_FLAG_HIDDEN);
}

static void refresh_alarm(void * user)
{
    const vg_alarm_t * a;
    const vg_sensor_t * s = NULL;
    const vg_sensor_t * as;
    char buf[160];
    char a1[16];
    int i;
    int start;
    int npts;
    uint16_t sensor_n = 0;
    LV_UNUSED(user);
    if(vg_nav_current() != VG_PAGE_ALARM) return;
    if(s_alarm_ui.root == NULL || !lv_obj_is_valid(s_alarm_ui.root)) return;

    a = vg_model_get_active_alarm();
    vg_model_get_sensors(&sensor_n);
    if(sensor_n > 0) {
        s = vg_model_get_primary_sensor();
    }

    if(a == NULL) {
        lv_label_set_text(s_alarm_ui.title, "无活动告警");
        vg_status_chip_set(s_alarm_ui.chip, "正常", VG_SEV_OK);
        set_ai_text("【规则摘要】暂无告警上下文。");
        return;
    }

    as = NULL;
    if(a->sensor_id[0] != '\0') {
        as = vg_model_get_sensor(a->sensor_id);
    }
    if(as == NULL) {
        as = s;
    }

    if(a->active) {
        lv_label_set_text(s_alarm_ui.title, a->title);
        vg_status_chip_set(s_alarm_ui.chip, vg_severity_label_zh(a->severity), a->severity);

        vg_metric_row_set_value(s_alarm_ui.rows[0],
                                a->severity == VG_SEV_OFFLINE ? "通信离线" : "阈值越限");

        if(a->severity == VG_SEV_OFFLINE || as == NULL) {
            vg_metric_row_set_value(s_alarm_ui.rows[1], "--");
            vg_metric_row_set_value(s_alarm_ui.rows[2], "--");
        }
        else {
            fmt_f1(a1, sizeof(a1), a->value);
            lv_snprintf(buf, sizeof(buf), "%s %s", a1, as->unit);
            vg_metric_row_set_value(s_alarm_ui.rows[1], buf);

            fmt_f1(a1, sizeof(a1), a->threshold);
            lv_snprintf(buf, sizeof(buf), "%s %s", a1, as->unit);
            vg_metric_row_set_value(s_alarm_ui.rows[2], buf);
        }

        lv_snprintf(buf, sizeof(buf), "%d s", a->duration_sec);
        vg_metric_row_set_value(s_alarm_ui.rows[3], buf);
        vg_metric_row_set_value(s_alarm_ui.rows[4], as ? as->name : "--");
        vg_metric_row_set_value(s_alarm_ui.rows[5],
                                a->acked ? "已处理" : (a->muted ? "已静音" : "活动中"));

        npts = 0;
        if(as != NULL) {
            npts = 8;
            if(as->history_len < npts) npts = as->history_len;
            start = (int)as->history_len - npts;
            if(start < 0) start = 0;
            buf[0] = '\0';
            for(i = 0; i < npts; i++) {
                char p[12];
                fmt_f1(p, sizeof(p), as->history[start + i]);
                if(i == 0) {
                    lv_snprintf(buf, sizeof(buf), "%s", p);
                }
                else {
                    size_t len = strlen(buf);
                    lv_snprintf(buf + len, sizeof(buf) - len, " %s", p);
                }
            }
        }
        if(npts == 0) {
            lv_label_set_text(s_alarm_ui.hist_lab, "历史: --");
        }
        else {
            char full[128];
            lv_snprintf(full, sizeof(full), "历史: %s", buf);
            lv_label_set_text(s_alarm_ui.hist_lab, full);
        }

        if(as != NULL && a->severity != VG_SEV_OFFLINE) {
            lv_snprintf(buf, sizeof(buf),
                        "【规则摘要】%s 当前 %.1f%s，阈值 %.1f%s，已持续 %d 秒。"
                        "判定来自点表 cmp/阈值，本地规则引擎。",
                        as->name, (double)a->value, as->unit,
                        (double)a->threshold, as->unit, a->duration_sec);
        }
        else if(a->severity == VG_SEV_OFFLINE) {
            lv_snprintf(buf, sizeof(buf),
                        "【规则摘要】连续读失败达 fail_n，离线已持续 %d 秒。",
                        a->duration_sec);
        }
        else {
            lv_snprintf(buf, sizeof(buf),
                        "【规则摘要】有活动告警，但本地尚无该点读数。");
        }
        set_ai_text(buf);

        if(s_alarm_ui.btn_ack) {
            if(a->acked) {
                lv_obj_add_state(s_alarm_ui.btn_ack, LV_STATE_DISABLED);
            }
            else {
                lv_obj_remove_state(s_alarm_ui.btn_ack, LV_STATE_DISABLED);
            }
        }
        if(s_alarm_ui.btn_mute) {
            if(a->muted) {
                lv_obj_add_state(s_alarm_ui.btn_mute, LV_STATE_DISABLED);
            }
            else {
                lv_obj_remove_state(s_alarm_ui.btn_mute, LV_STATE_DISABLED);
            }
        }
    }
    else {
        lv_label_set_text(s_alarm_ui.title, "无活动告警");
        vg_status_chip_set(s_alarm_ui.chip, "正常", VG_SEV_OK);
        vg_metric_row_set_value(s_alarm_ui.rows[0], "--");
        vg_metric_row_set_value(s_alarm_ui.rows[1], "--");
        vg_metric_row_set_value(s_alarm_ui.rows[2], "--");
        vg_metric_row_set_value(s_alarm_ui.rows[3], "--");
        vg_metric_row_set_value(s_alarm_ui.rows[4], as ? as->name : "--");
        vg_metric_row_set_value(s_alarm_ui.rows[5], "无");
        lv_label_set_text(s_alarm_ui.hist_lab, "历史: --");
        set_ai_text("【规则摘要】当前无活动告警。");
        if(s_alarm_ui.btn_ack) lv_obj_add_state(s_alarm_ui.btn_ack, LV_STATE_DISABLED);
        if(s_alarm_ui.btn_mute) lv_obj_add_state(s_alarm_ui.btn_mute, LV_STATE_DISABLED);
    }
}

static void on_mute(lv_event_t * e)
{
    LV_UNUSED(e);
    vg_model_mute_alarm();
    vg_shell_toast("已静音");
}

static void on_ack(lv_event_t * e)
{
    LV_UNUSED(e);
    vg_model_ack_alarm();
    vg_shell_toast("已标记处理");
}

static void on_alarm_delete(lv_event_t * e)
{
    LV_UNUSED(e);
    vg_model_off_change(refresh_alarm, NULL);
    memset(&s_alarm_ui, 0, sizeof(s_alarm_ui));
}

static lv_obj_t * make_btn(lv_obj_t * parent, const char * text, bool primary, lv_event_cb_t cb)
{
    lv_obj_t * btn = lv_button_create(parent);
    lv_obj_set_size(btn, 140, VG_MIN_TOUCH_H);
    lv_obj_set_style_min_height(btn, VG_MIN_TOUCH_H, 0);
    lv_obj_set_style_max_height(btn, VG_MIN_TOUCH_H, 0);
    vg_style_apply_btn(btn, primary);
    lv_obj_t * lab = lv_label_create(btn);
    lv_label_set_text(lab, text);
    lv_obj_set_style_text_font(lab, vg_font_ui(), 0);
    lv_obj_center(lab);
    if(cb) lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);
    return btn;
}

void vg_page_alarm_create(lv_obj_t * parent, const void * args)
{
    lv_obj_t * head;
    lv_obj_t * left;
    lv_obj_t * body;
    lv_obj_t * actions;
    LV_UNUSED(args);
    memset(&s_alarm_ui, 0, sizeof(s_alarm_ui));
    s_alarm_ui.root = parent;
    lv_obj_add_event_cb(parent, on_alarm_delete, LV_EVENT_DELETE, NULL);

    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(parent, 2, 0);
    lv_obj_remove_flag(parent, LV_OBJ_FLAG_SCROLLABLE);

    head = lv_obj_create(parent);
    vg_style_apply_card(head);
    lv_obj_set_width(head, lv_pct(100));
    lv_obj_set_height(head, 36);
    lv_obj_set_style_min_height(head, 36, 0);
    lv_obj_set_style_max_height(head, 36, 0);
    lv_obj_set_style_pad_all(head, 4, 0);
    lv_obj_set_flex_flow(head, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(head, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    left = lv_obj_create(head);
    lv_obj_remove_flag(left, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(left, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(left, 0, 0);
    lv_obj_set_style_pad_all(left, 0, 0);
    lv_obj_set_size(left, LV_SIZE_CONTENT, LV_SIZE_CONTENT);

    s_alarm_ui.title = lv_label_create(left);
    vg_style_apply_label(s_alarm_ui.title, false);
    lv_label_set_text(s_alarm_ui.title, "告警");

    s_alarm_ui.chip = vg_status_chip_create(head, "正常", VG_SEV_OK);

    body = lv_obj_create(parent);
    vg_style_apply_card(body);
    lv_obj_set_width(body, lv_pct(100));
    lv_obj_set_flex_grow(body, 1);
    lv_obj_set_style_min_height(body, 0, 0);
    lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(body, 2, 0);
    lv_obj_set_style_pad_ver(body, 2, 0);
    lv_obj_set_style_pad_hor(body, 6, 0);
    lv_obj_add_flag(body, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(body, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(body, LV_SCROLLBAR_MODE_AUTO);

    /* AI block first: 480x272 otherwise hides it under metric rows. */
    s_alarm_ui.ai_head = lv_label_create(body);
    vg_style_apply_label(s_alarm_ui.ai_head, false);
    lv_obj_set_style_text_color(s_alarm_ui.ai_head, vg_color_info(), 0);
    lv_label_set_text(s_alarm_ui.ai_head, "规则摘要");

    s_alarm_ui.ai_lab = lv_label_create(body);
    lv_label_set_long_mode(s_alarm_ui.ai_lab, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_alarm_ui.ai_lab, lv_pct(100));
    vg_style_apply_label(s_alarm_ui.ai_lab, true);
    lv_obj_set_style_text_font(s_alarm_ui.ai_lab, vg_font_small(), 0);
    lv_label_set_text(s_alarm_ui.ai_lab,
                      "【规则摘要】当前无活动告警。");

    s_alarm_ui.rows[0] = vg_metric_row_create(body, "类型", "--");
    s_alarm_ui.rows[1] = vg_metric_row_create(body, "当前值", "--");
    s_alarm_ui.rows[2] = vg_metric_row_create(body, "阈值", "--");
    s_alarm_ui.rows[3] = vg_metric_row_create(body, "持续", "--");
    s_alarm_ui.rows[4] = vg_metric_row_create(body, "传感器", "--");
    s_alarm_ui.rows[5] = vg_metric_row_create(body, "状态", "--");

    s_alarm_ui.hist_lab = lv_label_create(body);
    lv_label_set_long_mode(s_alarm_ui.hist_lab, LV_LABEL_LONG_DOT);
    lv_obj_set_width(s_alarm_ui.hist_lab, lv_pct(100));
    vg_style_apply_label(s_alarm_ui.hist_lab, true);
    lv_obj_set_style_text_font(s_alarm_ui.hist_lab, vg_font_small(), 0);
    lv_label_set_text(s_alarm_ui.hist_lab, "历史: --");

    actions = lv_obj_create(parent);
    lv_obj_remove_flag(actions, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(actions, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(actions, 0, 0);
    lv_obj_set_style_pad_all(actions, 0, 0);
    lv_obj_set_width(actions, lv_pct(100));
    lv_obj_set_height(actions, VG_MIN_TOUCH_H);
    lv_obj_set_style_min_height(actions, VG_MIN_TOUCH_H, 0);
    lv_obj_set_style_max_height(actions, VG_MIN_TOUCH_H, 0);
    lv_obj_set_flex_flow(actions, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(actions, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(actions, 4, 0);

    s_alarm_ui.btn_mute = make_btn(actions, "静音", false, on_mute);
    s_alarm_ui.btn_ack = make_btn(actions, "标记处理", true, on_ack);

    vg_model_on_change(refresh_alarm, NULL);
    refresh_alarm(NULL);
}
