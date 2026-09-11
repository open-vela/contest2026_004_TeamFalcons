#include "vg_model.h"
#include "vg_ui_backend.h"
#include "vg_mthings_points.h"
#include "lvgl/lvgl.h"
#ifdef VG_HMI_BOARD
#include "vg_alarm_eval.h"
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include <strings.h>
#include <time.h>

#define VG_MAX_LISTENERS 8
#define VG_DIAG_LOAD_MS 500
#define VG_ADD_TEST_MS 600
#define VG_OTA_STEP_MS 400
#define VG_OTA_STEP_PCT 5
#define VG_MOCK_SEED 24

typedef struct {
    vg_model_change_cb_t cb;
    void * user;
} vg_listener_t;

static vg_scenario_t s_scenario = VG_SCENARIO_NORMAL;
static vg_sensor_t s_sensors[VG_SENSOR_MAX];
static uint16_t s_sensor_n;
static vg_alarm_t s_alarm;
static vg_net_status_t s_net;
static vg_diagnosis_t s_diag;
static vg_log_entry_t s_logs[VG_LOG_MAX];
static uint16_t s_log_n;
static vg_listener_t s_listeners[VG_MAX_LISTENERS];
static int s_listener_n;
static uint32_t s_tick;
static lv_timer_t * s_diag_tmr;
static vg_add_sensor_state_t s_add_state = VG_ADD_IDLE;
static vg_sensor_candidate_t s_candidate;
static char s_add_test_msg[80];
static float s_add_test_value;
static int32_t s_add_test_quality;
static lv_timer_t * s_add_tmr;
static vg_ota_t s_ota;
static lv_timer_t * s_ota_tmr;
static vg_sys_status_t s_sys;
static vg_home_filter_t s_home_filter = VG_HOME_FILTER_ALL;
static char s_selected_id[VG_SENSOR_ID_MAX];

static void cancel_add_timer(void);
static void cancel_ota_timer(void);
static void reset_add_state(void);
static void reset_ota_state(void);

/* Filtered index list for home (indices into s_sensors) */
static uint16_t s_filt_idx[VG_SENSOR_MAX];
static uint16_t s_filt_n;

static void notify_all(void)
{
    int i;
    for(i = 0; i < s_listener_n; i++) {
        if(s_listeners[i].cb) s_listeners[i].cb(s_listeners[i].user);
    }
}

static void fill_time_now(char * buf, size_t n)
{
    time_t now;
    struct tm * tm_info;
    time(&now);
    tm_info = localtime(&now);
    if(tm_info) lv_snprintf(buf, n, "%02d:%02d", tm_info->tm_hour, tm_info->tm_min);
    else lv_snprintf(buf, n, "--:--");
}

void vg_model_append_log(vg_log_type_t t, vg_severity_t sev, const char * text)
{
    vg_log_entry_t * e;
    if(s_log_n >= VG_LOG_MAX) {
        memmove(&s_logs[0], &s_logs[1], sizeof(s_logs[0]) * (VG_LOG_MAX - 1));
        s_log_n = VG_LOG_MAX - 1;
    }
    e = &s_logs[s_log_n++];
    memset(e, 0, sizeof(*e));
    e->type = t;
    e->severity = sev;
    fill_time_now(e->time, sizeof(e->time));
    if(text) strncpy(e->text, text, sizeof(e->text) - 1);
}

static float approx_sin(float t)
{
    int phase = ((int)(t * 10.0f) % 62 + 62) % 62;
    if(phase < 16) return (float)phase / 16.0f;
    if(phase < 31) return (float)(31 - phase) / 15.0f;
    if(phase < 47) return -(float)(phase - 31) / 16.0f;
    return -(float)(62 - phase) / 15.0f;
}

static void fill_sensor_history(vg_sensor_t * s, float base, float amp, float noise)
{
    int i;
    if(s == NULL) return;
    s->history_len = VG_HISTORY_LEN;
    for(i = 0; i < VG_HISTORY_LEN; i++) {
        float t = (float)i / 8.0f;
        float n = ((float)((i * 17 + (int)s_tick * 3 + (int)s->reg_addr) % 11) - 5.0f) * noise;
        s->history[i] = base + amp * approx_sin(t) + n;
    }
    s->value = s->history[VG_HISTORY_LEN - 1];
}

static int sev_rank(vg_severity_t sev)
{
    switch(sev) {
        case VG_SEV_CRIT: return 4;
        case VG_SEV_WARN: return 3;
        case VG_SEV_OFFLINE: return 2;
        case VG_SEV_INFO: return 1;
        default: return 0;
    }
}

static void rebuild_filter(void)
{
    uint16_t i, j;
    s_filt_n = 0;
    for(i = 0; i < s_sensor_n; i++) {
        const vg_sensor_t * s = &s_sensors[i];
        bool pass = false;
        switch(s_home_filter) {
            case VG_HOME_FILTER_ALARM:
                pass = (s->severity == VG_SEV_WARN || s->severity == VG_SEV_CRIT);
                break;
            case VG_HOME_FILTER_OFFLINE:
                pass = (!s->online || s->severity == VG_SEV_OFFLINE);
                break;
            case VG_HOME_FILTER_OK:
                pass = (s->online && s->severity == VG_SEV_OK);
                break;
            case VG_HOME_FILTER_ALL:
            default:
                pass = true;
                break;
        }
        if(pass) s_filt_idx[s_filt_n++] = i;
    }
    /* severity desc, then name asc — simple insertion sort on indices */
    for(i = 1; i < s_filt_n; i++) {
        uint16_t key = s_filt_idx[i];
        j = i;
        while(j > 0) {
            const vg_sensor_t * a = &s_sensors[s_filt_idx[j - 1]];
            const vg_sensor_t * b = &s_sensors[key];
            int ra = sev_rank(a->severity);
            int rb = sev_rank(b->severity);
            if(ra > rb) break;
            if(ra == rb && strcmp(a->name, b->name) <= 0) break;
            s_filt_idx[j] = s_filt_idx[j - 1];
            j--;
        }
        s_filt_idx[j] = key;
    }
}

static void seed_fleet(void)
{
#ifdef VG_HMI_BOARD
    s_sensor_n = 0;
    s_selected_id[0] = '\0';
    return;
#endif
    /* 24 mock sensors — structure scales to VG_SENSOR_MAX without UI change */
    static const char * units[] = {"C", "%", "kPa", "mm/s", "A", "V"};
    static const char * kinds[] = {"温度", "湿度", "压力", "振动", "电流", "电压"};
    uint16_t i;
    s_sensor_n = VG_MOCK_SEED;
    if(s_sensor_n > VG_SENSOR_MAX) s_sensor_n = VG_SENSOR_MAX;

    for(i = 0; i < s_sensor_n; i++) {
        vg_sensor_t * s = &s_sensors[i];
        int kind = (int)(i % 6);
        memset(s, 0, sizeof(*s));
        lv_snprintf(s->id, sizeof(s->id), "s_%02u", (unsigned)(i + 1));
        lv_snprintf(s->name, sizeof(s->name), "%s-%02u", kinds[kind], (unsigned)(i / 6 + 1));
        strncpy(s->type, kinds[kind], sizeof(s->type) - 1);
        strncpy(s->unit, units[kind], sizeof(s->unit) - 1);
        strncpy(s->formula, "R0", sizeof(s->formula) - 1);
        s->function_code = 3;
        s->length = 1;
        s->data_format = VG_SENSOR_FMT_UINT16;
        s->word_order = VG_SENSOR_ORDER_ABCD;
        s->period_ms = 1000;
        s->reg_addr = (int32_t)(0x100 + i);
        s->slave_addr = 1;
        s->quality_pct = 90 + (int)(i % 10);
        s->online = true;
        s->age_sec = 1 + (int)(i % 5);
        s->severity = VG_SEV_OK;
        switch(kind) {
            case 0: s->thr_low = 5; s->thr_warn = 55; s->thr_crit = 70;
                s->base_value = 34.0f + (float)(i % 5);
                fill_sensor_history(s, s->base_value, 0.6f, 0.08f); break;
            case 1: s->thr_low = 10; s->thr_warn = 75; s->thr_crit = 90;
                s->base_value = 45.0f + (float)(i % 8);
                fill_sensor_history(s, s->base_value, 1.0f, 0.1f); break;
            case 2: s->thr_low = 50; s->thr_warn = 180; s->thr_crit = 220;
                s->base_value = 110.0f + (float)(i % 10);
                fill_sensor_history(s, s->base_value, 2.0f, 0.2f); break;
            case 3: s->thr_low = 0; s->thr_warn = 4.5f; s->thr_crit = 7.0f;
                s->base_value = 1.5f + 0.1f * (float)(i % 4);
                fill_sensor_history(s, s->base_value, 0.15f, 0.04f); break;
            case 4: s->thr_low = 0; s->thr_warn = 12; s->thr_crit = 18;
                s->base_value = 4.0f + 0.2f * (float)(i % 5);
                fill_sensor_history(s, s->base_value, 0.2f, 0.05f); break;
            default: s->thr_low = 180; s->thr_warn = 250; s->thr_crit = 280;
                s->base_value = 220.0f + (float)(i % 6);
                fill_sensor_history(s, s->base_value, 1.5f, 0.1f); break;
        }
    }
    if(s_selected_id[0] == '\0') {
        strncpy(s_selected_id, s_sensors[0].id, sizeof(s_selected_id) - 1);
    }
}

