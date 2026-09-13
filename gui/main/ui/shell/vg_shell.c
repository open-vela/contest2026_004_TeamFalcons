#include "vg_shell.h"
#include "pages/vg_pages.h"
#include "widgets/vg_confirm_dialog.h"
#include "theme/vg_theme.h"
#include "vg_display.h"
#include "vg_hmi_perf.h"
#include <stdio.h>
#include <string.h>
#include <time.h>

#define VG_NAV_STACK_MAX 6

typedef void (*vg_page_create_fn)(lv_obj_t * parent, const void * args);

static lv_obj_t * s_root;
static lv_obj_t * s_status_bar;
static lv_obj_t * s_title;
static lv_obj_t * s_date_lab;
static lv_obj_t * s_chip_net;
static lv_obj_t * s_chip_wifi;
static lv_obj_t * s_chip_mimo;
static lv_obj_t * s_chip_acq;
static lv_obj_t * s_chip_aud;
static lv_obj_t * s_chip_alarm;
static lv_obj_t * s_time_lab;
static lv_obj_t * s_back_btn;
static lv_obj_t * s_content;
static lv_obj_t * s_toast;
static lv_timer_t * s_toast_tmr;
static lv_timer_t * s_clock_tmr;
static lv_timer_t * s_model_tmr;

static vg_nav_state_t s_stack[VG_NAV_STACK_MAX];
static int s_stack_n;
static vg_page_id_t s_current = VG_PAGE_HOME;
static lv_obj_t * s_page_root = NULL;
static uint32_t s_page_generation = 0;

static void toast_hide_cb(lv_timer_t * t)
{
    LV_UNUSED(t);
    if(s_toast) {
        lv_obj_add_flag(s_toast, LV_OBJ_FLAG_HIDDEN);
    }
    if(s_toast_tmr) {
        lv_timer_delete(s_toast_tmr);
        s_toast_tmr = NULL;
    }
}

static void clock_cb(lv_timer_t * t)
{
    static int s_prev_hour = -1;
    static int s_prev_min = -1;
    static int s_prev_day = -1;
    char buf[24];
    time_t now;
    struct tm * tm_info;
    LV_UNUSED(t);
    time(&now);
    tm_info = localtime(&now);
    if(tm_info) {
        if(tm_info->tm_hour != s_prev_hour || tm_info->tm_min != s_prev_min) {
            s_prev_hour = tm_info->tm_hour;
            s_prev_min = tm_info->tm_min;
            lv_snprintf(buf, sizeof(buf), "%02d:%02d", tm_info->tm_hour, tm_info->tm_min);
            if(s_time_lab) lv_label_set_text(s_time_lab, buf);
        }
        if(tm_info->tm_mday != s_prev_day) {
            s_prev_day = tm_info->tm_mday;
            /* Date rides on the per-page title: VelaGuard|26-9-13 on Home,
             * 设备详情|26-9-13 on subpages. */
            lv_snprintf(buf, sizeof(buf), "|%d-%d-%d",
                        (tm_info->tm_year + 1900) % 100,
                        tm_info->tm_mon + 1, tm_info->tm_mday);
            if(s_date_lab) lv_label_set_text(s_date_lab, buf);
        }
    }
}

/* 1s mock acquisition driver: values / ages / alarm duration advance here */
static void model_tick_cb(lv_timer_t * t)
{
    uint32_t t0 = vg_hmi_perf_now_us();

    LV_UNUSED(t);
    vg_model_tick();
    vg_hmi_perf_span(VG_PERF_MODEL_TICK, t0);
}

static void alarm_chip_clicked(lv_event_t * e)
{
    LV_UNUSED(e);
    vg_nav_goto(VG_PAGE_ALARM, NULL);
}

static void back_clicked(lv_event_t * e)
{
    LV_UNUSED(e);
    vg_nav_back();
}

static void model_changed(void * user)
{
    LV_UNUSED(user);
    vg_shell_refresh_status();
}

