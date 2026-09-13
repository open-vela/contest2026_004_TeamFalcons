/* Headless interaction checks for 09-13-hmi-touch-acceptance (T4).
 *
 * Practical subset of research/interaction-cases.md driven by the scripted
 * pointer indev (no board touch hardware):
 *   I01 click home tile -> device
 *   I02 press + drag cancels navigation
 *   I11 filter preserved across navigation
 *   I12 trend window restore
 *   I13 deleted device point returns home
 *   I16 mid-press nav + wait_release does not open old target
 *   I17 bounded nav stack
 *   I18 confirm closes before page back
 *   I19 discover scan defaults off
 *   I22 32-point fleet + alarm list cap
 *   I24 trend history advances without leaving page
 */

#include "harness_common.h"
#include "model/vg_model.h"
#include "shell/vg_shell.h"
#include "widgets/vg_confirm_dialog.h"
#include <stdio.h>
#include <string.h>

static void seed_points_range(int start, int n)
{
    vg_runtime_point_t pts[32];
    int i;

    if(n > 32) n = 32;
    if(start < 0) start = 0;
    memset(pts, 0, sizeof(pts));
    for(i = 0; i < n; i++) {
        int id = start + i;
        snprintf(pts[i].id, sizeof(pts[i].id), "PT%02d", id);
        snprintf(pts[i].name, sizeof(pts[i].name), "Sensor%02d", id);
        pts[i].addr = 1;
        pts[i].fc = 3;
        pts[i].reg = (uint16_t)(100 + id);
        snprintf(pts[i].unit, sizeof(pts[i].unit), "C");
        snprintf(pts[i].dtype, sizeof(pts[i].dtype), "uint16");
        pts[i].scale = 1.0f;
        snprintf(pts[i].cmp, sizeof(pts[i].cmp), "ge");
        pts[i].fail_n = 3;
    }
    vg_model_import_runtime_points(pts, (uint16_t)n);
    for(i = 0; i < n; i++) {
        vg_model_set_live(i, 20.0f + (float)(start + i), true);
    }
    hg_pump(8);
}

static void seed_points(int n)
{
    seed_points_range(0, n);
}

static bool click_home_tile(const char * name_sub)
{
    int cx = 0, cy = 0;
    lv_obj_t * lab = hg_find_obj(hg_match_label_sub, (void *)name_sub, &cx, &cy);
    lv_obj_t * tile;

    if(lab == NULL) {
        printf("FAIL find tile label '%s'\n", name_sub);
        return false;
    }
    tile = lv_obj_get_parent(lab);
    if(tile == NULL) return false;
    return hg_click_obj_center(tile);
}

static bool drag_home_tile(const char * name_sub)
{
    int cx = 0, cy = 0;
    lv_obj_t * lab = hg_find_obj(hg_match_label_sub, (void *)name_sub, &cx, &cy);
    lv_obj_t * tile;
    lv_area_t a;
    int y1;

    if(lab == NULL) {
        printf("FAIL find tile label '%s' for drag\n", name_sub);
        return false;
    }
    tile = lv_obj_get_parent(lab);
    if(tile == NULL) return false;
    lv_obj_get_coords(tile, &a);
    cx = (a.x1 + a.x2) / 2;
    cy = (a.y1 + a.y2) / 2;
    y1 = cy + 100;
    if(y1 > HG_H - 8) y1 = HG_H - 8;
    hg_queue_drag(cx, cy, cx, y1, 12);
    hg_pump(40);
    return true;
}

static bool press_hold_home_tile(const char * name_sub, int hold_frames)
{
    int cx = 0, cy = 0;
    lv_obj_t * lab = hg_find_obj(hg_match_label_sub, (void *)name_sub, &cx, &cy);
    lv_obj_t * tile;
    lv_area_t a;

    if(lab == NULL) {
        printf("FAIL find tile label '%s' for hold\n", name_sub);
        return false;
    }
    tile = lv_obj_get_parent(lab);
    if(tile == NULL) return false;
    lv_obj_get_coords(tile, &a);
    hg_queue_press_hold((a.x1 + a.x2) / 2, (a.y1 + a.y2) / 2, hold_frames);
    return true;
}

