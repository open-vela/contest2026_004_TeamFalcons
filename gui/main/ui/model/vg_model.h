#ifndef VG_MODEL_H
#define VG_MODEL_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "theme/vg_theme.h"
#include "vg_ui_backend.h"

#define VG_SENSOR_ID_MAX 24
#define VG_SENSOR_NAME_MAX 48
#define VG_SENSOR_TYPE_MAX 24
#define VG_SENSOR_FORMULA_MAX 64
#define VG_SENSOR_REG_HEX_MAX 5
#define VG_CUSTOM_TYPE_MAX 12
#define VG_UNIT_MAX 12
/* PC sim keeps a 5 min trend window. Board BSS must leave room for
 * NET + mbedTLS + Agent when velaguard-lvgl merges the net stack.
 * Trend page is stage-2 / deferred. */
#ifdef VG_HMI_BOARD
#define VG_HISTORY_LEN 16
#define VG_SENSOR_MAX 64
#else
#define VG_HISTORY_LEN 300
#define VG_SENSOR_MAX 256
#endif
#define VG_ALARM_TITLE_MAX 64
#define VG_LOG_MAX 24

typedef enum {
    VG_SCENARIO_NORMAL = 1,
    VG_SCENARIO_WARN,
    VG_SCENARIO_CRIT,
    VG_SCENARIO_OFFLINE,
    VG_SCENARIO_AI_DOWN,
    VG_SCENARIO_OTA
} vg_scenario_t;

typedef enum {
    VG_HOME_FILTER_ALL = 0,
    VG_HOME_FILTER_ALARM,
    VG_HOME_FILTER_OFFLINE,
    VG_HOME_FILTER_OK
} vg_home_filter_t;

typedef struct {
    bool net_ok;
    bool wifi_ok;      /* ESP STA assoc + IP (board); mirrors net on sim */
    bool mimo_ok;      /* AI bridge: MQTT session online */
    bool acq_ok;
    bool aud_ok;
    bool ota_active;
    char ip[20];
    int32_t latency_ms;
} vg_net_status_t;

typedef enum {
    VG_SENSOR_FMT_UINT16 = 0,
    VG_SENSOR_FMT_INT16,
    VG_SENSOR_FMT_UINT32,
    VG_SENSOR_FMT_INT32,
    VG_SENSOR_FMT_FLOAT32
} vg_sensor_data_format_t;

typedef enum {
    VG_SENSOR_ORDER_ABCD = 0,
    VG_SENSOR_ORDER_CDAB
} vg_sensor_word_order_t;

typedef struct {
    char id[VG_SENSOR_ID_MAX];
    char name[VG_SENSOR_NAME_MAX];
    char type[VG_SENSOR_TYPE_MAX];
    char unit[VG_UNIT_MAX];
    char formula[VG_SENSOR_FORMULA_MAX];
    uint8_t function_code;
    int32_t length;
    vg_sensor_data_format_t data_format;
    vg_sensor_word_order_t word_order;
    uint8_t slave_addr;   /* Modbus RTU slave (1-247) */
    float base_value;     /* tick fluctuation anchor (drift-free) */
    float value;
    float thr_warn;
    float thr_crit;
    float thr_low;
    char cmp[4];
    uint8_t has_warn;
    uint8_t has_crit;
    uint8_t fail_n;
    uint8_t fail_streak;
    uint8_t failwin_bits;   /* last-8-poll failure ring, bit=1 failed */
    uint8_t failwin_pos;    /* next ring slot to overwrite */
    uint8_t failwin_fails;  /* failures currently inside the window */
    vg_severity_t severity;
    int32_t age_sec;
    int32_t period_ms;
    int32_t reg_addr;
    int32_t quality_pct;
    bool online;
    float history[VG_HISTORY_LEN];
    uint16_t history_len;
} vg_sensor_t;

typedef struct {
    bool active;
    vg_severity_t severity;
    char title[VG_ALARM_TITLE_MAX];
    char sensor_id[VG_SENSOR_ID_MAX];
    float value;
    float threshold;
    int32_t duration_sec;
    bool acked;
    bool muted;
} vg_alarm_t;

typedef enum {
    VG_ADD_IDLE = 0,     /* 无候选 */
    VG_ADD_PREVIEW,      /* 已生成候选，待测试/确认 */
    VG_ADD_TESTING,      /* 测试读取中 */
    VG_ADD_TEST_OK,      /* 测试成功，可确认 */
    VG_ADD_TEST_FAIL     /* 测试失败，可重试/放弃 */
} vg_add_sensor_state_t;

typedef struct {
    char id[VG_SENSOR_ID_MAX];
    char name[VG_SENSOR_NAME_MAX];
    char type[VG_SENSOR_TYPE_MAX];
    char unit[VG_UNIT_MAX];
    char formula[VG_SENSOR_FORMULA_MAX];
    uint8_t function_code;
    uint8_t slave_addr;   /* Modbus RTU slave (1-247) */
    char reg_addr_hex[VG_SENSOR_REG_HEX_MAX];
    int32_t reg_addr;
    int32_t length;
    int32_t period_ms;
    vg_sensor_data_format_t data_format;
    vg_sensor_word_order_t word_order;
    float thr_low;
    float thr_warn;
    float thr_crit;
    char source[16];     /* builtin | custom | manual */
} vg_sensor_candidate_t;

typedef struct {
    bool used;
    char type[VG_SENSOR_TYPE_MAX];
    char unit[VG_UNIT_MAX];
    char formula[VG_SENSOR_FORMULA_MAX];
    vg_sensor_data_format_t data_format;
    vg_sensor_word_order_t word_order;
    float thr_low;
    float thr_warn;
    float thr_crit;
} vg_sensor_type_template_t;

