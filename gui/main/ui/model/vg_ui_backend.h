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

#ifdef __cplusplus
}
#endif

#endif /* VG_UI_BACKEND_H */