static void clear_diagnosis(void)
{
    memset(&s_diag, 0, sizeof(s_diag));
    s_diag.state = VG_DIAG_IDLE;
}

static void cancel_diag_timer(void)
{
    if(s_diag_tmr) {
        lv_timer_delete(s_diag_tmr);
        s_diag_tmr = NULL;
    }
}

static void settle_diagnosis(void)
{
    clear_diagnosis();
    if(s_scenario == VG_SCENARIO_AI_DOWN || !s_net.mimo_ok) {
        s_diag.state = VG_DIAG_ERROR;
        strncpy(s_diag.error_msg, "MiMo 不可用", sizeof(s_diag.error_msg) - 1);
        strncpy(s_diag.alarm_title, s_alarm.active ? s_alarm.title : "无活动告警",
                sizeof(s_diag.alarm_title) - 1);
        return;
    }
    if(s_scenario == VG_SCENARIO_WARN) {
        s_diag.state = VG_DIAG_OK;
        strncpy(s_diag.alarm_title, s_alarm.title, sizeof(s_diag.alarm_title) - 1);
        strncpy(s_diag.summary, "温度接近预警阈值，建议关注散热与负载变化。", sizeof(s_diag.summary) - 1);
        strncpy(s_diag.risk, "medium", sizeof(s_diag.risk) - 1);
        strncpy(s_diag.causes[0], "环境温度升高", sizeof(s_diag.causes[0]) - 1);
        strncpy(s_diag.causes[1], "散热风扇效率下降", sizeof(s_diag.causes[1]) - 1);
        strncpy(s_diag.causes[2], "负载短时偏高", sizeof(s_diag.causes[2]) - 1);
        s_diag.cause_n = 3;
        strncpy(s_diag.actions[0], "检查散热通道", sizeof(s_diag.actions[0]) - 1);
        strncpy(s_diag.actions[1], "降低非关键负载", sizeof(s_diag.actions[1]) - 1);
        strncpy(s_diag.actions[2], "持续观察 10 分钟", sizeof(s_diag.actions[2]) - 1);
        s_diag.action_n = 3;
        s_diag.confidence_pct = 82;
        return;
    }
    if(s_scenario == VG_SCENARIO_CRIT) {
        s_diag.state = VG_DIAG_OK;
        strncpy(s_diag.alarm_title, s_alarm.title, sizeof(s_diag.alarm_title) - 1);
        strncpy(s_diag.summary, "温度已超严重阈值，存在过热风险，请立即处理。", sizeof(s_diag.summary) - 1);
        strncpy(s_diag.risk, "high", sizeof(s_diag.risk) - 1);
        strncpy(s_diag.causes[0], "冷却系统异常", sizeof(s_diag.causes[0]) - 1);
        strncpy(s_diag.causes[1], "传感器附近热源", sizeof(s_diag.causes[1]) - 1);
        strncpy(s_diag.causes[2], "阈值配置可能偏紧", sizeof(s_diag.causes[2]) - 1);
        s_diag.cause_n = 3;
        strncpy(s_diag.actions[0], "现场检查冷却", sizeof(s_diag.actions[0]) - 1);
        strncpy(s_diag.actions[1], "必要时停机保护", sizeof(s_diag.actions[1]) - 1);
        strncpy(s_diag.actions[2], "核对阈值与标定", sizeof(s_diag.actions[2]) - 1);
        s_diag.action_n = 3;
        s_diag.confidence_pct = 91;
        return;
    }
    if(s_scenario == VG_SCENARIO_OFFLINE) {
        s_diag.state = VG_DIAG_OK;
        strncpy(s_diag.alarm_title, s_alarm.title, sizeof(s_diag.alarm_title) - 1);
        strncpy(s_diag.summary, "传感器通信中断，采集链路可能异常。", sizeof(s_diag.summary) - 1);
        strncpy(s_diag.risk, "high", sizeof(s_diag.risk) - 1);
        strncpy(s_diag.causes[0], "传感器供电异常", sizeof(s_diag.causes[0]) - 1);
        strncpy(s_diag.causes[1], "RS485/线缆松动", sizeof(s_diag.causes[1]) - 1);
        strncpy(s_diag.causes[2], "采集模块故障", sizeof(s_diag.causes[2]) - 1);
        s_diag.cause_n = 3;
        strncpy(s_diag.actions[0], "检查供电与接线", sizeof(s_diag.actions[0]) - 1);
        strncpy(s_diag.actions[1], "重启采集通道", sizeof(s_diag.actions[1]) - 1);
        strncpy(s_diag.actions[2], "更换备用传感器", sizeof(s_diag.actions[2]) - 1);
        s_diag.action_n = 3;
        s_diag.confidence_pct = 88;
        return;
    }
    s_diag.state = VG_DIAG_OK;
    strncpy(s_diag.alarm_title, "无活动告警", sizeof(s_diag.alarm_title) - 1);
    strncpy(s_diag.summary, "当前无活动告警，系统运行正常。", sizeof(s_diag.summary) - 1);
    strncpy(s_diag.risk, "low", sizeof(s_diag.risk) - 1);
    strncpy(s_diag.causes[0], "无异常指标", sizeof(s_diag.causes[0]) - 1);
    s_diag.cause_n = 1;
    strncpy(s_diag.actions[0], "保持常规巡检", sizeof(s_diag.actions[0]) - 1);
    s_diag.action_n = 1;
    s_diag.confidence_pct = 70;
}

static void diag_timer_cb(lv_timer_t * t)
{
    LV_UNUSED(t);
    s_diag_tmr = NULL;
    settle_diagnosis();
    if(s_diag.state == VG_DIAG_OK)
        vg_model_append_log(VG_LOG_DIAG, VG_SEV_INFO, "AI 诊断完成");
    else if(s_diag.state == VG_DIAG_ERROR)
        vg_model_append_log(VG_LOG_DIAG, VG_SEV_WARN, "AI 诊断失败: MiMo 不可用");
    notify_all();
}

/* ------------------------------------------------------------------ */
/* C3: 添加传感器 / 系统状态 / OTA                                     */
/* ------------------------------------------------------------------ */

typedef struct {
    const char * cn;
    const char * alias;
    const char * alias2;
    const char * unit;
    float thr_low;
    float thr_warn;
    float thr_crit;
} vg_kind_t;

static const vg_kind_t s_kinds[] = {
    {"温度", "temp", "temperature", "C",     5.0f,   55.0f,  70.0f },
    {"湿度", "hum",  "humidity",    "%",     10.0f,  75.0f,  90.0f },
    {"压力", "press","pressure",    "kPa",   50.0f,  180.0f, 220.0f},
    {"振动", "vib",  "vibration",   "mm/s",  0.0f,   4.5f,   7.0f  },
    {"电流", "cur",  "current",     "A",     0.0f,   12.0f,  18.0f },
    {"电压", "volt", "voltage",     "V",     180.0f, 250.0f, 280.0f}
};

static vg_sensor_type_template_t s_custom_types[VG_CUSTOM_TYPE_MAX];
static uint8_t s_custom_type_n;
static uint32_t s_id_seed = VG_MOCK_SEED * 2654435761u;

typedef struct {
    const char * p;
    float raw;
    bool used_r0;
    bool error;
} formula_parser_t;

static void formula_skip_space(formula_parser_t * parser)
{
    while(parser->p && (*parser->p == ' ' || *parser->p == '\t')) parser->p++;
}

static float formula_parse_expr(formula_parser_t * parser);