typedef struct {
    char ip[20];
    bool mimo_ok;
    int32_t latency_ms;
    bool rs485_ok;
    int32_t fs_free_pct;
    bool audio_ok;
    char version[16];
    char ota_label[16];
    int32_t log_used;
    int32_t log_cap;
} vg_sys_status_t;

typedef enum {
    VG_OTA_IDLE = 0,
    VG_OTA_OFFER,
    VG_OTA_PROGRESS,
    VG_OTA_DONE_OK,
    VG_OTA_DONE_FAIL
} vg_ota_state_t;

typedef struct {
    vg_ota_state_t state;
    char version[16];
    char note[80];
    int32_t size_mb;
    int32_t progress_pct;
    char error[48];
} vg_ota_t;

typedef enum {
    VG_DIAG_IDLE = 0,
    VG_DIAG_LOADING,
    VG_DIAG_OK,
    VG_DIAG_ERROR
} vg_diag_state_t;

typedef struct {
    vg_diag_state_t state;
    char summary[128];
    char risk[16];
    char causes[3][64];
    uint8_t cause_n;
    char actions[3][64];
    uint8_t action_n;
    int32_t confidence_pct;
    char error_msg[64];
    char alarm_title[64];
} vg_diagnosis_t;

typedef enum {
    VG_LOG_ALARM = 0,
    VG_LOG_DIAG,
    VG_LOG_SYS,
    VG_LOG_OTA,
    VG_LOG_UI
} vg_log_type_t;

typedef struct {
    vg_log_type_t type;
    vg_severity_t severity;
    char time[8];
    char text[80];
} vg_log_entry_t;

typedef void (*vg_model_change_cb_t)(void * user);

void vg_model_init(void);
void vg_model_set_scenario(vg_scenario_t s);
vg_scenario_t vg_model_get_scenario(void);
const vg_sensor_t * vg_model_get_sensor(const char * id);
const vg_sensor_t * vg_model_get_primary_sensor(void);
const vg_sensor_t * vg_model_get_sensors(uint16_t * out_count);
const vg_sensor_t * vg_model_home_sensor_at(uint16_t i);
uint16_t vg_model_home_sensor_count(void);
void vg_model_set_home_filter(vg_home_filter_t f);
vg_home_filter_t vg_model_get_home_filter(void);
void vg_model_count_by_filter(uint16_t * all, uint16_t * alarm, uint16_t * offline, uint16_t * ok);
void vg_model_set_selected_sensor(const char * id);
const char * vg_model_get_selected_sensor_id(void);
const vg_sensor_t * vg_model_get_selected_sensor(void);
const vg_alarm_t * vg_model_get_active_alarm(void);
const vg_net_status_t * vg_model_get_net(void);
void vg_model_on_change(vg_model_change_cb_t cb, void * user);
void vg_model_off_change(vg_model_change_cb_t cb, void * user);
void vg_model_tick(void);
const char * vg_severity_label_zh(vg_severity_t sev);
const char * vg_scenario_label(vg_scenario_t s);

const vg_diagnosis_t * vg_model_get_diagnosis(void);
void vg_model_request_diagnosis(void);
void vg_model_ack_alarm(void);
void vg_model_mute_alarm(void);
const vg_log_entry_t * vg_model_get_logs(uint16_t * out_count);
void vg_model_append_log(vg_log_type_t t, vg_severity_t sev, const char * text);
const char * vg_log_type_label_zh(vg_log_type_t t);
const char * vg_risk_label_zh(const char * risk);

/* 添加传感器（C3） */
void vg_model_add_sensor_begin(void);
bool vg_model_add_sensor_generate(const vg_sensor_candidate_t * config);
void vg_model_add_sensor_test(void);
bool vg_model_add_sensor_confirm(void);
void vg_model_add_sensor_abort(void);
vg_add_sensor_state_t vg_model_add_sensor_state(void);
const vg_sensor_candidate_t * vg_model_get_candidate(void);
const char * vg_model_add_sensor_test_msg(void);
float vg_model_add_sensor_test_value(void);
int32_t vg_model_add_sensor_test_quality(void);
void vg_model_generate_sensor_id(char * out, size_t n);
bool vg_model_sensor_id_is_unique(const char * id);
uint8_t vg_model_sensor_type_count(void);
const vg_sensor_type_template_t * vg_model_sensor_type_at(uint8_t index);
bool vg_model_save_sensor_type(const vg_sensor_type_template_t * type);

/* 系统状态 / OTA（C3） */
const vg_sys_status_t * vg_model_get_sys_status(void);
const vg_ota_t * vg_model_get_ota(void);
void vg_model_ota_accept(void);
void vg_model_ota_cancel(void);
void vg_model_ota_retry(void);

/* Discover / backend sync (Phase B) */
void vg_model_import_discover_slaves(const vg_ui_slave_t * slaves, int n);
void vg_model_import_mthings(void);

typedef struct {
    char id[VG_SENSOR_ID_MAX];
    char name[VG_SENSOR_NAME_MAX];
    uint8_t addr;
    uint8_t fc;
    uint16_t reg;
    char unit[8];
    char dtype[16];
    float scale;
    char cmp[4];
    uint8_t has_warn;
    uint8_t has_crit;
    float warn;
    float crit;
    uint8_t fail_n;
} vg_runtime_point_t;

void vg_model_import_runtime_points(const vg_runtime_point_t * pts, int n);
bool vg_model_set_live(uint16_t idx, float value, bool online);

#endif
