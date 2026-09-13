/* Deterministic point-table fixture check for the VelaGuard HMI
 * (task 09-13-hmi-performance-baseline).
 *
 * Feeds the model through its public import/live APIs — no NSH, no RS485,
 * no board config — and asserts rendering and interaction on the real UI:
 *   - empty table renders without rows or fake points
 *   - a 32-point table (VG_DISCOVER_MAX_POINTS) renders and rows navigate
 *   - duplicate display names and a 23-char legal ID are preserved
 *   - scenario-driven alarm counts the PC model can produce (0/2/3; the
 *     1- and 8-alarm datasets need the board alarm path — see B4/D3)
 *   - history depth follows the build's capacity profile (VG_HISTORY_LEN)
 *
 * Usage: fixture_check <out-dir>
 */

#include "harness_common.h"
#include "model/vg_model.h"
#include "shell/vg_shell.h"
#include <stdio.h>
#include <string.h>

#define FIXTURE_N 32

static void build_fixture(vg_runtime_point_t * pts)
{
    int i;
    static const char * units[] = {"C", "%", "kPa"};

    for(i = 0; i < FIXTURE_N; i++) {
        vg_runtime_point_t * p = &pts[i];
        memset(p, 0, sizeof(*p));
        snprintf(p->id, sizeof(p->id), "PT%03d", i);
        if(i == 10) {
            /* exactly 23 chars: VG_SENSOR_ID_MAX - 1 */
            snprintf(p->id, sizeof(p->id), "P0000000000000000000001");
        }
        if(i == 5 || i == 6) {
            /* duplicate display names are legal and must both survive */
            snprintf(p->name, sizeof(p->name), "重复名称测点");
        }
        else {
            snprintf(p->name, sizeof(p->name), "测点%02d", i);
        }
        p->addr = (uint8_t)(i + 1);
        p->fc = 3;
        p->reg = (uint16_t)(i + 1);
        snprintf(p->unit, sizeof(p->unit), "%s", units[i % 3]);
        snprintf(p->dtype, sizeof(p->dtype), "uint16");
        p->scale = 1.0f;
        snprintf(p->cmp, sizeof(p->cmp), "ge");
        if(i % 2 == 0) {
            p->has_warn = 1; p->warn = 40.0f;
            p->has_crit = 1; p->crit = 80.0f;
        }
        p->fail_n = 3;
    }
}

int main(int argc, char ** argv)
{
    const char * out_dir = (argc > 1) ? argv[1] : ".";
    bool ok = true;
    vg_runtime_point_t pts[FIXTURE_N];
    const vg_sensor_t * sensors;
    uint16_t n = 0;
    uint16_t i;
    char needle[48];

    build_fixture(pts);
    hg_init();

    /* 1) empty table: clean import, no rows, no fake points */
    vg_model_import_runtime_points(NULL, 0);
    hg_pump(30);
    sensors = vg_model_get_sensors(&n);
    ok &= hg_expect(sensors != NULL && n == 0, "empty table yields zero points");
    ok &= hg_expect(vg_model_active_alarm_count() == 0, "empty table has no alarms");
    hg_dump_ppm(out_dir, "01_fixture_home_empty");
    ok &= hg_expect_not_blank("home renders an empty-state page");

    /* 2) 32-point table: count, duplicate names, 23-char ID survive */
    vg_model_import_runtime_points(pts, FIXTURE_N);
    hg_pump(30);
    sensors = vg_model_get_sensors(&n);
    ok &= hg_expect(n == FIXTURE_N, "32-point table imports fully");
    {
        uint16_t dup = 0, long_id = 0;
        for(i = 0; i < n; i++) {
            if(strcmp(sensors[i].name, "重复名称测点") == 0) dup++;
            if(strlen(sensors[i].id) == 23 &&
               strcmp(sensors[i].id, "P0000000000000000000001") == 0) long_id++;
        }
        ok &= hg_expect(dup == 2, "duplicate display names preserved");
        ok &= hg_expect(long_id == 1, "23-char legal ID preserved");
    }
    hg_dump_ppm(out_dir, "02_fixture_home_32pt");
    ok &= hg_expect_not_blank("home renders 32-point list");

    /* 3) real click on the first home row opens the device page.
     * Home rows are clickable tiles (not buttons), so locate the row by
     * its name label and click the label's coordinates. */
    {
        lv_obj_t * lab = hg_find_obj(hg_match_label_sub, (void *)"测点00",
                                     NULL, NULL);
        ok &= hg_expect(lab != NULL, "home shows the first fixture row");
        ok &= hg_click_obj_center(lab);
    }
    hg_pump(10);
    ok &= hg_expect(vg_nav_current() == VG_PAGE_DEVICE,
                    "home row click opens device page");
    hg_dump_ppm(out_dir, "03_fixture_device_from_row");
    vg_nav_back();
    hg_pump(10);
    ok &= hg_expect(vg_nav_current() == VG_PAGE_HOME, "back returns home");

    /* 4) online values drive the trend history to the profile depth */
    for(i = 0; i < FIXTURE_N; i++) {
        ok &= vg_model_set_live(i, 20.0f + (float)i, true);
    }
    for(i = 0; i <= VG_HISTORY_LEN; i++) {
        vg_model_set_live(0, 20.0f + (float)(i % 10), true);
    }
    vg_model_set_selected_sensor(sensors[0].id);
    vg_nav_goto(VG_PAGE_TREND, NULL);
    hg_pump(30);
    ok &= hg_click_btn("全部"); /* trend defaults to the 60-sample window */
    hg_pump(10);
    snprintf(needle, sizeof(needle), "当前 %d 点", (int)VG_HISTORY_LEN);
    {
        lv_obj_t * lab = hg_find_obj(hg_match_label_sub, (void *)"当前 ", NULL, NULL);
        printf("[INFO] trend label: %s\n",
               (lab != NULL) ? lv_label_get_text(lab) : "(none)");
        ok &= hg_expect(lab != NULL &&
                        strstr(lv_label_get_text(lab), needle) != NULL,
                        "trend full window matches profile history depth");
    }
    hg_dump_ppm(out_dir, "04_fixture_trend_full_history");
    vg_nav_back();
    hg_pump(10);

    /* 5) scenario alarm counts reachable on the PC model path */
    vg_model_set_scenario(VG_SCENARIO_WARN);
    hg_pump(40);
    ok &= hg_expect(vg_model_active_alarm_count() == 3,
                    "WARN scenario yields 3 episodes on fixture table");
    vg_nav_goto(VG_PAGE_ALARM, NULL);
    hg_pump(10);
    hg_dump_ppm(out_dir, "05_fixture_alarm_warn3");
    ok &= hg_expect_not_blank("alarm page renders rows");
    vg_nav_back();
    hg_pump(10);

    vg_model_set_scenario(VG_SCENARIO_OFFLINE);
    hg_pump(40);
    ok &= hg_expect(vg_model_active_alarm_count() == 2,
                    "OFFLINE scenario yields 2 episodes on fixture table");
    vg_model_set_scenario(VG_SCENARIO_NORMAL);
    hg_pump(40);
    ok &= hg_expect(vg_model_active_alarm_count() == 0,
                    "NORMAL clears every episode");
    printf("[INFO] 1- and 8-alarm datasets need the board alarm path "
           "(vg_model_set_live board branch); covered by B4/D3, not here\n");

    printf("\nfixture_check: %s (%d failures)\n",
           ok ? "ALL PASS" : "FAILURES PRESENT", hg_failures());
    return ok ? 0 : 1;
}
