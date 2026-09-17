#ifndef VG_UI_BACKEND_H
#define VG_UI_BACKEND_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

/* The AI advice fields are the board's own contract type: the board backend
 * parses the agent's file into it and the page reads it back, so both ends
 * must agree byte for byte.  Visible from gui/ because gui/CMakeLists.txt and
 * app/velaguard/Makefile both put app/velaguard on the include path. */
#include "vg_ai_contract.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t  addr;
    uint16_t probe_reg;
    char     label[32];
} vg_ui_slave_t;

/* Board live net snapshot for the status bar (no IO on the caller side) */
typedef struct {
    bool rj45_has_ip;
    bool wifi_assoc;
    bool wifi_has_ip;
    bool mqtt_online;   /* MiMo bridge: MQTT CONNACK received */
    bool aud_ok;        /* /dev/pwm0 (alarm buzzer) accessible */
    int  egress;        /* vg_egress_t: 0 none, 1 rj45, 2 wifi */
    char ip[16];        /* active egress IP, empty when none */
} vg_ui_net_live_t;

typedef enum {
    VG_UI_REPORT_IDLE = 0,
    VG_UI_REPORT_READING,
    VG_UI_REPORT_GENERATING,
    VG_UI_REPORT_READY,
    VG_UI_REPORT_EMPTY,
    VG_UI_REPORT_ERROR
} vg_ui_report_status_t;

typedef struct {
    uint32_t request_id;
    uint32_t version;
    vg_ui_report_status_t status;
    int err;
    uint32_t elapsed_s;
    char path[128];
    char body[1536];
    bool truncated;
    /* true when the report shown came from the board agent, false when it is
     * the firmware's own deterministic statistics.  The page labels the
     * source from this so a reader can never mistake one for the other. */
    bool from_agent;
} vg_ui_report_snapshot_t;

/* Per-point AI advice for the alarm page.
 *
 * The board asks the agent for advice when the alarm set changes and caches
 * the validated answer, so the page itself never does IO and never blocks.
 * VG_UI_ADV_IDLE       nothing outstanding, no advice to show
 * VG_UI_ADV_PENDING    a round is queued or running
 * VG_UI_ADV_READY      a validated document is loaded
 * VG_UI_ADV_ERROR      the round failed, timed out, or its answer was rejected
 */
typedef enum {
    VG_UI_ADV_IDLE = 0,
    VG_UI_ADV_PENDING,
    VG_UI_ADV_READY,
    VG_UI_ADV_ERROR
} vg_ui_advice_state_t;

void                 vg_ui_alarm_advice_request(void);
vg_ui_advice_state_t vg_ui_alarm_advice_state(void);

/* Look up one alarm episode.  Returns true only when a loaded document has
 * an entry for exactly this point and episode; the epoch test is what stops
 * advice from a previous alarm on the same point being shown again. */
bool vg_ui_alarm_advice_get(const char *sensor_id, uint32_t al_epoch,
                            vg_ai_advice_entry_t *out);

/* PC simulator / headless only: install canned advice so the page can be
 * exercised without a board.  Defined in gui/main/ui/model/vg_ui_backend.c,
 * which the firmware build filters out; pass n == 0 to clear. */
void vg_ui_backend_mock_set_advice(const vg_ai_advice_entry_t *entries, int n);

/* discover_*_status: 0 idle, 1 running, 2 done, <0 error */
typedef struct {
    int (*discover_scan_start)(int addr_min, int addr_max);
    int (*discover_scan_status)(void);
    int (*discover_apply_start)(void); /* probe+infer+points.json+vgcfg */
    int (*discover_apply_status)(void);
    int (*get_slaves)(vg_ui_slave_t *out, int max);
    int (*read_latest_report)(char *body, size_t body_sz,
                              char *path, size_t path_sz,
                              bool *from_agent);
    /* Ask the on-device agent to generate today's daily report (MiMo).
     * Returns false when the platform has no agent backend. */
    bool (*request_daily_report)(void);
    int (*report_request)(bool allow_generate, uint32_t *request_id);
    bool (*report_snapshot)(vg_ui_report_snapshot_t *out);
} vg_ui_backend_t;

const vg_ui_backend_t *vg_ui_backend_get(void);

int vg_ui_report_request(bool allow_generate, uint32_t *request_id);
bool vg_ui_report_snapshot(vg_ui_report_snapshot_t *out);

/* Board discover: vg_bus_scan / apply return or negative errno */
int vg_ui_backend_scan_last_result(void);
int vg_ui_backend_apply_last_result(void);
void vg_ui_backend_scan_progress(int *cur_addr, int *addr_max);
void vg_ui_backend_acq_start(void);
bool vg_ui_backend_apply_live(void);
void vg_ui_backend_boot_points(void);

/* Real net/buzzer state for the status bar. Returns false when the
 * platform has no live source (PC sim) — model keeps scenario values. */
bool vg_ui_backend_poll_net(vg_ui_net_live_t *out);

#ifdef __cplusplus
}
#endif

#endif /* VG_UI_BACKEND_H */