static float formula_parse_factor(formula_parser_t * parser)
{
    char * end;
    float value;
    int sign = 1;

    formula_skip_space(parser);
    if(*parser->p == '+' || *parser->p == '-') {
        if(*parser->p == '-') sign = -1;
        parser->p++;
        formula_skip_space(parser);
    }
    if(strncasecmp(parser->p, "R0", 2) == 0 &&
       (parser->p[2] == '\0' || !isalnum((unsigned char)parser->p[2]))) {
        parser->p += 2;
        parser->used_r0 = true;
        return (float)sign * parser->raw;
    }
    if(*parser->p == '(') {
        parser->p++;
        value = formula_parse_expr(parser);
        formula_skip_space(parser);
        if(*parser->p != ')') parser->error = true;
        else parser->p++;
        return (float)sign * value;
    }
    value = strtof(parser->p, &end);
    if(end == parser->p) {
        parser->error = true;
        return 0.0f;
    }
    parser->p = end;
    return (float)sign * value;
}

static float formula_parse_term(formula_parser_t * parser)
{
    float value = formula_parse_factor(parser);
    while(!parser->error) {
        float rhs;
        char op;
        formula_skip_space(parser);
        op = *parser->p;
        if(op != '*' && op != '/') break;
        parser->p++;
        rhs = formula_parse_factor(parser);
        if(op == '*') value *= rhs;
        else if(rhs > -0.000001f && rhs < 0.000001f) parser->error = true;
        else value /= rhs;
    }
    return value;
}

static float formula_parse_expr(formula_parser_t * parser)
{
    float value = formula_parse_term(parser);
    while(!parser->error) {
        float rhs;
        char op;
        formula_skip_space(parser);
        op = *parser->p;
        if(op != '+' && op != '-') break;
        parser->p++;
        rhs = formula_parse_term(parser);
        if(op == '+') value += rhs;
        else value -= rhs;
    }
    return value;
}

static bool evaluate_formula(const char * formula, float raw, float * out)
{
    formula_parser_t parser;
    float value;
    if(formula == NULL || formula[0] == '\0' || out == NULL) return false;
    parser.p = formula;
    parser.raw = raw;
    parser.used_r0 = false;
    parser.error = false;
    value = formula_parse_expr(&parser);
    formula_skip_space(&parser);
    if(parser.error || !parser.used_r0 || *parser.p != '\0' || !isfinite(value)) return false;
    *out = value;
    return true;
}

static bool valid_formula(const char * formula)
{
    float value;
    return evaluate_formula(formula, 1.0f, &value) &&
           evaluate_formula(formula, 32.0f, &value);
}

static bool valid_template_text(const char * text, size_t max_len)
{
    size_t i;
    if(text == NULL || text[0] == '\0' || strlen(text) >= max_len) return false;
    for(i = 0; text[i] != '\0'; i++) {
        if(text[i] == '|' || text[i] == '\r' || text[i] == '\n') return false;
    }
    return true;
}

static uint32_t next_id_random(void)
{
    s_id_seed = s_id_seed * 1664525u + 1013904223u;
    return s_id_seed;
}

bool vg_model_sensor_id_is_unique(const char * id)
{
    uint16_t i;
    if(id == NULL || id[0] == '\0') return false;
    for(i = 0; i < s_sensor_n; i++) {
        if(strcmp(s_sensors[i].id, id) == 0) return false;
    }
    return true;
}

void vg_model_generate_sensor_id(char * out, size_t n)
{
    int attempt;
    unsigned fallback;
    if(out == NULL || n == 0) return;
    out[0] = '\0';
    for(attempt = 0; attempt < 32; attempt++) {
        lv_snprintf(out, n, "s_%06lX", (unsigned long)(next_id_random() & 0xFFFFFFu));
        if(vg_model_sensor_id_is_unique(out)) return;
    }
    /* Deterministic fallback must still be unique (B-P2-13) */
    fallback = (unsigned)(s_sensor_n + 1);
    for(attempt = 0; attempt < VG_SENSOR_MAX + 32; attempt++) {
        lv_snprintf(out, n, "s_%u", fallback);
        if(vg_model_sensor_id_is_unique(out)) return;
        fallback++;
    }
}

static int custom_type_index(const char * name)
{
    uint8_t i;
    if(name == NULL) return -1;
    for(i = 0; i < s_custom_type_n; i++) {
        if(s_custom_types[i].used && strcmp(s_custom_types[i].type, name) == 0) return (int)i;
    }
    return -1;
}

#if defined(_WIN32)
#define VG_CUSTOM_TYPE_STORE "vg_sensor_types.cfg"

static void load_custom_types(void)
{
    FILE * file;
    char line[256];
    memset(s_custom_types, 0, sizeof(s_custom_types));
    s_custom_type_n = 0;
    file = fopen(VG_CUSTOM_TYPE_STORE, "r");
    if(file == NULL) return;
    while(s_custom_type_n < VG_CUSTOM_TYPE_MAX && fgets(line, sizeof(line), file)) {
        vg_sensor_type_template_t type;
        unsigned format, order;
        memset(&type, 0, sizeof(type));
        if(sscanf(line, "%23[^|]|%11[^|]|%63[^|]|%u|%u|%f|%f|%f",
                  type.type, type.unit, type.formula, &format, &order,
                  &type.thr_low, &type.thr_warn, &type.thr_crit) != 8) continue;
        type.used = true;
        type.data_format = (vg_sensor_data_format_t)format;
        type.word_order = (vg_sensor_word_order_t)order;
        if(!valid_template_text(type.type, sizeof(type.type)) ||
           !valid_template_text(type.unit, sizeof(type.unit)) ||
           type.data_format > VG_SENSOR_FMT_FLOAT32 || type.word_order > VG_SENSOR_ORDER_CDAB ||
           !valid_formula(type.formula) || !isfinite(type.thr_low) ||
           !isfinite(type.thr_warn) || !isfinite(type.thr_crit) ||
           type.thr_low >= type.thr_warn || type.thr_warn >= type.thr_crit) continue;
        s_custom_types[s_custom_type_n++] = type;
    }
    fclose(file);
}

static void persist_custom_types(void)
{
    FILE * file;
    uint8_t i;
    file = fopen(VG_CUSTOM_TYPE_STORE, "w");
    if(file == NULL) return;
    for(i = 0; i < s_custom_type_n; i++) {
        const vg_sensor_type_template_t * type = &s_custom_types[i];
        fprintf(file, "%s|%s|%s|%u|%u|%.9g|%.9g|%.9g\n",
                type->type, type->unit, type->formula,
                (unsigned)type->data_format, (unsigned)type->word_order,
                type->thr_low, type->thr_warn, type->thr_crit);
    }
    fclose(file);
}
#else
static void load_custom_types(void) { }
static void persist_custom_types(void) { }
#endif

uint8_t vg_model_sensor_type_count(void)
{
    return (uint8_t)(sizeof(s_kinds) / sizeof(s_kinds[0]) + s_custom_type_n);
}

const vg_sensor_type_template_t * vg_model_sensor_type_at(uint8_t index)
{
    static vg_sensor_type_template_t builtin;
    size_t builtin_n = sizeof(s_kinds) / sizeof(s_kinds[0]);
    if(index < builtin_n) {
        const vg_kind_t * k = &s_kinds[index];
        memset(&builtin, 0, sizeof(builtin));
        builtin.used = true;
        strncpy(builtin.type, k->cn, sizeof(builtin.type) - 1);
        strncpy(builtin.unit, k->unit, sizeof(builtin.unit) - 1);
        strncpy(builtin.formula, "R0", sizeof(builtin.formula) - 1);
        builtin.data_format = VG_SENSOR_FMT_UINT16;
        builtin.word_order = VG_SENSOR_ORDER_ABCD;
        builtin.thr_low = k->thr_low;
        builtin.thr_warn = k->thr_warn;
        builtin.thr_crit = k->thr_crit;
        return &builtin;
    }
    index = (uint8_t)(index - builtin_n);
    if(index >= s_custom_type_n) return NULL;
    return &s_custom_types[index];
}

bool vg_model_save_sensor_type(const vg_sensor_type_template_t * type)
{
    int index;
    if(type == NULL || !type->used ||
       !valid_template_text(type->type, sizeof(type->type)) ||
       !valid_template_text(type->unit, sizeof(type->unit)) ||
       !valid_formula(type->formula) || !isfinite(type->thr_low) ||
       !isfinite(type->thr_warn) || !isfinite(type->thr_crit) ||
       type->thr_low >= type->thr_warn || type->thr_warn >= type->thr_crit) return false;
    index = custom_type_index(type->type);
    if(index < 0) {
        if(s_custom_type_n >= VG_CUSTOM_TYPE_MAX) return false;
        index = s_custom_type_n++;
    }
    s_custom_types[index] = *type;
    s_custom_types[index].used = true;
    persist_custom_types();
    notify_all();
    return true;
}

