/****************************************************************************
 * app/velaguard/vg_ai_contract.h
 *
 * Board-side contract for text produced by the on-board AI agent
 * (OPENVELACLAW).  Everything the agent writes and the board later shows on
 * screen passes through here first: AGENTS.md V5 requires the check to live
 * in C, not in the prompt.
 *
 * Deliberately free of LVGL, cJSON and NuttX headers so the whole module
 * compiles under app/velaguard/host_tests and its bounds can be exercised
 * on the host.
 ****************************************************************************/

#ifndef __APP_VELAGUARD_VG_AI_CONTRACT_H
#define __APP_VELAGUARD_VG_AI_CONTRACT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Caps.  VG_AI_SUM_MAX is sized so "AI · " plus the text still fits the
 * 96-byte row label buffer in gui/main/ui/pages/vg_page_alarm.c. */

#define VG_AI_ID_MAX      24      /* VG_SENSOR_ID_MAX, plus NUL */
#define VG_AI_ADV_MAX     8       /* ALARM_LIST_MAX */
#define VG_AI_SUM_MAX     80
#define VG_AI_TEXT_MAX    200
#define VG_AI_DOC_MAX     2560    /* inside the 4 KB HMI stream buffer */
#define VG_AI_REPORT_MAX  1536    /* vg_ui_report_snapshot_t.body[], so a
                                   * report that passes never truncates on
                                   * screen; the skill is told 1400 for
                                   * margin */
#define VG_AI_BOOT_HEX    8       /* boot printed as 8 hex digits */

/* Distinct negative results so host tests can assert the exact failure. */

enum vg_ai_rc
{
  VG_AI_OK         = 0,
  VG_AI_ERR_ARG    = -1,   /* NULL / zero length / bad caller argument */
  VG_AI_ERR_FORMAT = -2,   /* malformed structure */
  VG_AI_ERR_RANGE  = -3,   /* number out of range, too many entries */
  VG_AI_ERR_KEY    = -4,   /* unknown or misplaced key */
  VG_AI_ERR_COUNT  = -5,   /* entry count / index mismatch */
  VG_AI_ERR_STALE  = -6,   /* boot or req does not match the live round */
  VG_AI_ERR_UTF8   = -7,   /* control byte or invalid UTF-8 sequence */
  VG_AI_ERR_DUP    = -8    /* duplicate point id or (id, epoch) */
};

/* Severity uses the same numbering as enum vg_alarm_kind. */

#define VG_AI_SEV_NONE    0
#define VG_AI_SEV_WARN    1
#define VG_AI_SEV_CRIT    2
#define VG_AI_SEV_OFFLINE 3

typedef struct
{
  char     id[VG_AI_ID_MAX];
  uint32_t epoch;
  uint8_t  sev;
  bool     unresolved;
  char     sum[VG_AI_SUM_MAX + 1];
  char     ev[VG_AI_TEXT_MAX + 1];
  char     att[VG_AI_TEXT_MAX + 1];
} vg_ai_advice_entry_t;

typedef struct
{
  uint32_t             boot;
  uint32_t             req;
  int                  n;
  vg_ai_advice_entry_t e[VG_AI_ADV_MAX];
} vg_ai_advice_doc_t;

/* One active alarm, as handed to the request builder. */

typedef struct
{
  const char *id;
  const char *name;
  uint32_t    epoch;
  uint8_t     sev;
  float       value;
  float       threshold;
  int32_t     dur_s;
} vg_ai_alarm_in_t;

/****************************************************************************
 * Parse a VGADV1 document.
 *
 * Any structural violation fails the whole document: *out is untouched and
 * a negative enum vg_ai_rc is returned.  Over-long text fields are the one
 * exception -- they are cut back to a UTF-8 character boundary so a chatty
 * model costs a word, not the whole round.
 *
 * expect_boot / expect_req are the values the board sent for this round;
 * a mismatch reports VG_AI_ERR_STALE.
 ****************************************************************************/

int vg_ai_advice_parse(const char *buf, size_t len,
                       uint32_t expect_boot, uint32_t expect_req,
                       vg_ai_advice_doc_t *out);

/* Look up one point in an already parsed document.  NULL when absent. */

const vg_ai_advice_entry_t *vg_ai_advice_find(const vg_ai_advice_doc_t *doc,
                                              const char *id, uint32_t epoch);

/****************************************************************************
 * Build the request text for one alarm-advice round.  Pure: no IO, no
 * allocation.  Returns the written length (excluding the terminator), or a
 * negative enum vg_ai_rc when the cap is too small.
 ****************************************************************************/

int vg_ai_advice_build_request(char *out, size_t cap,
                               uint32_t boot, uint32_t req,
                               const vg_ai_alarm_in_t *a, int n);

/****************************************************************************
 * Validate the agent-authored daily report.
 *
 * expect_date is "YYYY-MM-DD" for the local day the report claims to cover.
 * Pass mtime_s or now_s as 0 to skip the freshness check (host tests).
 ****************************************************************************/

int vg_ai_report_validate(const char *buf, size_t len,
                          const char *expect_date,
                          long mtime_s, long now_s);

/****************************************************************************
 * Small helpers shared with the board side.
 ****************************************************************************/

/* 1 when id matches the point-table charset: [A-Za-z0-9_], 1..23 bytes. */

int vg_ai_id_valid(const char *id);

/* 1 when the buffer is valid UTF-8 with no C0 control byte other than the
 * given separator.  Used for both the document and the report body. */

int vg_ai_utf8_valid(const char *buf, size_t len, char allow);

#endif /* __APP_VELAGUARD_VG_AI_CONTRACT_H */
