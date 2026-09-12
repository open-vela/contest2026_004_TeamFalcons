#ifndef VG_UI_BACKEND_H
#define VG_UI_BACKEND_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

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

/* discover_*_status: 0 idle, 1 running, 2 done, <0 error */
typedef struct {
    int (*discover_scan_start)(int addr_min, int addr_max);
    int (*discover_scan_status)(void);
    int (*discover_apply_start)(void); /* probe+infer+points.json+vgcfg */
    int (*discover_apply_status)(void);
    int (*get_slaves)(vg_ui_slave_t *out, int max);
    int (*read_latest_report)(char *body, size_t body_sz,
                              char *path, size_t path_sz);
} vg_ui_backend_t;

const vg_ui_backend_t *vg_ui_backend_get(void);

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