static void set_candidate_type_defaults(vg_sensor_candidate_t * candidate,
                                        const vg_kind_t * kind)
{
    if(candidate == NULL || kind == NULL) return;
    strncpy(candidate->type, kind->cn, sizeof(candidate->type) - 1);
    strncpy(candidate->unit, kind->unit, sizeof(candidate->unit) - 1);
    strncpy(candidate->formula, "R0", sizeof(candidate->formula) - 1);
    candidate->data_format = VG_SENSOR_FMT_UINT16;
    candidate->word_order = VG_SENSOR_ORDER_ABCD;
    candidate->thr_low = kind->thr_low;
    candidate->thr_warn = kind->thr_warn;
    candidate->thr_crit = kind->thr_crit;
}

static void seed_add_defaults(void)
{
    memset(&s_candidate, 0, sizeof(s_candidate));
    vg_model_generate_sensor_id(s_candidate.id, sizeof(s_candidate.id));
    strncpy(s_candidate.name, "新传感器", sizeof(s_candidate.name) - 1);
    set_candidate_type_defaults(&s_candidate, &s_kinds[0]);
    s_candidate.function_code = 3;
    strncpy(s_candidate.reg_addr_hex, "0100", sizeof(s_candidate.reg_addr_hex) - 1);
    s_candidate.reg_addr = 0x100;
    s_candidate.length = 1;
    s_candidate.period_ms = 1000;
    s_candidate.slave_addr = 1;
    strncpy(s_candidate.source, "builtin", sizeof(s_candidate.source) - 1);
}

static void cancel_add_timer(void)
{
    if(s_add_tmr) {
        lv_timer_delete(s_add_tmr);
        s_add_tmr = NULL;
    }
}

static void reset_add_state(void)
{
    cancel_add_timer();
    s_add_state = VG_ADD_IDLE;
    memset(&s_candidate, 0, sizeof(s_candidate));
    s_add_test_msg[0] = '\0';
    s_add_test_value = 0.0f;
    s_add_test_quality = 0;
}

static void cancel_ota_timer(void)
{
    if(s_ota_tmr) {
        lv_timer_delete(s_ota_tmr);
        s_ota_tmr = NULL;
    }
}

static void reset_ota_state(void)
{
    cancel_ota_timer();
    memset(&s_ota, 0, sizeof(s_ota));
    s_ota.state = VG_OTA_IDLE;
}

static bool parse_reg_hex(const char * text, int32_t * out)
{
    char * end;
    unsigned long value;
    size_t i;
    if(text == NULL || text[0] == '\0' || strlen(text) > 4) return false;
    for(i = 0; text[i] != '\0'; i++) {
        if(!isxdigit((unsigned char)text[i])) return false;
    }
    value = strtoul(text, &end, 16);
    if(end == text || *end != '\0' || value > 0xFFFFul) return false;
    if(out) *out = (int32_t)value;
    return true;
}

static bool valid_sensor_id(const char * id)
{
    size_t i;
    if(id == NULL || id[0] == '\0' || strlen(id) >= VG_SENSOR_ID_MAX) return false;
    for(i = 0; id[i] != '\0'; i++) {
        if(!isalnum((unsigned char)id[i]) && id[i] != '_' && id[i] != '-') return false;
    }
    return true;
}

static const char * add_validation_error(const vg_sensor_candidate_t * config,
                                         vg_sensor_candidate_t * normalized)
{
    int32_t address;
    if(config == NULL || normalized == NULL) return "配置不能为空";
    if(!valid_template_text(config->name, sizeof(config->name))) return "请输入有效的传感器名称";
    if(!valid_template_text(config->type, sizeof(config->type))) return "请选择或创建传感器类型";
    if(!valid_template_text(config->unit, sizeof(config->unit))) return "请输入有效单位";
    if(config->function_code != 3 && config->function_code != 4) return "功能码仅支持 03 或 04";
    if(config->slave_addr < 1 || config->slave_addr > 247) return "从站地址必须是 1–247";
    if(!parse_reg_hex(config->reg_addr_hex, &address)) return "寄存器地址必须是 1–4 位十六进制";
    if(config->length < 1 || config->length > 32) return "读取长度必须是 1–32 个寄存器";
    if((uint32_t)address + (uint32_t)config->length - 1u > 0xFFFFu) return "寄存器地址范围超出 FFFF";
    if((config->data_format == VG_SENSOR_FMT_UINT32 || config->data_format == VG_SENSOR_FMT_INT32 ||
        config->data_format == VG_SENSOR_FMT_FLOAT32) && config->length < 2) return "32 位数据长度至少为 2";
    if(config->period_ms < 100 || config->period_ms > 86400000) return "采样周期必须在 0.1–86400 秒";
    if(!isfinite(config->thr_low) || !isfinite(config->thr_warn) || !isfinite(config->thr_crit) ||
       config->thr_low >= config->thr_warn || config->thr_warn >= config->thr_crit) return "阈值必须满足 低限 < 预警 < 严重";
    if(!valid_formula(config->formula)) return "换算公式无效，仅支持 R0、四则运算和括号";
    if(config->data_format > VG_SENSOR_FMT_FLOAT32) return "数据格式无效";
    if(config->word_order > VG_SENSOR_ORDER_CDAB) return "字序无效";
    if(config->id[0] != '\0' && (!valid_sensor_id(config->id) || !vg_model_sensor_id_is_unique(config->id))) return "传感器 ID 非法或已存在";
    *normalized = *config;
    if(normalized->id[0] == '\0') vg_model_generate_sensor_id(normalized->id, sizeof(normalized->id));
    normalized->reg_addr = address;
    lv_snprintf(normalized->reg_addr_hex, sizeof(normalized->reg_addr_hex), "%04lX", (unsigned long)address);
    return NULL;
}

void vg_model_add_sensor_begin(void)
{
    reset_add_state();
    seed_add_defaults();
    notify_all();
}

bool vg_model_add_sensor_generate(const vg_sensor_candidate_t * config)
{
    vg_sensor_candidate_t normalized;
    const char * error = add_validation_error(config, &normalized);
    if(error != NULL) {
        reset_add_state();
        strncpy(s_add_test_msg, error, sizeof(s_add_test_msg) - 1);
        notify_all();
        return false;
    }
    reset_add_state();
    s_candidate = normalized;
    s_add_state = VG_ADD_PREVIEW;
    notify_all();
    return true;
}

static void add_test_timer_cb(lv_timer_t * t)
{
    float raw = 32.0f;
    LV_UNUSED(t);
    s_add_tmr = NULL;
    if(s_add_state != VG_ADD_TESTING) return;

    if(s_scenario == VG_SCENARIO_OFFLINE || !s_net.acq_ok) {
        s_add_state = VG_ADD_TEST_FAIL;
        s_add_test_quality = 0;
        strncpy(s_add_test_msg, "采集链路不可用", sizeof(s_add_test_msg) - 1);
    }
    else if(!evaluate_formula(s_candidate.formula, raw, &s_add_test_value)) {
        s_add_state = VG_ADD_TEST_FAIL;
        s_add_test_quality = 0;
        strncpy(s_add_test_msg, "换算公式计算失败", sizeof(s_add_test_msg) - 1);
    }
    else {
        s_add_test_quality = 92 + (int)(s_tick % 8);
        lv_snprintf(s_add_test_msg, sizeof(s_add_test_msg), "原始值 %.1f → %.1f %s",
                    raw, s_add_test_value, s_candidate.unit);
        s_add_state = VG_ADD_TEST_OK;
    }
    notify_all();
}

void vg_model_add_sensor_test(void)
{
    if(s_add_state != VG_ADD_PREVIEW && s_add_state != VG_ADD_TEST_FAIL) return;
    cancel_add_timer();
    s_add_state = VG_ADD_TESTING;
    s_add_test_msg[0] = '\0';
    notify_all();
    s_add_tmr = lv_timer_create(add_test_timer_cb, VG_ADD_TEST_MS, NULL);
    lv_timer_set_repeat_count(s_add_tmr, 1);
}