static lv_obj_t * make_chip(lv_obj_t * parent, const char * text, vg_severity_t sev)
{
    lv_obj_t * chip = lv_obj_create(parent);
    lv_obj_remove_flag(chip, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(chip, LV_SIZE_CONTENT, 18);
    vg_style_apply_chip(chip, sev);
    lv_obj_t * lab = lv_label_create(chip);
    lv_label_set_text(lab, text);
    lv_obj_center(lab);
    lv_obj_set_user_data(chip, lab);
    return chip;
}

static void set_chip(lv_obj_t * chip, const char * text, vg_severity_t sev)
{
    if(chip == NULL) return;
    vg_style_apply_chip(chip, sev);
    lv_obj_t * lab = (lv_obj_t *)lv_obj_get_user_data(chip);
    if(lab) lv_label_set_text(lab, text);
}

void vg_shell_create(void)
{
    lv_obj_t * scr = lv_screen_active();
    vg_style_apply_screen(scr);
    lv_obj_set_size(scr, VG_DISP_W, VG_DISP_H);

    s_root = lv_obj_create(scr);
    lv_obj_set_size(s_root, VG_DISP_W, VG_DISP_H);
    lv_obj_set_pos(s_root, 0, 0);
    vg_style_apply_screen(s_root);
    lv_obj_remove_flag(s_root, LV_OBJ_FLAG_SCROLLABLE);

    s_status_bar = lv_obj_create(s_root);
    lv_obj_set_size(s_status_bar, VG_DISP_W, VG_STATUS_H);
    lv_obj_set_pos(s_status_bar, 0, 0);
    lv_obj_set_style_bg_color(s_status_bar, vg_color_surface(), 0);
    lv_obj_set_style_bg_opa(s_status_bar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(s_status_bar, vg_color_border(), 0);
    lv_obj_set_style_border_width(s_status_bar, 0, 0);
    lv_obj_set_style_border_side(s_status_bar, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_width(s_status_bar, 1, 0);
    lv_obj_set_style_pad_hor(s_status_bar, 6, 0);
    lv_obj_set_style_pad_ver(s_status_bar, 2, 0);
    lv_obj_set_style_radius(s_status_bar, 0, 0);
    lv_obj_remove_flag(s_status_bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(s_status_bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(s_status_bar, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(s_status_bar, 4, 0);

    s_back_btn = lv_button_create(s_status_bar);
    /* Status-bar exception: 48x28 back hit target inside the 28 px bar. */
    lv_obj_set_size(s_back_btn, 48, 28);
    vg_style_apply_btn(s_back_btn, false);
    lv_obj_t * back_lab = lv_label_create(s_back_btn);
    lv_label_set_text(back_lab, "<");
    lv_obj_center(back_lab);
    lv_obj_add_event_cb(s_back_btn, back_clicked, LV_EVENT_CLICKED, NULL);
    lv_obj_add_flag(s_back_btn, LV_OBJ_FLAG_HIDDEN);

    s_title = lv_label_create(s_status_bar);
    lv_label_set_text(s_title, "VelaGuard");
    lv_obj_set_style_text_font(s_title, vg_font_ui(), 0);
    lv_obj_set_style_text_color(s_title, vg_color_text(), 0);

    s_date_lab = lv_label_create(s_status_bar);
    lv_label_set_text(s_date_lab, "");
    lv_obj_set_style_text_font(s_date_lab, vg_font_small(), 0);
    lv_obj_set_style_text_color(s_date_lab, vg_color_muted(), 0);

    lv_obj_t * spacer = lv_obj_create(s_status_bar);
    lv_obj_set_flex_grow(spacer, 1);
    lv_obj_set_style_bg_opa(spacer, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(spacer, 0, 0);
    lv_obj_set_height(spacer, 1);
    lv_obj_remove_flag(spacer, LV_OBJ_FLAG_SCROLLABLE);

    /* NET/WiFi use info token when healthy (design: link); WiFi is
     * muted while the hot-standby bearer is not joined. */
    s_chip_net = make_chip(s_status_bar, "NET", VG_SEV_INFO);
    s_chip_wifi = make_chip(s_status_bar, "WiFi", VG_SEV_INFO);
    s_chip_mimo = make_chip(s_status_bar, "MiMo", VG_SEV_INFO);
    s_chip_acq = make_chip(s_status_bar, "ACQ", VG_SEV_OK);
    s_chip_aud = make_chip(s_status_bar, "AUD", VG_SEV_OK);

    /* ALARM chip (manual 16.8): always shown; tap opens alarm/AI page.
     * Label becomes "告警!" while an alarm is active. */
    s_chip_alarm = make_chip(s_status_bar, "告警", VG_SEV_OK);
    /* Status-bar exception: widen alarm entry to >=44x28 without growing the bar. */
    lv_obj_set_size(s_chip_alarm, 44, 28);
    lv_obj_set_style_min_width(s_chip_alarm, 44, 0);
    lv_obj_set_style_min_height(s_chip_alarm, 28, 0);
    lv_obj_add_flag(s_chip_alarm, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_chip_alarm, alarm_chip_clicked, LV_EVENT_CLICKED, NULL);

    s_time_lab = lv_label_create(s_status_bar);
    lv_label_set_text(s_time_lab, "--:--");
    lv_obj_set_style_text_font(s_time_lab, vg_font_small(), 0);
    lv_obj_set_style_text_color(s_time_lab, vg_color_muted(), 0);

    s_content = lv_obj_create(s_root);
    lv_obj_set_size(s_content, VG_DISP_W, VG_CONTENT_H);
    lv_obj_set_pos(s_content, 0, VG_STATUS_H);
    vg_style_apply_screen(s_content);
    lv_obj_set_style_pad_all(s_content, VG_PAGE_PAD, 0);
    lv_obj_remove_flag(s_content, LV_OBJ_FLAG_SCROLLABLE);
    /* Keep page children clipped to the 244px content host */
    lv_obj_remove_flag(s_content, LV_OBJ_FLAG_OVERFLOW_VISIBLE);

    s_toast = lv_obj_create(s_root);
    lv_obj_set_size(s_toast, 280, 36);
    lv_obj_align(s_toast, LV_ALIGN_BOTTOM_MID, 0, -8);
    lv_obj_set_style_bg_color(s_toast, vg_color_surface(), 0);
    lv_obj_set_style_bg_opa(s_toast, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(s_toast, vg_color_accent(), 0);
    lv_obj_set_style_border_width(s_toast, 1, 0);
    lv_obj_set_style_radius(s_toast, VG_CARD_RADIUS, 0);
    lv_obj_set_style_pad_all(s_toast, 6, 0);
    lv_obj_remove_flag(s_toast, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t * toast_lab = lv_label_create(s_toast);
    lv_label_set_text(toast_lab, "");
    lv_obj_set_style_text_font(toast_lab, vg_font_ui(), 0);
    lv_obj_set_style_text_color(toast_lab, vg_color_text(), 0);
    lv_obj_center(toast_lab);
    lv_obj_set_user_data(s_toast, toast_lab);
    lv_obj_add_flag(s_toast, LV_OBJ_FLAG_HIDDEN);

    s_stack_n = 0;
    s_current = VG_PAGE_HOME;
    s_toast_tmr = NULL;
    s_clock_tmr = lv_timer_create(clock_cb, 1000, NULL);
    s_model_tmr = lv_timer_create(model_tick_cb, 1000, NULL);
    clock_cb(NULL);

    vg_model_on_change(model_changed, NULL);
    vg_shell_refresh_status();
}

lv_obj_t * vg_shell_get_content(void)
{
    return s_page_root ? s_page_root : s_content;
}

uint32_t vg_shell_page_generation(void)
{
    return s_page_generation;
}

void vg_shell_set_title(const char * title)
{
    if(s_title) {
        lv_label_set_text(s_title, title ? title : "VelaGuard");
    }
}

/* WiFi chip: styled like NET when ESP has IP; muted when not associated —
 * the hot standby being idle is not a fault. */
static void set_wifi_chip(bool ok)
{
    lv_obj_t * lab;

    if(s_chip_wifi == NULL) return;
    lab = (lv_obj_t *)lv_obj_get_user_data(s_chip_wifi);

    if(ok) {
        set_chip(s_chip_wifi, "WiFi", VG_SEV_INFO);
        if(lab) lv_obj_set_style_text_color(lab, vg_color_info(), 0);
        return;
    }

    set_chip(s_chip_wifi, "WiFi", VG_SEV_INFO);
    lv_obj_set_style_bg_opa(s_chip_wifi, LV_OPA_TRANSP, 0);
    if(lab) lv_obj_set_style_text_color(lab, vg_color_muted(), 0);
}

void vg_shell_set_status(const vg_net_status_t * net)
{
    if(net == NULL) return;
    set_chip(s_chip_net, net->net_ok ? "NET" : "NET!", net->net_ok ? VG_SEV_INFO : VG_SEV_CRIT);
    set_wifi_chip(net->wifi_ok);
    if(net->ota_active) {
        set_chip(s_chip_mimo, "OTA", VG_SEV_INFO);
    }
    else {
        set_chip(s_chip_mimo, net->mimo_ok ? "MiMo" : "MiMo!", net->mimo_ok ? VG_SEV_INFO : VG_SEV_WARN);
    }
    set_chip(s_chip_acq, net->acq_ok ? "ACQ" : "ACQ!", net->acq_ok ? VG_SEV_OK : VG_SEV_CRIT);
    set_chip(s_chip_aud, net->aud_ok ? "AUD" : "AUD!", net->aud_ok ? VG_SEV_OK : VG_SEV_WARN);
}

void vg_shell_show_back(bool show)
{
    if(s_back_btn == NULL) return;
    if(show) lv_obj_remove_flag(s_back_btn, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(s_back_btn, LV_OBJ_FLAG_HIDDEN);
}

void vg_shell_toast(const char * msg)
{
    if(s_toast == NULL) return;
    lv_obj_t * lab = (lv_obj_t *)lv_obj_get_user_data(s_toast);
    if(lab) lv_label_set_text(lab, msg ? msg : "");
    lv_obj_remove_flag(s_toast, LV_OBJ_FLAG_HIDDEN);
    if(s_toast_tmr) {
        lv_timer_delete(s_toast_tmr);
        s_toast_tmr = NULL;
    }
    s_toast_tmr = lv_timer_create(toast_hide_cb, 1800, NULL);
    lv_timer_set_repeat_count(s_toast_tmr, 1);
}

/* ALARM chip: always visible (opens AI/alarm page); "告警!" when active. */
static void refresh_alarm_chip(void)
{
    const vg_alarm_t * a;
    lv_obj_t * lab;

    if(s_chip_alarm == NULL) return;

    a = vg_model_get_active_alarm();
    lab = (lv_obj_t *)lv_obj_get_user_data(s_chip_alarm);
    lv_obj_remove_flag(s_chip_alarm, LV_OBJ_FLAG_HIDDEN);

    if(a == NULL || !a->active) {
        set_chip(s_chip_alarm, "告警", VG_SEV_OK);
        lv_obj_set_style_bg_opa(s_chip_alarm, LV_OPA_TRANSP, 0);
        if(lab) lv_obj_set_style_text_color(lab, vg_color_muted(), 0);
        return;
    }

    set_chip(s_chip_alarm, "告警!", a->severity);
    /* Dim only when every active alarm was acked/muted (multi-alarm rows
     * each carry their own flag; severity comes from the primary alarm) */
    if(vg_model_alarms_all_quieted()) {
        lv_obj_set_style_bg_opa(s_chip_alarm, LV_OPA_TRANSP, 0);
        if(lab) lv_obj_set_style_text_color(lab, vg_color_muted(), 0);
    }
    else {
        lv_obj_set_style_bg_opa(s_chip_alarm, LV_OPA_30, 0);
        if(lab) {
            lv_obj_set_style_text_color(lab, vg_color_severity(a->severity), 0);
        }
    }
}

void vg_shell_refresh_status(void)
{
    vg_shell_set_status(vg_model_get_net());
    refresh_alarm_chip();
}

void vg_shell_modal_close(void)
{
    vg_confirm_dialog_close();
}

static void nav_wait_pointer_release(void)
{
    lv_indev_t * indev = lv_indev_get_next(NULL);

    while(indev != NULL) {
        if(lv_indev_get_type(indev) == LV_INDEV_TYPE_POINTER) {
            lv_indev_wait_release(indev);
        }
        indev = lv_indev_get_next(indev);
    }
}

static void nav_capture_current(vg_nav_state_t * out)
{
    const char * sel;

    memset(out, 0, sizeof(*out));
    out->page = s_current;
    out->structure_version = vg_model_structure_version();
    out->home_filter = vg_model_get_home_filter();
    sel = vg_model_get_selected_sensor_id();
    if(sel != NULL && sel[0] != '\0') {
        strncpy(out->sensor_id, sel, sizeof(out->sensor_id) - 1);
    }

    switch(s_current) {
        case VG_PAGE_HOME:
            vg_page_home_nav_capture(out);
            break;
        case VG_PAGE_TREND:
            vg_page_trend_nav_capture(out);
            break;
        case VG_PAGE_ALARM:
            vg_page_alarm_nav_capture(out);
            break;
        case VG_PAGE_REPORT:
            vg_page_report_nav_capture(out);
            break;
        default:
            break;
    }
}

static void nav_apply_restore(vg_page_id_t id, const vg_nav_state_t * st)
{
    if(st == NULL) return;

    switch(id) {
        case VG_PAGE_HOME:
            vg_page_home_nav_restore(st);
            break;
        case VG_PAGE_TREND:
            vg_page_trend_nav_restore(st);
            break;
        case VG_PAGE_ALARM:
            vg_page_alarm_nav_restore(st);
            break;
        case VG_PAGE_REPORT:
            vg_page_report_nav_restore(st);
            break;
        default:
            break;
    }
}

static void nav_stack_push(const vg_nav_state_t * st)
{
    if(s_stack_n >= VG_NAV_STACK_MAX) {
        /* Drop oldest return record, keep depth bounded. */
        memmove(&s_stack[0], &s_stack[1],
                sizeof(s_stack[0]) * (VG_NAV_STACK_MAX - 1));
        s_stack_n = VG_NAV_STACK_MAX - 1;
    }
    s_stack[s_stack_n] = *st;
    s_stack_n++;
}

static void create_page(vg_page_id_t id, const vg_nav_state_t * restore)
{
    if(s_page_root != NULL) {
        lv_obj_delete(s_page_root);
        s_page_root = NULL;
    }
    s_page_generation++;

    /* Device restore with a deleted point returns home instead. */
    if(id == VG_PAGE_DEVICE && restore != NULL) {
        if(restore->sensor_id[0] != '\0') {
            vg_model_set_selected_sensor(restore->sensor_id);
        }
        if(vg_model_get_selected_sensor() == NULL) {
            id = VG_PAGE_HOME;
            s_current = VG_PAGE_HOME;
            restore = NULL;
        }
    }

    s_page_root = lv_obj_create(s_content);
    lv_obj_set_size(s_page_root, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_opa(s_page_root, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_page_root, 0, 0);
    lv_obj_set_style_pad_all(s_page_root, 0, 0);
    lv_obj_remove_flag(s_page_root, LV_OBJ_FLAG_SCROLLABLE);

    /* Apply model-level restore before page widgets bind to it. */
    if(restore != NULL) {
        if(id == VG_PAGE_HOME &&
           restore->home_filter != vg_model_get_home_filter()) {
            vg_model_set_home_filter(restore->home_filter);
        }
        if(restore->sensor_id[0] != '\0' &&
           (id == VG_PAGE_DEVICE || id == VG_PAGE_TREND)) {
            vg_model_set_selected_sensor(restore->sensor_id);
        }
    }

    switch(id) {
        case VG_PAGE_HOME:
            vg_shell_set_title("VelaGuard");
            vg_page_home_create(s_page_root, NULL);
            break;
        case VG_PAGE_DEVICE:
            vg_shell_set_title("设备详情");
            vg_page_device_create(s_page_root, NULL);
            break;
        case VG_PAGE_TREND:
            vg_shell_set_title("实时趋势");
            vg_page_trend_create(s_page_root, NULL);
            break;
        case VG_PAGE_ALARM:
            vg_shell_set_title("告警详情");
            vg_page_alarm_create(s_page_root, NULL);
            break;
        case VG_PAGE_DIAGNOSIS:
            vg_shell_set_title("AI 诊断");
            vg_page_diagnosis_create(s_page_root, NULL);
            break;
        case VG_PAGE_LOGS:
            vg_shell_set_title("事件日志");
            vg_page_logs_create(s_page_root, NULL);
            break;
        case VG_PAGE_ADD_SENSOR:
            vg_shell_set_title("添加传感器");
            vg_page_add_sensor_create(s_page_root, NULL);
            break;
        case VG_PAGE_SYSTEM:
            vg_shell_set_title("系统状态");
            vg_page_system_create(s_page_root, NULL);
            break;
        case VG_PAGE_OTA:
            vg_shell_set_title("OTA 升级");
            vg_page_ota_create(s_page_root, NULL);
            break;
        case VG_PAGE_REPORT:
            vg_shell_set_title("运行报告");
            vg_page_report_create(s_page_root, NULL);
            break;
        case VG_PAGE_DISCOVER:
            vg_shell_set_title("总线探查");
            vg_page_discover_create(s_page_root, NULL);
            break;
        default:
            break;
    }

    if(restore != NULL) {
        nav_apply_restore(id, restore);
    }
    vg_shell_show_back(s_stack_n > 0);
    nav_wait_pointer_release();
}

static bool nav_allowed(vg_page_id_t id, const char ** toast_out)
{
    switch(id) {
        case VG_PAGE_DIAGNOSIS:
        case VG_PAGE_LOGS:
        case VG_PAGE_SYSTEM:
        case VG_PAGE_ADD_SENSOR:
            if(toast_out) *toast_out = "阶段 2 提供";
            return false;
        case VG_PAGE_OTA:
            if(toast_out) *toast_out = "阶段 3 提供";
            return false;
        default:
            return true;
    }
}

void vg_nav_goto(vg_page_id_t id, const void * args)
{
    const char * toast;
    uint32_t nav_t0;
    vg_nav_state_t leaving;

    LV_UNUSED(args);

    if(id >= VG_PAGE_COUNT) return;
    if(!nav_allowed(id, &toast)) {
        vg_shell_toast(toast);
        return;
    }

    if(s_page_root != NULL && id == s_current) {
        return;
    }

    /* nav_dispatch: navigation request into page build return */
    nav_t0 = vg_hmi_perf_now_us();

    /* First paint (shell just created): do not push phantom back entry */
    if(s_page_root == NULL && id == VG_PAGE_HOME) {
        s_stack_n = 0;
        s_current = id;
        create_page(id, NULL);
        vg_hmi_perf_span(VG_PERF_NAV, nav_t0);
        return;
    }

    if(id != s_current) {
        nav_capture_current(&leaving);
        nav_stack_push(&leaving);
    }
    s_current = id;
    create_page(id, NULL);
    vg_hmi_perf_span(VG_PERF_NAV, nav_t0);
}

void vg_nav_back(void)
{
    uint32_t nav_t0;
    vg_nav_state_t target;

    /* Modal wins over navigation: back/Esc closes the confirm dialog first. */
    if(vg_confirm_dialog_is_open()) {
        vg_confirm_dialog_close();
        return;
    }
    nav_t0 = vg_hmi_perf_now_us();
    if(s_stack_n <= 0) {
        if(s_current != VG_PAGE_HOME) {
            s_current = VG_PAGE_HOME;
            create_page(VG_PAGE_HOME, NULL);
            vg_hmi_perf_span(VG_PERF_NAV, nav_t0);
        }
        return;
    }
    s_stack_n--;
    target = s_stack[s_stack_n];
    s_current = target.page;
    create_page(s_current, &target);
    vg_hmi_perf_span(VG_PERF_NAV, nav_t0);
}

vg_page_id_t vg_nav_current(void)
{
    return s_current;
}

uint32_t vg_shell_debug_timer_count(void)
{
    return (uint32_t)((s_clock_tmr ? 1 : 0) + (s_model_tmr ? 1 : 0) +
                      (s_toast_tmr ? 1 : 0));
}
