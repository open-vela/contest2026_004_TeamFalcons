/* Reuse the real PC backend, substituting only the report snapshot. */
#define vg_ui_backend_get scene_default_backend_get
#define vg_ui_report_request scene_default_report_request
#define vg_ui_report_snapshot scene_default_report_snapshot
#include "../../../../gui/main/ui/model/vg_ui_backend.c"
#undef vg_ui_backend_get
#undef vg_ui_report_request
#undef vg_ui_report_snapshot

static uint32_t s_scene_request;
int vg_ui_report_request(bool allow_generate,uint32_t *id)
{
    (void)allow_generate;
    s_scene_request++;
    if(id)*id=s_scene_request;
    return 0;
}
bool vg_ui_report_snapshot(vg_ui_report_snapshot_t *out)
{
    memset(out,0,sizeof(*out));
    out->request_id=s_scene_request;out->version=s_scene_request;
    out->status=VG_UI_REPORT_READY;out->from_agent=false;
    snprintf(out->path,sizeof(out->path),"/scene/runtime-report.txt");
    snprintf(out->body,sizeof(out->body),
       "设备间巡检记录\n\n"
       "点位在线 6/6\n温度当前 29.0 C，最高 36.2 C\n"
       "温度曾超过预警 30.0 C、严重 35.0 C\n"
       "温度告警已恢复，当前活动告警 0\n"
       "A相电压 224.6 V，电流 8.2 A\nUPS负载 42%%，水浸 0\n"
       "交接：核对通风与现场处理记录。\n");
    return true;
}
const vg_ui_backend_t *vg_ui_backend_get(void)
{
    static vg_ui_backend_t b;
    b=*scene_default_backend_get();
    b.report_request=vg_ui_report_request;b.report_snapshot=vg_ui_report_snapshot;
    return &b;
}
