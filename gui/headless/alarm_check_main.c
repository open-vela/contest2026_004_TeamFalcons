/* Headless render check for the multi-row alarm page.
 *
 * Builds the LVGL UI into a 480x272 off-screen buffer (no SDL, no board),
 * drives the real pointer indev with scripted clicks located by widget
 * text, dumps frames as PPM for visual inspection, and prints model-level
 * PASS/FAIL lines. Plumbing shared via harness_common (format-aware
 * buffer/decode); test logic unchanged from the original standalone
 * harness. Not compiled into firmware (app/velaguard Makefile) or the SDL
 * simulator (gui/CMakeLists.txt globs main/ui only).
 *
 * Usage: alarm_check <out-dir>
 */

#include "harness_common.h"
#include "model/vg_model.h"
#include "shell/vg_shell.h"
#include <stdio.h>
#include <string.h>

static void print_alarms(void)
{
    vg_alarm_t list[16];
    uint16_t n = vg_model_collect_alarms(list, 16);
    uint16_t i;
    printf("alarms: total=%u quieted=%d\n", (unsigned)vg_model_active_alarm_count(),
           vg_model_alarms_all_quieted() ? 1 : 0);
    for(i = 0; i < n; i++) {
        printf("  [%u] id=%s sev=%d acked=%d muted=%d dur=%d\n", (unsigned)i,
               list[i].sensor_id, (int)list[i].severity, list[i].acked ? 1 : 0,
               list[i].muted ? 1 : 0, (int)list[i].duration_sec);
    }
}

int main(int argc, char ** argv)
{
    const char * out_dir = (argc > 1) ? argv[1] : ".";
    bool ok = true;
    vg_alarm_t list[16];
    uint16_t n;

    hg_init();

    /* 1) CRIT scenario seeds 3 episodes: primary crit + s9 crit + s3 warn */
    vg_model_set_scenario(VG_SCENARIO_CRIT);
    vg_nav_goto(VG_PAGE_ALARM, NULL);
    hg_pump(40); /* >= 2 model ticks so durations advance */
    hg_dump_ppm(out_dir, "01_alarm_crit_multi");
    print_alarms();
    n = vg_model_collect_alarms(list, 16);
    ok &= hg_expect(n == 3, "crit scenario yields 3 alarm rows");
    ok &= hg_expect(list[0].severity == VG_SEV_CRIT, "rows sorted: primary crit first");
    ok &= hg_expect(list[2].severity == VG_SEV_WARN, "rows sorted: warn last");
    ok &= hg_expect_not_blank("crit alarm page renders content");

    /* 2) ack + mute row 2 (2nd crit) via its real buttons */
    ok &= hg_click_nth_btn("标记处理", 1);
    ok &= hg_click_nth_btn("静音", 1);
    hg_dump_ppm(out_dir, "02_alarm_row2_quieted");
    print_alarms();
    ok &= hg_expect(vg_model_collect_alarms(list, 16) == 3, "ack/mute keeps episodes alive");
    ok &= hg_expect(list[1].acked && list[1].muted, "row2 acked+muted");
    ok &= hg_expect(!list[0].acked && !list[0].muted, "row1 untouched");
    ok &= hg_expect(!vg_model_alarms_all_quieted(), "not all quieted (row3 active)");

    /* 3) quiet the rest FIRST (rows still in view): row1 mute, row3 ack
     *    -> all quieted, status chip would dim */
    ok &= hg_click_nth_btn("静音", 0);
    ok &= hg_click_nth_btn("标记处理", 2);
    hg_dump_ppm(out_dir, "03_alarm_all_quieted");
    print_alarms();
    ok &= hg_expect(vg_model_alarms_all_quieted(), "all alarms quieted after per-row actions");

    /* 4) select row 2 LAST: tapping its card scrolls the detail of that
     *    row into view (rows scroll out, so do this after all clicks) */
    ok &= hg_click_row("标记处理", 1);
    hg_pump(30);
    hg_dump_ppm(out_dir, "04_alarm_select_row2");

    /* 5) empty state */
    vg_model_set_scenario(VG_SCENARIO_NORMAL);
    hg_pump(30);
    hg_dump_ppm(out_dir, "05_alarm_empty");
    print_alarms();
    ok &= hg_expect(vg_model_active_alarm_count() == 0, "normal scenario clears all episodes");

    /* 6) WARN scenario: 3 warn rows, rebuild from empty */
    vg_model_set_scenario(VG_SCENARIO_WARN);
    hg_pump(30);
    hg_dump_ppm(out_dir, "06_alarm_warn_multi");
    print_alarms();
    n = vg_model_collect_alarms(list, 16);
    ok &= hg_expect(n == 3, "warn scenario yields 3 alarm rows");

    printf("\nalarm_check: %s (%d failures)\n",
           ok ? "ALL PASS" : "FAILURES PRESENT", hg_failures());
    return ok ? 0 : 1;
}
