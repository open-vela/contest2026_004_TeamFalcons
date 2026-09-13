#ifndef VG_PAGES_H
#define VG_PAGES_H

#include "lvgl/lvgl.h"
#include "shell/vg_shell.h"

void vg_page_home_create(lv_obj_t * parent, const void * args);
void vg_page_device_create(lv_obj_t * parent, const void * args);
void vg_page_trend_create(lv_obj_t * parent, const void * args);
void vg_page_alarm_create(lv_obj_t * parent, const void * args);
void vg_page_diagnosis_create(lv_obj_t * parent, const void * args);
void vg_page_logs_create(lv_obj_t * parent, const void * args);
void vg_page_add_sensor_create(lv_obj_t * parent, const void * args);
void vg_page_system_create(lv_obj_t * parent, const void * args);
void vg_page_ota_create(lv_obj_t * parent, const void * args);
void vg_page_report_create(lv_obj_t * parent, const void * args);
void vg_page_discover_create(lv_obj_t * parent, const void * args);

/* Navigation capture/restore (touch-acceptance T2). Safe no-ops when the
 * page is not the live instance. */
void vg_page_home_nav_capture(vg_nav_state_t * st);
void vg_page_home_nav_restore(const vg_nav_state_t * st);
void vg_page_trend_nav_capture(vg_nav_state_t * st);
void vg_page_trend_nav_restore(const vg_nav_state_t * st);
void vg_page_alarm_nav_capture(vg_nav_state_t * st);
void vg_page_alarm_nav_restore(const vg_nav_state_t * st);
void vg_page_report_nav_capture(vg_nav_state_t * st);
void vg_page_report_nav_restore(const vg_nav_state_t * st);

#endif