int main(int argc, char ** argv)
{
    const char * out_dir = (argc > 1) ? argv[1] : ".";
    bool ok = true;
    int i;
    uint16_t home_n;
    vg_alarm_t alarms[16];
    uint16_t alarm_n;
    const vg_sensor_t * s0;
    uint32_t hist_v1;
    uint32_t hist_v2;

    (void)out_dir;
    hg_init();
    seed_points(16);

    /* I01: tap first home tile enters device once */
    ok &= hg_expect(vg_nav_current() == VG_PAGE_HOME, "start on home");
    ok &= hg_expect(click_home_tile("Sensor00"), "I01 click home tile");
    ok &= hg_expect(vg_nav_current() == VG_PAGE_DEVICE, "I01 opens device");
    vg_nav_back();
    hg_pump(6);
    ok &= hg_expect(vg_nav_current() == VG_PAGE_HOME, "back to home");

    /* I02: press then drag should scroll, not open device */
    ok &= hg_expect(drag_home_tile("Sensor00"), "I02 perform drag");
    ok &= hg_expect(vg_nav_current() == VG_PAGE_HOME,
                    "I02 drag cancels tile navigation");

    /* I11: filter offline (exact chip label "离线 0" with seeded online fleet) */
    ok &= hg_expect(hg_click_btn("离线 0"), "click offline filter");
    ok &= hg_expect(vg_model_get_home_filter() == VG_HOME_FILTER_OFFLINE,
                    "I11 offline filter applied");
    vg_nav_goto(VG_PAGE_ALARM, NULL);
    hg_pump(4);
    vg_nav_back();
    hg_pump(6);
    ok &= hg_expect(vg_nav_current() == VG_PAGE_HOME, "I11 back to home");
    ok &= hg_expect(vg_model_get_home_filter() == VG_HOME_FILTER_OFFLINE,
                    "I11 filter preserved across navigation");
    ok &= hg_expect(hg_click_btn("全部 16"), "restore all filter");
    ok &= hg_expect(vg_model_get_home_filter() == VG_HOME_FILTER_ALL,
                    "I11 all filter restored");

    /* I12: trend window recent/all restore */
    vg_model_set_selected_sensor("PT00");
    vg_nav_goto(VG_PAGE_TREND, NULL);
    hg_pump(8);
    ok &= hg_expect(vg_nav_current() == VG_PAGE_TREND, "open trend");
    ok &= hg_expect(hg_click_btn("全部"), "select full history window");
    vg_nav_goto(VG_PAGE_HOME, NULL);
    hg_pump(6);
    vg_nav_back();
    hg_pump(8);
    ok &= hg_expect(vg_nav_current() == VG_PAGE_TREND, "I12 back restores trend page");

    /* I13: leave device, delete that point, back must land on home */
    vg_nav_goto(VG_PAGE_HOME, NULL);
    hg_pump(4);
    ok &= hg_expect(click_home_tile("Sensor00"), "I13 open device for PT00");
    ok &= hg_expect(vg_nav_current() == VG_PAGE_DEVICE, "I13 on device");
    seed_points_range(1, 15); /* drop PT00 / Sensor00 */
    ok &= hg_expect(vg_model_get_sensor("PT00") == NULL, "I13 PT00 removed");
    vg_nav_back();
    hg_pump(8);
    ok &= hg_expect(vg_nav_current() == VG_PAGE_HOME,
                    "I13 deleted point returns home, not another device");
    seed_points(16);

    /* I16: mid-press navigation + wait_release must not open the old tile */
    ok &= hg_expect(press_hold_home_tile("Sensor01", 40), "I16 queue hold");
    hg_pump(4); /* press latched */
    vg_nav_goto(VG_PAGE_ALARM, NULL);
    hg_pump(50); /* hold completes + release under wait_release */
    ok &= hg_expect(vg_nav_current() == VG_PAGE_ALARM,
                    "I16 stays on alarm after mid-press release");

    /* I17: bounded stack */
    for(i = 0; i < 12; i++) {
        vg_nav_goto(VG_PAGE_ALARM, NULL);
        hg_pump(1);
        vg_nav_goto(VG_PAGE_REPORT, NULL);
        hg_pump(1);
        vg_nav_goto(VG_PAGE_DISCOVER, NULL);
        hg_pump(1);
    }
    for(i = 0; i < 24; i++) {
        vg_nav_back();
        hg_pump(1);
    }
    ok &= hg_expect(vg_nav_current() == VG_PAGE_HOME ||
                    vg_nav_current() == VG_PAGE_TREND ||
                    vg_nav_current() == VG_PAGE_DISCOVER ||
                    vg_nav_current() == VG_PAGE_ALARM ||
                    vg_nav_current() == VG_PAGE_REPORT,
                    "I17 back stays on a valid page");
    /* Drain to home */
    for(i = 0; i < 8; i++) {
        if(vg_nav_current() == VG_PAGE_HOME) break;
        vg_nav_back();
        hg_pump(1);
    }
    vg_nav_goto(VG_PAGE_HOME, NULL);
    hg_pump(4);

    /* I18: confirm dialog closes before page leave */
    vg_nav_goto(VG_PAGE_DISCOVER, NULL);
    hg_pump(8);
    ok &= hg_expect(vg_nav_current() == VG_PAGE_DISCOVER, "I18 on discover");
    vg_confirm_dialog_create("确认点表",
                             "测试确认框：返回应先关闭对话框。",
                             "high", "确认写入", NULL, NULL);
    hg_pump(4);
    ok &= hg_expect(vg_confirm_dialog_is_open(), "I18 confirm is open");
    vg_nav_back();
    hg_pump(4);
    ok &= hg_expect(!vg_confirm_dialog_is_open(), "I18 back closes confirm first");
    ok &= hg_expect(vg_nav_current() == VG_PAGE_DISCOVER,
                    "I18 still on discover after closing confirm");

    /* I19: leave/re-enter discover — scan stays default off */
    vg_nav_goto(VG_PAGE_HOME, NULL);
    hg_pump(4);
    vg_nav_goto(VG_PAGE_DISCOVER, NULL);
    hg_pump(8);
    ok &= hg_expect(vg_nav_current() == VG_PAGE_DISCOVER, "I19 re-enter discover");
    ok &= hg_expect(hg_click_btn("开始扫描 1-32"), "tap scan without enabling switch");
    ok &= hg_expect(vg_nav_current() == VG_PAGE_DISCOVER,
                    "I19 scan without switch stays on page");

    /* I22: 32-point fleet; alarm UI collects at most 8 rows */
    vg_nav_goto(VG_PAGE_HOME, NULL);
    hg_pump(4);
    seed_points(32);
    home_n = vg_model_home_sensor_count();
    ok &= hg_expect(home_n == 32, "I22 home shows 32 points");
    vg_model_set_scenario(VG_SCENARIO_CRIT);
    vg_nav_goto(VG_PAGE_ALARM, NULL);
    hg_pump(20);
    alarm_n = vg_model_collect_alarms(alarms, 16);
    ok &= hg_expect(alarm_n > 0 && alarm_n <= 8,
                    "I22 alarm collect stays within 8-row display cap");
    ok &= hg_expect(vg_model_active_alarm_count() >= alarm_n,
                    "I22 active count is at least displayed rows");
    vg_model_set_scenario(VG_SCENARIO_NORMAL);
    hg_pump(8);

    /* I24: identical live samples advance history on trend without leaving */
    seed_points(4);
    vg_model_set_selected_sensor("PT00");
    vg_nav_goto(VG_PAGE_TREND, NULL);
    hg_pump(10);
    s0 = vg_model_get_sensor("PT00");
    ok &= hg_expect(s0 != NULL && vg_nav_current() == VG_PAGE_TREND,
                    "I24 on trend with PT00");
    if(s0 != NULL) {
        hist_v1 = s0->history_version;
        for(i = 0; i < 5; i++) {
            vg_model_set_live(0, 42.0f, true);
            hg_pump(4);
        }
        hist_v2 = s0->history_version;
        ok &= hg_expect(hist_v2 > hist_v1, "I24 history advances on same value");
        ok &= hg_expect(vg_nav_current() == VG_PAGE_TREND,
                        "I24 trend page stays mounted");
    }

    printf("interaction_check: %s (%d failures)\n",
           ok && hg_failures() == 0 ? "ALL PASS" : "FAIL", hg_failures());
    return (ok && hg_failures() == 0) ? 0 : 1;
}