bool vg_model_add_sensor_confirm(void)
{
    vg_sensor_t * s;
    char logbuf[100];
    bool test_ok = s_add_state == VG_ADD_TEST_OK;
    if(!test_ok && s_add_state != VG_ADD_TEST_FAIL) return false;
    if(!vg_model_sensor_id_is_unique(s_candidate.id)) {
        vg_model_append_log(VG_LOG_UI, VG_SEV_WARN, "传感器 ID 已存在，无法添加");
        notify_all();
        return false;
    }
    if(s_sensor_n >= VG_SENSOR_MAX) {
        vg_model_append_log(VG_LOG_UI, VG_SEV_WARN, "传感器池已满，无法添加");
        notify_all();
        return false;
    }
    s = &s_sensors[s_sensor_n];
    memset(s, 0, sizeof(*s));
    strncpy(s->id, s_candidate.id, sizeof(s->id) - 1);
    strncpy(s->name, s_candidate.name, sizeof(s->name) - 1);
    strncpy(s->type, s_candidate.type, sizeof(s->type) - 1);
    strncpy(s->unit, s_candidate.unit, sizeof(s->unit) - 1);
    strncpy(s->formula, s_candidate.formula, sizeof(s->formula) - 1);
    s->function_code = s_candidate.function_code;
    s->length = s_candidate.length;
    s->data_format = s_candidate.data_format;
    s->word_order = s_candidate.word_order;
    s->slave_addr = s_candidate.slave_addr;
    s->base_value = test_ok ? s_add_test_value : 0.0f;
    s->thr_low = s_candidate.thr_low;
    s->thr_warn = s_candidate.thr_warn;
    s->thr_crit = s_candidate.thr_crit;
    s->reg_addr = s_candidate.reg_addr;
    s->period_ms = s_candidate.period_ms;
    s->quality_pct = test_ok ? s_add_test_quality : 0;
    s->online = test_ok;
    s->age_sec = 0;
    s->severity = test_ok ? VG_SEV_OK : VG_SEV_OFFLINE;
    s->value = test_ok ? s_add_test_value : 0.0f;
    /* Failed-test sensors persist offline: flat zero history, no mock wiggle */
    fill_sensor_history(s, s->base_value, test_ok ? 0.3f : 0.0f,
                        test_ok ? 0.05f : 0.0f);
    if(!test_ok) s->value = 0.0f;
    s_sensor_n++;
    if(test_ok) {
        lv_snprintf(logbuf, sizeof(logbuf), "已添加传感器: %s", s_candidate.name);
        vg_model_append_log(VG_LOG_UI, VG_SEV_INFO, logbuf);
    }
    else {
        lv_snprintf(logbuf, sizeof(logbuf), "已添加传感器（首次测试失败）: %s", s_candidate.name);
        vg_model_append_log(VG_LOG_UI, VG_SEV_WARN, logbuf);
    }
    rebuild_filter();
    reset_add_state();
    notify_all();
    return true;
}

void vg_model_add_sensor_abort(void)
{
    reset_add_state();
    notify_all();
}

vg_add_sensor_state_t vg_model_add_sensor_state(void)
{
    return s_add_state;
}

const vg_sensor_candidate_t * vg_model_get_candidate(void)
{
    return &s_candidate;
}

const char * vg_model_add_sensor_test_msg(void)
{
    return s_add_test_msg;
}

float vg_model_add_sensor_test_value(void)
{
    return s_add_test_value;
}

int32_t vg_model_add_sensor_test_quality(void)
{
    return s_add_test_quality;
}

static void ota_tick_cb(lv_timer_t * t)
{
    LV_UNUSED(t);
    if(s_ota.state != VG_OTA_PROGRESS) {
        cancel_ota_timer();
        return;
    }
    s_ota.progress_pct += VG_OTA_STEP_PCT;
    if(s_ota.progress_pct >= 100) {
        s_ota.progress_pct = 100;
        s_ota.state = VG_OTA_DONE_OK;
        s_net.ota_active = false;
        cancel_ota_timer();
        vg_model_append_log(VG_LOG_OTA, VG_SEV_OK, "OTA 升级完成");
        notify_all();
        return;
    }
    notify_all();
}

void vg_model_ota_accept(void)
{
    char logbuf[80];
    if(s_ota.state != VG_OTA_OFFER) return;
    /* Manual 16.9/5.11: block upgrades while a critical alarm is active */
    if(s_alarm.active && s_alarm.severity == VG_SEV_CRIT) {
        s_ota.state = VG_OTA_DONE_FAIL;
        strncpy(s_ota.error, "存在严重告警，禁止升级", sizeof(s_ota.error) - 1);
        vg_model_append_log(VG_LOG_OTA, VG_SEV_WARN, "OTA 被拒绝: 存在严重告警");
        notify_all();
        return;
    }
    if(s_scenario == VG_SCENARIO_OFFLINE || !s_net.net_ok) {
        s_ota.state = VG_OTA_DONE_FAIL;
        strncpy(s_ota.error, "网络不可用", sizeof(s_ota.error) - 1);
        vg_model_append_log(VG_LOG_OTA, VG_SEV_WARN, "OTA 升级失败: 网络不可用");
        notify_all();
        return;
    }
    s_ota.state = VG_OTA_PROGRESS;
    s_ota.progress_pct = 0;
    s_net.ota_active = true;
    lv_snprintf(logbuf, sizeof(logbuf), "OTA 升级开始: v%s", s_ota.version);
    vg_model_append_log(VG_LOG_OTA, VG_SEV_INFO, logbuf);
    notify_all();
    s_ota_tmr = lv_timer_create(ota_tick_cb, VG_OTA_STEP_MS, NULL);
}

void vg_model_ota_cancel(void)
{
    if(s_ota.state != VG_OTA_OFFER) return;
    s_ota.state = VG_OTA_IDLE;
    s_net.ota_active = false;
    vg_model_append_log(VG_LOG_OTA, VG_SEV_INFO, "OTA 升级已取消");
    notify_all();
}

void vg_model_ota_retry(void)
{
    if(s_ota.state != VG_OTA_DONE_FAIL) return;
    s_ota.state = VG_OTA_OFFER;
    s_ota.progress_pct = 0;
    s_ota.error[0] = '\0';
    s_net.ota_active = true;
    notify_all();
}

static void refresh_sys_status(void)
{
    memset(&s_sys, 0, sizeof(s_sys));
    strncpy(s_sys.ip, s_net.ip, sizeof(s_sys.ip) - 1);
    s_sys.mimo_ok = s_net.mimo_ok;
    s_sys.latency_ms = s_net.latency_ms;
    s_sys.rs485_ok = s_net.acq_ok;
    s_sys.fs_free_pct = 78;
    s_sys.audio_ok = s_net.aud_ok;
    strncpy(s_sys.version, "v9.1.0-sim", sizeof(s_sys.version) - 1);
    if(s_ota.state == VG_OTA_PROGRESS) {
        strncpy(s_sys.ota_label, "升级中", sizeof(s_sys.ota_label) - 1);
    }
    else if(s_net.ota_active) {
        strncpy(s_sys.ota_label, "可升级", sizeof(s_sys.ota_label) - 1);
    }
    else {
        strncpy(s_sys.ota_label, "无更新", sizeof(s_sys.ota_label) - 1);
    }
    s_sys.log_used = s_log_n;
    s_sys.log_cap = VG_LOG_MAX;
}

const vg_sys_status_t * vg_model_get_sys_status(void)
{
    refresh_sys_status();
    return &s_sys;
}

const vg_ota_t * vg_model_get_ota(void)
{
    return &s_ota;
}

static void seed_base_logs(void)
{
    s_log_n = 0;
    vg_model_append_log(VG_LOG_SYS, VG_SEV_INFO, "系统启动完成");
    vg_model_append_log(VG_LOG_SYS, VG_SEV_OK, "网络连接已建立");
    vg_model_append_log(VG_LOG_UI, VG_SEV_INFO, "进入总览页");
    vg_model_append_log(VG_LOG_SYS, VG_SEV_OK, "采集任务运行中");
    vg_model_append_log(VG_LOG_SYS, VG_SEV_INFO, "时钟同步成功");
    vg_model_append_log(VG_LOG_UI, VG_SEV_INFO, "状态栏刷新");
    vg_model_append_log(VG_LOG_SYS, VG_SEV_OK, "存储自检通过");
    vg_model_append_log(VG_LOG_SYS, VG_SEV_INFO, "配置加载完成");
}

static void set_alarm_from_sensor(vg_sensor_t * s, vg_severity_t sev, const char * title,
                                  float thr, int32_t duration)
{
    memset(&s_alarm, 0, sizeof(s_alarm));
    s_alarm.active = true;
    s_alarm.severity = sev;
    s_alarm.value = s->value;
    s_alarm.threshold = thr;
    s_alarm.duration_sec = duration;
    s_alarm.acked = false;
    s_alarm.muted = false;
    strncpy(s_alarm.title, title, sizeof(s_alarm.title) - 1);
    strncpy(s_alarm.sensor_id, s->id, sizeof(s_alarm.sensor_id) - 1);
}

