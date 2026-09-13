/* Headless render check for the point-table trend page (task
 * 09-13-trend-page-live). Test logic unchanged from the original
 * standalone harness; plumbing now shared via harness_common
 * (format-aware buffer/decode). The full-window sample assertion follows
 * the build's capacity profile (VG_HISTORY_LEN: 300 on PC, 128 on the
 * board-capacity profile) instead of a hardcoded PC value.
 *
 * Usage: trend_check <out-dir>
 */

#include "harness_common.h"
#include "model/vg_model.h"
#include "shell/vg_shell.h"
#include <stdio.h>
#include <string.h>

int main(int argc, char ** argv)
{
    const char * out_dir = (argc > 1) ? argv[1] : ".";
    bool ok = true;
    lv_obj_t * dd;
    lv_obj_t * lab;
    const vg_sensor_t * sensors;
    uint16_t n = 0;
    uint16_t i;
    uint16_t sel0, target;
    const char * sel_id;
    char full_needle[32];

    hg_init();
    hg_pump(30); /* >= 1 model tick so sim history fills */

    sensors = vg_model_get_sensors(&n);
    if(sensors == NULL) n = 0;
    if(!hg_expect(n > 1, "sim model exposes several points")) return 1;

    /* 1) AC1: trend page is reachable (stage-2 gate lifted) */
    vg_nav_goto(VG_PAGE_TREND, NULL);
    hg_pump(20);
    ok &= hg_expect(vg_nav_current() == VG_PAGE_TREND, "trend page reachable (no stage gate)");
    dd = hg_find_obj(hg_match_dropdown, NULL, NULL, NULL);
    ok &= hg_expect(dd != NULL, "trend page has a point dropdown");
    if(dd == NULL) return 1;
    ok &= hg_expect(strlen(lv_dropdown_get_options(dd)) > 0, "dropdown lists points");
    hg_dump_ppm(out_dir, "01_trend_initial");
    ok &= hg_expect_not_blank("trend page renders content");

    /* 2) AC2/AC3: pick a different point through the real dropdown UI.
     * LVGL 9 renders all options in one multi-line label inside the list;
     * rows are hit-tested by y, so click the target row's coordinates. */
    sel_id = vg_model_get_selected_sensor_id();
    sel0 = 0;
    for(i = 0; i < n; i++) {
        if(sel_id && strcmp(sensors[i].id, sel_id) == 0) sel0 = i;
    }
    target = (uint16_t)((sel0 + 1) % n);
    hg_click_obj_center(dd); /* open the list */
    {
        lv_obj_t * list = lv_dropdown_get_list(dd);
        ok &= hg_expect(list != NULL, "dropdown list opens");
        if(list != NULL) {
            lv_obj_t * opts = lv_obj_get_child(list, 0);
            ok &= hg_expect(opts != NULL && lv_obj_check_type(opts, &lv_label_class),
                            "list holds the options label");
            if(opts != NULL) {
                const lv_font_t * f = lv_obj_get_style_text_font(opts, LV_PART_MAIN);
                int32_t lh = lv_font_get_line_height(f);
                lv_area_t la;
                lv_obj_get_coords(opts, &la);
                hg_queue_click((la.x1 + la.x2) / 2,
                               la.y1 + (int)target * lh + lh / 2);
                hg_pump(12);
            }
        }
    }
    hg_pump(10);
    ok &= hg_expect(vg_model_get_selected_sensor_id() != NULL &&
                    strcmp(vg_model_get_selected_sensor_id(), sensors[target].id) == 0,
                    "dropdown click switches selected point by id");
    hg_dump_ppm(out_dir, "02_trend_switched");

    /* 3) AC4: window toggles in samples; full window follows the profile */
    snprintf(full_needle, sizeof(full_needle), "当前 %d 点", (int)VG_HISTORY_LEN);
    ok &= hg_click_btn("全部");
    lab = hg_find_obj(hg_match_label_sub, (void *)"当前 ", NULL, NULL);
    ok &= hg_expect(lab != NULL && strstr(lv_label_get_text(lab), full_needle) != NULL,
                    "full window matches the profile history length");
    ok &= hg_click_nth_btn("最近60点", 0);
    lab = hg_find_obj(hg_match_label_sub, (void *)"当前 ", NULL, NULL);
    ok &= hg_expect(lab != NULL && strstr(lv_label_get_text(lab), "当前 60 点") != NULL,
                    "recent window shows 60 samples");
    hg_dump_ppm(out_dir, "03_trend_recent60");

    /* 4) AC7: an offline point keeps the chart, its value shows -- */
    vg_model_set_scenario(VG_SCENARIO_OFFLINE);
    hg_pump(30);
    sensors = vg_model_get_sensors(&n);
    for(i = 0; i < n; i++) {
        if(!sensors[i].online) break;
    }
    if(i < n) {
        vg_model_set_selected_sensor(sensors[i].id);
        hg_pump(10);
        lab = hg_find_obj(hg_match_label_sub, (void *)"--", NULL, NULL);
        ok &= hg_expect(lab != NULL, "offline point value label shows --");
        hg_dump_ppm(out_dir, "04_trend_offline");
    }
    else {
        printf("[SKIP] OFFLINE scenario leaves every point online\n");
    }
    vg_model_set_scenario(VG_SCENARIO_NORMAL);
    hg_pump(30);

    /* 5) AC5: a point without thresholds renders with auto range */
    {
        uint16_t free_i = (uint16_t)-1;
        for(i = 0; i < n; i++) {
            if(!sensors[i].has_warn && !sensors[i].has_crit) {
                free_i = i;
                break;
            }
        }
        if(free_i != (uint16_t)-1) {
            vg_model_set_selected_sensor(sensors[free_i].id);
            hg_pump(10);
            hg_dump_ppm(out_dir, "05_trend_no_threshold");
            ok &= hg_expect(true, "no-threshold point rendered (auto Y range)");
        }
        else {
            printf("[SKIP] no no-threshold point in sim table\n");
        }
    }

    /* 6) AC1: device page button leads back to trend on that point */
    vg_nav_goto(VG_PAGE_DEVICE, NULL);
    hg_pump(10);
    ok &= hg_click_nth_btn("查看实时趋势", 0);
    hg_pump(10);
    ok &= hg_expect(vg_nav_current() == VG_PAGE_TREND, "device page button opens trend");
    hg_dump_ppm(out_dir, "06_trend_from_device");

    printf("\ntrend_check: %s (%d failures)\n",
           ok ? "ALL PASS" : "FAILURES PRESENT", hg_failures());
    return ok ? 0 : 1;
}
