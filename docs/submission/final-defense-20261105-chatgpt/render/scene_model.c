/* Presentation-only deterministic snapshots over the real UI model.
 * This translation unit replaces the PC simulator model object at link time.
 * Firmware sources and firmware build flags are unchanged.
 */
#define vg_model_tick vg_model_tick_simulator
#include "../../../../gui/main/ui/model/vg_model.c"
#undef vg_model_tick
#include "vg_alarm_eval.h"

void vg_model_tick(void) { /* Samples are explicitly advanced by the scene. */ }

void scene_sample(int idx, float value, bool online)
{
    vg_sensor_t *s=&s_sensors[idx];
    struct vg_alarm_rule r={0};
    enum vg_alarm_kind k;
    float thr=0;
    /* The current UI truncates decimals. Keep the rendered one-decimal
     * fixture stable despite binary float representation at the boundary. */
    if(value>0)value+=0.0001f;
    vg_model_set_live((uint16_t)idx,value,online);
    strncpy(r.cmp,s->cmp,sizeof(r.cmp)-1);
    r.has_warn=s->has_warn; r.has_crit=s->has_crit;
    r.warn=s->thr_warn; r.crit=s->thr_crit; r.fail_n=s->fail_n;
    k=vg_alarm_sample(&r,&s->fail_window,online,value,&thr);
    s->online=online;
    s->quality_pct=online?100:0;
    s->severity=k==VG_ALARM_KIND_CRIT?VG_SEV_CRIT:
                k==VG_ALARM_KIND_WARN?VG_SEV_WARN:
                k==VG_ALARM_KIND_OFFLINE?VG_SEV_OFFLINE:VG_SEV_OK;
    if(k!=VG_ALARM_KIND_NONE) sensor_alarm_begin(s,s->severity);
    else if(s->al_active) sensor_alarm_end(s);
    derive_primary_alarm(); rebuild_filter(); notify_all();
}

void scene_network(bool online)
{
    s_net.net_ok=online; s_net.wifi_ok=false;
    s_net.mimo_ok=online; s_net.llm_ok=online;
    s_net.acq_ok=true; s_net.aud_ok=true;
    strncpy(s_net.ip,online?"192.0.2.20":"",sizeof(s_net.ip)-1);
    notify_all();
}

void scene_refresh(void) { derive_primary_alarm(); rebuild_filter(); notify_all(); }

/* One fixed, explicitly synthetic wall clock for reproducible UI frames. */
time_t time(time_t *out)
{
    time_t now=1793865600; /* 2026-11-05 08:00 UTC, scenario clock. */
    if(out)*out=now;
    return now;
}