static void set_ota_offer(void)
{
    s_net.ota_active = true;
    s_ota.state = VG_OTA_OFFER;
    strncpy(s_ota.version, "9.2.0", sizeof(s_ota.version) - 1);
    strncpy(s_ota.note, "新增传感器支持与性能优化", sizeof(s_ota.note) - 1);
    s_ota.size_mb = 24;
}

static void apply_scenario(vg_scenario_t s)
{
    char logbuf[80];
    vg_sensor_t * primary;

    cancel_diag_timer();
    reset_add_state();
    reset_ota_state();
    s_scenario = s;
    memset(&s_alarm, 0, sizeof(s_alarm));
    memset(&s_net, 0, sizeof(s_net));
    clear_diagnosis();
    seed_fleet();
    primary = s_sensor_n > 0 ? &s_sensors[0] : NULL;

#ifdef VG_HMI_BOARD
    s_net.net_ok = false;
    s_net.mimo_ok = false;
    s_net.acq_ok = (s_sensor_n > 0);
    s_net.aud_ok = true;
    s_net.ota_active = false;
    s_net.latency_ms = 0;
    s_net.ip[0] = '\0';
#else
    s_net.net_ok = true;
    s_net.mimo_ok = true;
    s_net.acq_ok = true;
    s_net.aud_ok = true;
    s_net.ota_active = false;
    s_net.latency_ms = 28;
    strncpy(s_net.ip, "192.168.1.50", sizeof(s_net.ip) - 1);
#endif

    switch(s) {
        case VG_SCENARIO_WARN:
            if(primary == NULL) break;
            primary->base_value = 58.0f;
            fill_sensor_history(primary, 58.0f, 1.5f, 0.15f);
            primary->severity = VG_SEV_WARN;
            primary->age_sec = 12;
            primary->quality_pct = 92;
            if(s_sensor_n > 2) {
                s_sensors[2].base_value = 185.0f;
                fill_sensor_history(&s_sensors[2], 185.0f, 2.0f, 0.2f);
                s_sensors[2].severity = VG_SEV_WARN;
                s_sensors[2].age_sec = 8;
            }
            if(s_sensor_n > 8) {
                s_sensors[8].severity = VG_SEV_WARN;
                s_sensors[8].base_value = 60.0f;
                fill_sensor_history(&s_sensors[8], 60.0f, 1.0f, 0.1f);
            }
            set_alarm_from_sensor(primary, VG_SEV_WARN, "温度预警", primary->thr_warn, 45);
            lv_snprintf(logbuf, sizeof(logbuf), "告警触发: %s", s_alarm.title);
            vg_model_append_log(VG_LOG_ALARM, VG_SEV_WARN, logbuf);
            break;

        case VG_SCENARIO_CRIT:
            if(primary == NULL) break;
            primary->base_value = 76.0f;
            fill_sensor_history(primary, 76.0f, 2.0f, 0.2f);
            primary->severity = VG_SEV_CRIT;
            primary->age_sec = 30;
            primary->quality_pct = 88;
            if(s_sensor_n > 3) {
                s_sensors[3].base_value = 6.2f;
                fill_sensor_history(&s_sensors[3], 6.2f, 0.4f, 0.1f);
                s_sensors[3].severity = VG_SEV_WARN;
                s_sensors[3].age_sec = 18;
            }
            if(s_sensor_n > 9) {
                s_sensors[9].severity = VG_SEV_CRIT;
                s_sensors[9].base_value = 78.0f;
                fill_sensor_history(&s_sensors[9], 78.0f, 1.5f, 0.2f);
            }
            set_alarm_from_sensor(primary, VG_SEV_CRIT, "温度严重告警", primary->thr_crit, 120);
            lv_snprintf(logbuf, sizeof(logbuf), "告警触发: %s", s_alarm.title);
            vg_model_append_log(VG_LOG_ALARM, VG_SEV_CRIT, logbuf);
            break;

        case VG_SCENARIO_OFFLINE:
            if(primary == NULL) break;
            primary->base_value = 0.0f;
            fill_sensor_history(primary, 0.0f, 0.0f, 0.0f);
            primary->value = 0.0f;
            primary->online = false;
            primary->severity = VG_SEV_OFFLINE;
            primary->age_sec = 300;
            primary->quality_pct = 0;
            s_net.acq_ok = false;
            if(s_sensor_n > 6) {
                s_sensors[6].online = false;
                s_sensors[6].severity = VG_SEV_OFFLINE;
                s_sensors[6].age_sec = 120;
                s_sensors[6].quality_pct = 0;
            }
            set_alarm_from_sensor(primary, VG_SEV_OFFLINE, "传感器离线", 0.0f, 300);
            lv_snprintf(logbuf, sizeof(logbuf), "告警触发: %s", s_alarm.title);
            vg_model_append_log(VG_LOG_ALARM, VG_SEV_OFFLINE, logbuf);
            /* OTA offer stays available; upgrading fails (no network). */
            set_ota_offer();
            break;

        case VG_SCENARIO_AI_DOWN:
            s_net.mimo_ok = false;
            s_net.latency_ms = 999;
            vg_model_append_log(VG_LOG_SYS, VG_SEV_WARN, "MiMo 服务不可用");
            break;

        case VG_SCENARIO_OTA:
            set_ota_offer();
            vg_model_append_log(VG_LOG_OTA, VG_SEV_INFO, "OTA 任务进行中");
            break;

        case VG_SCENARIO_NORMAL:
        default:
            vg_model_append_log(VG_LOG_SYS, VG_SEV_OK, "场景切换: 正常");
            break;
    }
    rebuild_filter();
}

void vg_model_init(void)
{
    s_listener_n = 0;
    s_tick = 0;
    s_diag_tmr = NULL;
    s_log_n = 0;
    s_home_filter = VG_HOME_FILTER_ALL;
    s_selected_id[0] = '\0';
    load_custom_types();
    seed_base_logs();
    apply_scenario(VG_SCENARIO_NORMAL);
#ifdef VG_HMI_BOARD
    vg_ui_backend_boot_points();
#endif
}

void vg_model_set_scenario(vg_scenario_t s)
{
    if(s < VG_SCENARIO_NORMAL || s > VG_SCENARIO_OTA) s = VG_SCENARIO_NORMAL;
    apply_scenario(s);
    notify_all();
}

vg_scenario_t vg_model_get_scenario(void) { return s_scenario; }

const vg_sensor_t * vg_model_get_sensor(const char * id)
{
    uint16_t i;
    if(id == NULL || id[0] == '\0') return &s_sensors[0];
    for(i = 0; i < s_sensor_n; i++) {
        if(strcmp(id, s_sensors[i].id) == 0) return &s_sensors[i];
    }
    return NULL;
}

const vg_sensor_t * vg_model_get_primary_sensor(void) { return &s_sensors[0]; }

const vg_sensor_t * vg_model_get_sensors(uint16_t * out_count)
{
    if(out_count) *out_count = s_sensor_n;
    return s_sensors;
}

const vg_sensor_t * vg_model_home_sensor_at(uint16_t i)
{
    if(i >= s_filt_n) return NULL;
    return &s_sensors[s_filt_idx[i]];
}

uint16_t vg_model_home_sensor_count(void) { return s_filt_n; }

void vg_model_set_home_filter(vg_home_filter_t f)
{
    s_home_filter = f;
    rebuild_filter();
    notify_all();
}

vg_home_filter_t vg_model_get_home_filter(void) { return s_home_filter; }

void vg_model_count_by_filter(uint16_t * all, uint16_t * alarm, uint16_t * offline, uint16_t * ok)
{
    uint16_t i, a = 0, o = 0, k = 0;
    for(i = 0; i < s_sensor_n; i++) {
        const vg_sensor_t * s = &s_sensors[i];
        if(!s->online || s->severity == VG_SEV_OFFLINE) o++;
        else if(s->severity == VG_SEV_WARN || s->severity == VG_SEV_CRIT) a++;
        else if(s->severity == VG_SEV_OK) k++;
    }
    if(all) *all = s_sensor_n;
    if(alarm) *alarm = a;
    if(offline) *offline = o;
    if(ok) *ok = k;
}

void vg_model_set_selected_sensor(const char * id)
{
    if(id == NULL || id[0] == '\0') return;
    strncpy(s_selected_id, id, sizeof(s_selected_id) - 1);
    s_selected_id[sizeof(s_selected_id) - 1] = '\0';
    notify_all();
}

const char * vg_model_get_selected_sensor_id(void) { return s_selected_id; }

