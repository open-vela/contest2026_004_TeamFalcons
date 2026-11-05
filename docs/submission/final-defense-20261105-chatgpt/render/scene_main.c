/* Reproducible defense screenshots, entirely synthetic operating data. */
#include "harness_common.h"
#include "model/vg_model.h"
#include "model/vg_ui_backend.h"
#include "shell/vg_shell.h"
#include "widgets/vg_voice_overlay.h"
#include "vg_voice_round.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void scene_sample(int idx,float value,bool online);
void scene_network(bool online);
void scene_refresh(void);
static struct vg_voice_ui voice;
bool vg_voice_round_ui(struct vg_voice_ui *out){*out=voice;return true;}
void vg_voice_round_end(void){voice.visible=false;}
void vg_voice_round_retry_tts(void){}
bool vg_voice_round_muted(void){return false;}
void vg_voice_pref_set_muted(bool muted){(void)muted;}
void vg_voice_round_hold(bool held){(void)held;}

static void frame(const char *out,const char *name,vg_page_id_t page)
{
    vg_nav_goto(page,NULL);hg_pump(16);scene_refresh();hg_pump(4);
    hg_dump_ppm(out,name);hg_expect_not_blank(name);
}
int main(int argc,char **argv)
{
    const char *out=argc>1?argv[1]:".";
    const char *ids[]={"temp_room","humidity_room","volt_a","curr_a","ups_load","leak"};
    const char *names[]={"设备间温度","设备间湿度","A相电压","A相电流","UPS负载","水浸状态"};
    const char *units[]={"C","%","V","A","%",""};
    const float values[]={28.4f,54.7f,224.6f,8.2f,42,0};
    vg_runtime_point_t pts[6]={0};int i,j;
    setenv("TZ","UTC",1);hg_init();
    for(i=0;i<6;i++){
        snprintf(pts[i].id,sizeof(pts[i].id),"%s",ids[i]);
        snprintf(pts[i].name,sizeof(pts[i].name),"%s",names[i]);
        snprintf(pts[i].unit,sizeof(pts[i].unit),"%s",units[i]);
        pts[i].addr=i<2?1:i<4?2:i==4?3:4;pts[i].fc=3;pts[i].reg=i;
        strcpy(pts[i].dtype,"uint16");pts[i].scale=1;pts[i].fail_n=5;
    }
    strcpy(pts[0].cmp,"ge");pts[0].has_warn=pts[0].has_crit=1;pts[0].warn=30;pts[0].crit=35;
    strcpy(pts[2].cmp,"le");pts[2].has_warn=pts[2].has_crit=1;pts[2].warn=200;pts[2].crit=180;
    strcpy(pts[4].cmp,"ge");pts[4].has_warn=pts[4].has_crit=1;pts[4].warn=80;pts[4].crit=90;
    vg_model_import_runtime_points(pts,6);scene_network(true);
    for(j=0;j<24;j++)for(i=0;i<6;i++)scene_sample(i,values[i],true);
    hg_expect(vg_model_active_alarm_count()==0,"normal: no alarm");
    frame(out,"01_home_normal",VG_PAGE_HOME);
    for(j=0;j<18;j++)scene_sample(0,28.4f+(32.6f-28.4f)*(j+1)/18,true);
    hg_expect(vg_model_active_alarm_count()==1,"warning: exactly one alarm");
    frame(out,"02_alarm_warning",VG_PAGE_ALARM);
    for(j=0;j<18;j++)scene_sample(0,32.6f+(36.2f-32.6f)*(j+1)/18,true);
    hg_expect(vg_model_get_active_alarm()->severity==VG_SEV_CRIT,"critical: real evaluator");
    frame(out,"03_alarm_critical",VG_PAGE_ALARM);
    vg_model_set_selected_sensor(ids[0]);
    frame(out,"04_device_critical",VG_PAGE_DEVICE);
    frame(out,"05_trend_peak",VG_PAGE_TREND);
    {
        const vg_sensor_t *s=vg_model_get_selected_sensor();
        vg_ai_advice_entry_t a={0};
        snprintf(a.id,sizeof(a.id),"%s",ids[0]);a.epoch=s->al_epoch;
        snprintf(a.sum,sizeof(a.sum),"设备间温度超过严重阈值");
        snprintf(a.ev,sizeof(a.ev),"当前36.2 C，严重阈值35.0 C，点位在线");
        snprintf(a.att,sizeof(a.att),"现场核对通风、散热和环境变化");
        vg_ui_backend_mock_set_advice(&a,1);
        frame(out,"06_alarm_advice",VG_PAGE_ALARM);
    }
    scene_network(false);scene_sample(0,36.1f,true);
    hg_expect(vg_model_get_net()->acq_ok && !vg_model_get_net()->net_ok,"network down, acquisition stays up");
    frame(out,"07_network_down",VG_PAGE_HOME);
    frame(out,"08_trend_network_down",VG_PAGE_TREND);
    scene_network(true);
    for(j=0;j<8;j++)scene_sample(2,values[2],false);
    frame(out,"09_instrument_offline",VG_PAGE_HOME);
    for(j=0;j<8;j++)scene_sample(2,values[2],true);
    voice.visible=true;voice.state=0;voice.generation=1;voice.result_left_ms=15000;
    snprintf(voice.status,sizeof(voice.status),"回答完成");
    snprintf(voice.question,sizeof(voice.question),"温度异常，电压和负载正常吗？");
    snprintf(voice.answer,sizeof(voice.answer),"AI 回答：温度36.1 C，超过严重阈值35 C。A相电压224.6 V，UPS负载42%%，均在线。请现场核对通风和散热情况。");
    vg_voice_overlay_create();
    frame(out,"10_voice_answer",VG_PAGE_HOME);
    voice.visible=false;vg_voice_overlay_destroy();
    for(j=0;j<24;j++)scene_sample(0,36.1f-(36.1f-29.0f)*(j+1)/24,true);
    hg_expect(vg_model_active_alarm_count()==0,"recovery: alarm cleared");
    frame(out,"11_home_recovered",VG_PAGE_HOME);
    vg_model_set_selected_sensor(ids[0]);frame(out,"12_trend_recovery",VG_PAGE_TREND);
    frame(out,"14_handover_report",VG_PAGE_REPORT);
    printf("scene: 6 points, warning=1, critical=1, recovered=0, failures=%d\n",hg_failures());
    return hg_failures()?1:0;
}
