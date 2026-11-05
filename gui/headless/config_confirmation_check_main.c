/* Headless local-confirmation UI check.
 *
 * Exercises the real LVGL surface with an offline provider: candidate details
 * are visible, alarm hides and then restores the same version, and one click
 * submits exactly one version-bound action.
 */

#include "harness_common.h"
#include "model/vg_model.h"
#include "widgets/vg_config_confirmation_ui.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

static struct vg_config_pending_preview s_pending;
static struct vg_config_confirmation_action s_action;
static int s_action_count;
static int s_load_result;

static void fill_text(char *out, size_t size, char value)
{
    memset(out, value, size - 1);
    out[size - 1] = '\0';
}

static void seed_candidate(void)
{
    memset(&s_pending, 0, sizeof(s_pending));
    s_pending.valid = true;
    strcpy(s_pending.task_id, "task-pdf-01");
    fill_text(s_pending.session_id, sizeof(s_pending.session_id), '2');
    s_pending.revision = 3;
    s_pending.has_base_hash = true;
    fill_text(s_pending.base_hash, sizeof(s_pending.base_hash), 'a');
    fill_text(s_pending.payload_hash, sizeof(s_pending.payload_hash), 'b');
    fill_text(s_pending.candidate_hash, sizeof(s_pending.candidate_hash), 'c');
    fill_text(s_pending.confirmation_id, sizeof(s_pending.confirmation_id), 'd');

    s_pending.candidate.schema_version = 1;
    strcpy(s_pending.candidate.mode, "add_sensor");
    s_pending.candidate.baud = 9600;
    s_pending.candidate.slave_address = 7;
    strcpy(s_pending.candidate.model, "TH-01");
    strcpy(s_pending.candidate.manual_id, "manual-th01");
    fill_text(s_pending.candidate.manual_sha256,
              sizeof(s_pending.candidate.manual_sha256), 'e');
    s_pending.candidate.point_count = 2;

    strcpy(s_pending.candidate.points[0].id, "s7_temp");
    strcpy(s_pending.candidate.points[0].name, "温度");
    s_pending.candidate.points[0].addr = 7;
    s_pending.candidate.points[0].fc = 3;
    s_pending.candidate.points[0].reg = 0;
    strcpy(s_pending.candidate.points[0].dtype, "int16");
    s_pending.candidate.points[0].scale = 0.1f;
    strcpy(s_pending.candidate.points[0].unit, "C");
    strcpy(s_pending.candidate.points[0].original_address, "0x0000");

    strcpy(s_pending.candidate.points[1].id, "s7_hum");
    strcpy(s_pending.candidate.points[1].name, "湿度");
    s_pending.candidate.points[1].addr = 7;
    s_pending.candidate.points[1].fc = 4;
    s_pending.candidate.points[1].reg = 1;
    strcpy(s_pending.candidate.points[1].dtype, "uint16");
    s_pending.candidate.points[1].scale = 0.1f;
    strcpy(s_pending.candidate.points[1].unit, "%");
    strcpy(s_pending.candidate.points[1].original_address, "0x0001");

    s_pending.candidate.evidence_count = 1;
    strcpy(s_pending.candidate.evidence[0].id, "ev-temp");
    s_pending.candidate.evidence[0].page = 12;
    strcpy(s_pending.candidate.evidence[0].quote,
           "温度寄存器 0x0000，int16，倍率 0.1，单位 C。");
}

static int load_pending(void *ctx,
                        struct vg_config_pending_preview *out)
{
    (void)ctx;
    if(s_load_result != 0) return s_load_result;
    *out = s_pending;
    return 0;
}

static int submit_action(
    void *ctx,
    const struct vg_config_confirmation_action *action)
{
    (void)ctx;
    s_action = *action;
    s_action_count++;
    return 0;
}

static bool label_exists(const char *text)
{
    int x;
    int y;
    return hg_find_obj(hg_match_label_sub, (void *)text, &x, &y) != NULL;
}

int main(int argc, char **argv)
{
    const char *out_dir = argc > 1 ? argv[1] : ".";
    struct vg_config_confirmation_ports ports = {
        load_pending, submit_action, NULL
    };
    bool ok = true;

    seed_candidate();
    hg_init();
    vg_config_confirmation_ui_init();
    ok &= hg_expect(vg_config_confirmation_ui_bind(&ports) == 0,
                    "bind offline confirmation provider");
    hg_pump(4);

    ok &= hg_expect(vg_config_confirmation_ui_is_open(),
                    "validated candidate opens local confirmation");
    ok &= hg_expect(label_exists("云端候选待确认"),
                    "confirmation title is visible");
    ok &= hg_expect(label_exists("task-pdf-01"),
                    "current task is visible");
    ok &= hg_expect(label_exists("温度"),
                    "validated point is visible");
    ok &= hg_expect(label_exists("s7_temp"),
                    "validated point id is visible");
    ok &= hg_expect(label_exists("0x0000"),
                    "point register is visible");
    ok &= hg_expect(label_exists("手册第 12 页"),
                    "manual evidence page is visible");
    ok &= hg_expect_not_blank("confirmation frame has visible content");
    hg_dump_ppm(out_dir, "config-confirm-open");

    vg_model_set_scenario(VG_SCENARIO_CRIT);
    hg_pump(8);
    ok &= hg_expect(!vg_config_confirmation_ui_is_open(),
                    "active alarm preempts confirmation");

    vg_model_set_scenario(VG_SCENARIO_NORMAL);
    hg_pump(8);
    ok &= hg_expect(vg_config_confirmation_ui_is_open(),
                    "same valid version returns after alarm clears");

    ok &= hg_expect(hg_click_btn("确认添加"),
                    "click exact candidate confirmation");
    hg_pump(4);
    ok &= hg_expect(s_action_count == 1,
                    "one click emits one local action");
    ok &= hg_expect(s_action.kind == VG_CONFIG_CONFIRMATION_CONFIRM,
                    "confirmation action kind is preserved");
    ok &= hg_expect(s_action.request.confirm.revision == 3 &&
                    strcmp(s_action.request.confirm.task_id,
                           s_pending.task_id) == 0 &&
                    strcmp(s_action.request.confirm.confirmation_id,
                           s_pending.confirmation_id) == 0,
                    "action carries task, revision and confirmation id");
    ok &= hg_expect(!vg_config_confirmation_ui_is_open(),
                    "confirmation surface closes after one action");

    ok &= hg_expect(vg_config_confirmation_ui_bind(&ports) == 0,
                    "rebind for cancel check");
    hg_pump(4);
    ok &= hg_expect(vg_config_confirmation_ui_is_open(),
                    "candidate reopens for cancel check");
    ok &= hg_expect(hg_click_btn("取消"),
                    "click candidate cancel");
    hg_pump(4);
    ok &= hg_expect(s_action_count == 2 &&
                    s_action.kind == VG_CONFIG_CONFIRMATION_CANCEL,
                    "cancel emits one exact local cancel");
    ok &= hg_expect(!vg_config_confirmation_ui_is_open(),
                    "cancel closes the confirmation");

    s_load_result = -ENOENT;
    vg_config_confirmation_ui_poll();
    ok &= hg_expect(!vg_config_confirmation_ui_is_open(),
                    "expired or removed candidate stays hidden");

    printf("config_confirmation_check: %s (%d failures)\n",
           ok && hg_failures() == 0 ? "ALL PASS" : "FAIL",
           hg_failures());
    return (ok && hg_failures() == 0) ? 0 : 1;
}