const vg_sensor_t * vg_model_get_selected_sensor(void)
{
    const vg_sensor_t * s = vg_model_get_sensor(s_selected_id);
    return s ? s : &s_sensors[0];
}

const vg_alarm_t * vg_model_get_active_alarm(void) { return &s_alarm; }
const vg_net_status_t * vg_model_get_net(void) { return &s_net; }

void vg_model_on_change(vg_model_change_cb_t cb, void * user)
{
    int i;
    if(cb == NULL) return;
    for(i = 0; i < s_listener_n; i++) {
        if(s_listeners[i].cb == cb && s_listeners[i].user == user) return;
    }
    if(s_listener_n >= VG_MAX_LISTENERS) return;
    s_listeners[s_listener_n].cb = cb;
    s_listeners[s_listener_n].user = user;
    s_listener_n++;
}

void vg_model_off_change(vg_model_change_cb_t cb, void * user)
{
    int i;
    if(cb == NULL) return;
    for(i = 0; i < s_listener_n; i++) {
        if(s_listeners[i].cb == cb && s_listeners[i].user == user) {
            s_listeners[i] = s_listeners[s_listener_n - 1];
            s_listener_n--;
            return;
        }
    }
}

void vg_model_tick(void)
{
    uint16_t i;
    bool dirty = false;
    s_tick++;
#ifndef VG_HMI_BOARD
    for(i = 0; i < s_sensor_n; i++) {
        if(s_sensors[i].online && s_scenario != VG_SCENARIO_OFFLINE) {
            /* Anchor at base_value: no cumulative drift across ticks */
            fill_sensor_history(&s_sensors[i], s_sensors[i].base_value, 0.3f, 0.03f);
            s_sensors[i].age_sec = 1;
        }
    }
    dirty = true;
#else
    dirty = vg_ui_backend_apply_live();
    (void)i;
#endif
    if(s_alarm.active) {
        const vg_sensor_t * p = vg_model_get_sensor(s_alarm.sensor_id);
        if(p) s_alarm.value = p->value;
        s_alarm.duration_sec++;
        dirty = true;
    }
    if(dirty) {
        notify_all();
    }
}

const char * vg_severity_label_zh(vg_severity_t sev)
{
    switch(sev) {
        case VG_SEV_WARN: return "预警";
        case VG_SEV_CRIT: return "严重";
        case VG_SEV_OFFLINE: return "离线";
        case VG_SEV_INFO: return "信息";
        default: return "正常";
    }
}

const char * vg_scenario_label(vg_scenario_t s)
{
    switch(s) {
        case VG_SCENARIO_WARN: return "预警";
        case VG_SCENARIO_CRIT: return "严重";
        case VG_SCENARIO_OFFLINE: return "离线";
        case VG_SCENARIO_AI_DOWN: return "AI不可用";
        case VG_SCENARIO_OTA: return "OTA中";
        default: return "正常";
    }
}

const vg_diagnosis_t * vg_model_get_diagnosis(void) { return &s_diag; }

void vg_model_request_diagnosis(void)
{
    if(s_diag.state == VG_DIAG_LOADING) return;
    cancel_diag_timer();
    clear_diagnosis();
    if(s_alarm.active)
        strncpy(s_diag.alarm_title, s_alarm.title, sizeof(s_diag.alarm_title) - 1);
    else
        strncpy(s_diag.alarm_title, "无活动告警", sizeof(s_diag.alarm_title) - 1);

    if(s_scenario == VG_SCENARIO_AI_DOWN || !s_net.mimo_ok) {
        s_diag.state = VG_DIAG_ERROR;
        strncpy(s_diag.error_msg, "MiMo 不可用", sizeof(s_diag.error_msg) - 1);
        vg_model_append_log(VG_LOG_DIAG, VG_SEV_WARN, "AI 诊断失败: MiMo 不可用");
        notify_all();
        return;
    }
    s_diag.state = VG_DIAG_LOADING;
    vg_model_append_log(VG_LOG_DIAG, VG_SEV_INFO, "开始 AI 诊断");
    notify_all();
    s_diag_tmr = lv_timer_create(diag_timer_cb, VG_DIAG_LOAD_MS, NULL);
    lv_timer_set_repeat_count(s_diag_tmr, 1);
}

void vg_model_ack_alarm(void)
{
    if(!s_alarm.active || s_alarm.acked) return;
    s_alarm.acked = true;
    vg_model_append_log(VG_LOG_UI, VG_SEV_INFO, "告警已标记处理");
    notify_all();
}

void vg_model_mute_alarm(void)
{
    if(!s_alarm.active) return;
    s_alarm.muted = true;
    vg_model_append_log(VG_LOG_UI, VG_SEV_INFO, "告警已静音");
    notify_all();
}

const vg_log_entry_t * vg_model_get_logs(uint16_t * out_count)
{
    if(out_count) *out_count = s_log_n;
    return s_logs;
}

const char * vg_log_type_label_zh(vg_log_type_t t)
{
    switch(t) {
        case VG_LOG_ALARM: return "告警";
        case VG_LOG_DIAG: return "诊断";
        case VG_LOG_SYS: return "系统";
        case VG_LOG_OTA: return "OTA";
        case VG_LOG_UI: return "界面";
        default: return "事件";
    }
}

const char * vg_risk_label_zh(const char * risk)
{
    if(risk == NULL) return "未知";
    if(strcmp(risk, "high") == 0) return "高";
    if(strcmp(risk, "medium") == 0) return "中";
    if(strcmp(risk, "low") == 0) return "低";
    return "未知";
}

void vg_model_import_discover_slaves(const vg_ui_slave_t * slaves, int n)
{
    int i;

    if(slaves == NULL || n <= 0) {
        return;
    }

    if(n > VG_SENSOR_MAX) {
        n = VG_SENSOR_MAX;
    }

    s_sensor_n = (uint16_t)n;
    for(i = 0; i < n; i++) {
        vg_sensor_t * s = &s_sensors[i];
        memset(s, 0, sizeof(*s));
        lv_snprintf(s->id, sizeof(s->id), "s_%u", (unsigned)slaves[i].addr);
        lv_snprintf(s->name, sizeof(s->name), "从站%u", (unsigned)slaves[i].addr);
        strncpy(s->type, "温度", sizeof(s->type) - 1);
        strncpy(s->unit, "C", sizeof(s->unit) - 1);
        strncpy(s->formula, "R0", sizeof(s->formula) - 1);
        s->function_code = 3;
        s->length = 1;
        s->data_format = VG_SENSOR_FMT_INT16;
        s->word_order = VG_SENSOR_ORDER_ABCD;
        s->period_ms = 1000;
        s->reg_addr = 0;
        s->slave_addr = slaves[i].addr;
        s->quality_pct = 0;
        s->online = true;
        s->age_sec = 0;
        s->severity = VG_SEV_OK;
        s->value = 0.0f;
        s->base_value = 0.0f;
        s->history_len = 0;
        s->thr_low = 5;
        s->thr_warn = 55;
        s->thr_crit = 70;
    }

    if(s_selected_id[0] == '\0' && s_sensor_n > 0) {
        strncpy(s_selected_id, s_sensors[0].id, sizeof(s_selected_id) - 1);
    }

#ifdef VG_HMI_BOARD
    s_net.acq_ok = true;
#endif

    rebuild_filter();
    notify_all();
}

void vg_model_import_mthings(void)
{
    int i;
    int n = vg_mthings_point_count;

    if(n <= 0) {
        return;
    }
    if(n > VG_SENSOR_MAX) {
        n = VG_SENSOR_MAX;
    }

    s_sensor_n = (uint16_t)n;
    for(i = 0; i < n; i++) {
        vg_sensor_t * s = &s_sensors[i];
        const vg_mthings_point_t * p = &vg_mthings_points[i];

        memset(s, 0, sizeof(*s));
        lv_snprintf(s->id, sizeof(s->id), "p%02u_%u",
                    (unsigned)p->addr, (unsigned)p->reg);
        lv_snprintf(s->name, sizeof(s->name), "%u·%s",
                    (unsigned)p->addr, p->name);
        strncpy(s->type, p->dev, sizeof(s->type) - 1);
        strncpy(s->unit, p->unit, sizeof(s->unit) - 1);
        lv_snprintf(s->formula, sizeof(s->formula), "R%u", (unsigned)p->reg);
        s->function_code = 3;
        s->length = 1;
        s->data_format = p->is_signed ? VG_SENSOR_FMT_INT16 : VG_SENSOR_FMT_UINT16;
        s->word_order = VG_SENSOR_ORDER_ABCD;
        s->period_ms = 1000;
        s->reg_addr = (int32_t)p->reg;
        s->slave_addr = p->addr;
        s->quality_pct = 0;
        s->online = false;
        s->age_sec = 0;
        s->severity = VG_SEV_OFFLINE;
        s->value = 0.0f;
        s->base_value = 0.0f;
        s->history_len = 0;
        s->thr_low = 0;
        s->thr_warn = 0;
        s->thr_crit = 0;
    }

    if(s_selected_id[0] == '\0' && s_sensor_n > 0) {
        strncpy(s_selected_id, s_sensors[0].id, sizeof(s_selected_id) - 1);
    }

#ifdef VG_HMI_BOARD
    s_net.acq_ok = true;
#endif

    rebuild_filter();
    notify_all();
}

