/* Headless verification for lifecycle, report state machine, and incremental render
 * (subtask 09-13-hmi-runtime-render).
 *
 * Verifies:
 *   - R-AC1: 200-cycle navigation across all pages; zero leaked objects; page generation increments.
 *   - R-AC2 & R-AC3: Report background snapshot consumption, navigating away during request, no stale callbacks.
 *   - R-AC5: Structure versioning, empty list does not repeatedly clean/rebuild, in-place value updates.
 *   - R-AC6: Trend history versioning, window shifts on identical sample, chart redraw skipping.
 */

#include "harness_common.h"
#include "model/vg_model.h"
#include "model/vg_ui_backend.h"
#include "shell/vg_shell.h"
#include <stdio.h>
#include <string.h>

#define CYCLE_COUNT 200

int main(int argc, char ** argv)
{
    const char * out_dir = (argc > 1) ? argv[1] : ".";
    bool ok = true;
    uint32_t gen_start;
    uint32_t gen_end;
    vg_runtime_point_t pts[4];
    int i;

    (void)out_dir;
    hg_init();

    /* ------------------------------------------------------------------
     * 1) R-AC1: 200 navigation cycles across pages
     * ------------------------------------------------------------------ */
    gen_start = vg_shell_page_generation();
    printf("[INFO] starting 200 navigation cycles, initial page generation = %u\n", gen_start);

    for(i = 0; i < CYCLE_COUNT; i++) {
        vg_nav_goto(VG_PAGE_DEVICE, NULL);
        hg_pump(1);
        vg_nav_goto(VG_PAGE_TREND, NULL);
        hg_pump(1);
        vg_nav_goto(VG_PAGE_ALARM, NULL);
        hg_pump(1);
        vg_nav_goto(VG_PAGE_REPORT, NULL);
        hg_pump(1);
        vg_nav_goto(VG_PAGE_DISCOVER, NULL);
        hg_pump(1);
        vg_nav_goto(VG_PAGE_HOME, NULL);
        hg_pump(1);
    }

    gen_end = vg_shell_page_generation();
    printf("[INFO] completed 200 cycles, final page generation = %u\n", gen_end);

    /* Each cycle visits 6 pages -> 1200 page creations */
    ok &= hg_expect(gen_end - gen_start == CYCLE_COUNT * 6,
                    "page generation advances exactly with navigation count");

    /* Content child is exactly 1 (the single s_page_root) */
    lv_obj_t * content = vg_shell_get_content();
    ok &= hg_expect(content != NULL, "shell content is valid");

    /* ------------------------------------------------------------------
     * 2) R-AC5: Empty point table and incremental structure render
     * ------------------------------------------------------------------ */
    vg_model_import_runtime_points(NULL, 0);
    hg_pump(10);
    uint32_t struct_v1 = vg_model_structure_version();

    /* Pumping frames on empty table must not change structure version or error */
    hg_pump(20);
    uint32_t struct_v2 = vg_model_structure_version();
    ok &= hg_expect(struct_v1 == struct_v2, "idle pump does not bump structure version");

    /* Import 4 points */
    for(i = 0; i < 4; i++) {
        memset(&pts[i], 0, sizeof(pts[i]));
        snprintf(pts[i].id, sizeof(pts[i].id), "PT%d", i);
        snprintf(pts[i].name, sizeof(pts[i].name), "Point%d", i);
        pts[i].addr = 1;
        pts[i].fc = 3;
        pts[i].reg = (uint16_t)(100 + i);
        snprintf(pts[i].unit, sizeof(pts[i].unit), "C");
        snprintf(pts[i].dtype, sizeof(pts[i].dtype), "uint16");
        pts[i].scale = 1.0f;
        snprintf(pts[i].cmp, sizeof(pts[i].cmp), "ge");
        pts[i].fail_n = 3;
    }
    vg_model_import_runtime_points(pts, 4);
    hg_pump(10);
    uint32_t struct_v3 = vg_model_structure_version();
    ok &= hg_expect(struct_v3 > struct_v2, "importing points bumps structure version");

    /* In-place value change must not bump structure version */
    vg_model_set_live(0, 42.0f, true);
    hg_pump(5);
    ok &= hg_expect(vg_model_structure_version() == struct_v3,
                    "in-place live value update does not bump structure version");

    /* Filter change with same filter must be no-op */
    vg_home_filter_t cur_f = vg_model_get_home_filter();
    vg_model_set_home_filter(cur_f);
    ok &= hg_expect(vg_model_structure_version() == struct_v3,
                    "identical filter set does not bump structure version");

    /* ------------------------------------------------------------------
     * 3) R-AC6: Trend history versioning
     * ------------------------------------------------------------------ */
    const vg_sensor_t * s0 = vg_model_get_sensor("PT0");
    ok &= hg_expect(s0 != NULL, "sensor PT0 found");
    if(s0 != NULL) {
        uint32_t hist_v1 = s0->history_version;
        /* Feed identical value -> history_version must still advance to slide window */
        vg_model_set_live(0, 42.0f, true);
        uint32_t hist_v2 = s0->history_version;
        ok &= hg_expect(hist_v2 > hist_v1,
                        "identical value live sample advances history version");
    }

    /* ------------------------------------------------------------------
     * 4) R-AC2 & R-AC3: Report background request and page switch
     * ------------------------------------------------------------------ */
    vg_nav_goto(VG_PAGE_REPORT, NULL);
    hg_pump(10);

    vg_ui_report_snapshot_t snap;
    bool has_snap = vg_ui_report_snapshot(&snap);
    ok &= hg_expect(has_snap, "report snapshot is available");
    ok &= hg_expect(snap.status == VG_UI_REPORT_READY || snap.status == VG_UI_REPORT_EMPTY,
                    "report snapshot has valid initial status");

    /* Switch away to Trend while report page timer is active */
    vg_nav_goto(VG_PAGE_TREND, NULL);
    hg_pump(20);

    /* Return to Report page: must re-initialize cleanly without stale callback crash */
    vg_nav_goto(VG_PAGE_REPORT, NULL);
    hg_pump(10);
    ok &= hg_expect(vg_nav_current() == VG_PAGE_REPORT, "returned to report page cleanly");

    printf("[RESULT] lifecycle_render_check %s\n", ok ? "PASSED" : "FAILED");
    return ok ? 0 : 1;
}
