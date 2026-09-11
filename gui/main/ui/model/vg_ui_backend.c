#include "vg_ui_backend.h"
#include "lvgl/lvgl.h"
#include <stdio.h>
#include <string.h>

static int mock_discover_scan_start(int addr_min, int addr_max)
{
    (void)addr_min;
    (void)addr_max;
    return 0;
}

static int mock_discover_scan_status(void)
{
    return 2;
}

static int mock_get_slaves(vg_ui_slave_t *out, int max)
{
    static const uint8_t addrs[] = {1, 3, 4, 5, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
    int i;
    int n = (int)(sizeof(addrs) / sizeof(addrs[0]));

    if(out == NULL || max <= 0) {
        return 0;
    }

    if(n > max) {
        n = max;
    }

    for(i = 0; i < n; i++) {
        out[i].addr = addrs[i];
        out[i].probe_reg = (addrs[i] == 2) ? 2 : 0;
        lv_snprintf(out[i].label, sizeof(out[i].label), "addr=%u", (unsigned)addrs[i]);
    }

    return n;
}

static int mock_discover_apply_start(void)
{
    return 0;
}

static int mock_discover_apply_status(void)
{
    return 2;
}

static int mock_read_report(char *body, size_t body_sz, char *path, size_t path_sz)
{
    static const char *mock_body =
        "2026-08-30 08:00 运营日报\n"
        "\n"
        "告警: 2 (WARNING 1 / OFFLINE 1)\n"
        "RS485: 14/32 从站在线 (mock)\n"
        "最高温: 82.4 C @ 从站 3\n";

    if(path != NULL && path_sz > 0) {
        lv_snprintf(path, path_sz, "/data/velaguard/reports/daily-20260830.md");
    }
    if(body != NULL && body_sz > 0) {
        lv_snprintf(body, body_sz, "%s", mock_body);
    }
    return 0;
}

static const vg_ui_backend_t s_mock_backend = {
    .discover_scan_start  = mock_discover_scan_start,
    .discover_scan_status = mock_discover_scan_status,
    .discover_apply_start = mock_discover_apply_start,
    .discover_apply_status = mock_discover_apply_status,
    .get_slaves           = mock_get_slaves,
    .read_latest_report   = mock_read_report,
};

const vg_ui_backend_t *vg_ui_backend_get(void)
{
    return &s_mock_backend;
}

int vg_ui_backend_scan_last_result(void)
{
    return 0;
}

int vg_ui_backend_apply_last_result(void)
{
    return 0;
}

void vg_ui_backend_scan_progress(int *cur_addr, int *addr_max)
{
    if(cur_addr != NULL) {
        *cur_addr = 0;
    }
    if(addr_max != NULL) {
        *addr_max = 32;
    }
}

void vg_ui_backend_acq_start(void)
{
}

bool vg_ui_backend_apply_live(void)
{
    return false;
}

void vg_ui_backend_boot_points(void)
{
}