void vg_model_import_runtime_points(const vg_runtime_point_t * pts, int n)
{
    int i;

    if(pts == NULL || n <= 0) {
        s_sensor_n = 0;
        s_selected_id[0] = '\0';
        rebuild_filter();
        notify_all();
        return;
    }

    if(n > VG_SENSOR_MAX) {
        n = VG_SENSOR_MAX;
    }

    s_sensor_n = (uint16_t)n;
    for(i = 0; i < n; i++) {
        vg_sensor_t * s = &s_sensors[i];
        const vg_runtime_point_t * p = &pts[i];
        int signed_v = (strcmp(p->dtype, "uint16") != 0);

        memset(s, 0, sizeof(*s));
        strncpy(s->id, p->id[0] ? p->id : "pt", sizeof(s->id) - 1);
        strncpy(s->name,
                p->name[0] ? p->name : (p->id[0] ? p->id : "pt"),
                sizeof(s->name) - 1);
        strncpy(s->type, "point", sizeof(s->type) - 1);
        strncpy(s->unit, p->unit, sizeof(s->unit) - 1);
        lv_snprintf(s->formula, sizeof(s->formula), "R%u", (unsigned)p->reg);
        s->function_code = p->fc ? p->fc : 3;
        s->length = 1;
        s->data_format = signed_v ? VG_SENSOR_FMT_INT16 : VG_SENSOR_FMT_UINT16;
        s->word_order = VG_SENSOR_ORDER_ABCD;
        s->period_ms = 1000;
        s->reg_addr = (int32_t)p->reg;
        s->slave_addr = p->addr;
        s->quality_pct = 0;
        s->online = false;
        s->age_sec = 0;
        s->severity = VG_SEV_OFFLINE;
        s->value = 0.0f;
        s->base_value = 0.0f;
        s->history_len = 0;
        s->thr_low = 0;
        s->thr_warn = p->has_warn ? p->warn : 0;
        s->thr_crit = p->has_crit ? p->crit : 0;
        strncpy(s->cmp, p->cmp, sizeof(s->cmp) - 1);
        s->has_warn = p->has_warn;
        s->has_crit = p->has_crit;
        s->fail_n = (p->fail_n >= 1) ? p->fail_n : 3;
        s->fail_streak = 0;
    }

    if(s_selected_id[0] == '\0' && s_sensor_n > 0) {
        strncpy(s_selected_id, s_sensors[0].id, sizeof(s_selected_id) - 1);
    }

#ifdef VG_HMI_BOARD
    s_net.acq_ok = true;
#endif

    rebuild_filter();
    notify_all();
}

bool vg_model_set_live(uint16_t idx, float value, bool online)
{
    vg_sensor_t * s;
    bool changed = false;

    if(idx >= s_sensor_n) {
        return false;
    }

    s = &s_sensors[idx];
    if(online) {
        if(s->fail_streak != 0) {
            s->fail_streak = 0;
            changed = true;
        }
        if(!s->online || s->value != value) {
            changed = true;
        }
        s->online = true;
        s->value = value;
        s->base_value = value;
        s->age_sec = 1;
        s->quality_pct = 95;
#ifdef VG_HMI_BOARD
        s_net.acq_ok = true;
        {
            struct vg_alarm_rule rule;
            enum vg_alarm_kind kind;
            float thr = 0.0f;
            vg_severity_t sev;
            const char * title;
            int rank;

            memset(&rule, 0, sizeof(rule));
            strncpy(rule.cmp, s->cmp, sizeof(rule.cmp) - 1);
            rule.has_warn = s->has_warn;
            rule.has_crit = s->has_crit;
            rule.warn = s->thr_warn;
            rule.crit = s->thr_crit;
            rule.fail_n = s->fail_n;
            kind = vg_alarm_eval(&rule, 1, 0, value, &thr);
            if(kind == VG_ALARM_KIND_CRIT) {
                sev = VG_SEV_CRIT;
                title = "点表严重告警";
            }
            else if(kind == VG_ALARM_KIND_WARN) {
                sev = VG_SEV_WARN;
                title = "点表预警";
            }
            else {
                sev = VG_SEV_OK;
                title = NULL;
            }
            if(s->severity != sev) {
                s->severity = sev;
                changed = true;
            }
            rank = vg_alarm_kind_rank(kind);
            if(title != NULL &&
               (!s_alarm.active ||
                strcmp(s_alarm.sensor_id, s->id) == 0 ||
                rank > vg_alarm_kind_rank(
                    s_alarm.severity == VG_SEV_CRIT ? VG_ALARM_KIND_CRIT :
                    s_alarm.severity == VG_SEV_OFFLINE ? VG_ALARM_KIND_OFFLINE :
                    s_alarm.severity == VG_SEV_WARN ? VG_ALARM_KIND_WARN :
                    VG_ALARM_KIND_NONE))) {
                char titled[VG_ALARM_TITLE_MAX];

                lv_snprintf(titled, sizeof(titled), "%s %s", s->name, title);
                set_alarm_from_sensor(s, sev, titled, thr,
                                     s_alarm.active &&
                                     strcmp(s_alarm.sensor_id, s->id) == 0 ?
                                     s_alarm.duration_sec : 0);
                changed = true;
            }
        }
#else
        s->severity = VG_SEV_OK;
#endif
        if(s->history_len < VG_HISTORY_LEN) {
            s->history[s->history_len++] = value;
        }
        else {
            memmove(&s->history[0], &s->history[1],
                    (VG_HISTORY_LEN - 1) * sizeof(float));
            s->history[VG_HISTORY_LEN - 1] = value;
        }
    }
    else {
        uint8_t prev_streak = s->fail_streak;

        if(s->fail_streak < 255) {
            s->fail_streak++;
        }
        if(s->age_sec < 100000) {
            s->age_sec++;
        }
        s->quality_pct = 0;
#ifdef VG_HMI_BOARD
        {
            struct vg_alarm_rule rule;
            enum vg_alarm_kind kind;
            float thr = 0.0f;

            memset(&rule, 0, sizeof(rule));
            strncpy(rule.cmp, s->cmp, sizeof(rule.cmp) - 1);
            rule.has_warn = s->has_warn;
            rule.has_crit = s->has_crit;
            rule.warn = s->thr_warn;
            rule.crit = s->thr_crit;
            rule.fail_n = s->fail_n;
            kind = vg_alarm_eval(&rule, 0, s->fail_streak, s->value, &thr);
            if(kind == VG_ALARM_KIND_OFFLINE) {
                if(s->online || s->severity != VG_SEV_OFFLINE) {
                    changed = true;
                }
                s->online = false;
                s->severity = VG_SEV_OFFLINE;
                if(!s_alarm.active ||
                   strcmp(s_alarm.sensor_id, s->id) == 0 ||
                   vg_alarm_kind_rank(VG_ALARM_KIND_OFFLINE) >
                   vg_alarm_kind_rank(
                       s_alarm.severity == VG_SEV_CRIT ? VG_ALARM_KIND_CRIT :
                       s_alarm.severity == VG_SEV_OFFLINE ? VG_ALARM_KIND_OFFLINE :
                       s_alarm.severity == VG_SEV_WARN ? VG_ALARM_KIND_WARN :
                       VG_ALARM_KIND_NONE)) {
                    char titled[VG_ALARM_TITLE_MAX];

                    lv_snprintf(titled, sizeof(titled), "%s 离线", s->name);
                    set_alarm_from_sensor(s, VG_SEV_OFFLINE, titled, 0.0f,
                                         s_alarm.active &&
                                         strcmp(s_alarm.sensor_id, s->id) == 0 ?
                                         s_alarm.duration_sec : 0);
                    changed = true;
                }
            }
            else if(prev_streak == 0) {
                changed = true;
            }
        }
#else
        s->online = false;
        s->severity = VG_SEV_OFFLINE;
        changed = true;
#endif
    }
    return changed;
}
