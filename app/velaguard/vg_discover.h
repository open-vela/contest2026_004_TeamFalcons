/****************************************************************************
 * app/velaguard/vg_discover.h
 *
 * Modbus bus discovery @ fixed 9600: address scan, register probe, point table.
 ****************************************************************************/

#ifndef __APP_VELAGUARD_VG_DISCOVER_H
#define __APP_VELAGUARD_VG_DISCOVER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifndef FAR
#  define FAR
#endif

#define VG_DISCOVER_MAX_SLAVES   32
#define VG_DISCOVER_MAX_BLOCKS   16
#define VG_DISCOVER_MAX_POINTS   32
#define VG_DISCOVER_BLOCK_STEP   16
#define VG_DISCOVER_PROBE_QTY    16
#define VG_POINT_ID_MAX          24
#define VG_POINT_NAME_MAX        48

struct vg_scan_hit
{
  uint8_t  addr;
  bool     alive;
  uint16_t probe_reg; /* FC03 start that answered */
};

struct vg_reg_block
{
  uint8_t  addr;
  uint8_t  fc;
  uint16_t start;
  uint16_t count;
  uint16_t sample[4];
};

struct vg_point_entry
{
  uint8_t  addr;
  uint8_t  fc;
  uint16_t reg;
  uint16_t qty;
  char     id[VG_POINT_ID_MAX];
  char     name[VG_POINT_NAME_MAX];
  char     dtype[16];
  float    scale;
  char     unit[8];
  char     cmp[4];      /* ge / le / eq, or empty */
  uint8_t  has_warn;
  uint8_t  has_crit;
  float    warn;
  float    crit;
  uint8_t  fail_n;      /* 1..20, default 3 */
};

struct vg_discover_summary
{
  int      baud;
  char     devpath[64];
  int      scan_addr;     /* address being probed (0 when idle) */
  int      scan_addr_min;
  int      scan_addr_max;
  int      n_hits;
  struct vg_scan_hit hits[VG_DISCOVER_MAX_SLAVES];
  int      n_blocks;
  struct vg_reg_block blocks[VG_DISCOVER_MAX_BLOCKS];
  int      n_points;
  struct vg_point_entry points[VG_DISCOVER_MAX_POINTS];
};

void vg_discover_reset(struct vg_discover_summary *sum);

int vg_bus_scan(FAR struct vg_discover_summary *sum,
                FAR const char *devpath,
                int baud, int addr_min, int addr_max, int inter_ms);

int vg_reg_probe_slave(FAR struct vg_discover_summary *sum,
                       FAR const char *devpath, int baud,
                       uint8_t addr, int fc_filter, int reg_max);

int vg_point_table_infer(FAR struct vg_discover_summary *sum);

int vg_point_table_write_candidate(FAR const struct vg_discover_summary *sum,
                                   FAR const char *path);

int vg_point_table_read(FAR struct vg_discover_summary *sum,
                        FAR const char *path);

int vg_point_table_find_id(FAR const struct vg_discover_summary *sum,
                           FAR const char *id);

int vg_point_table_ensure_candidate(FAR struct vg_discover_summary *sum,
                                    FAR const char *cand_path,
                                    FAR const char *committed_path);

int vg_point_table_apply(FAR const struct vg_discover_summary *sum,
                         FAR const char *points_path,
                         FAR const char *config_basedir,
                         bool confirm);

int vg_bus_try_lock(void);
void vg_bus_unlock(void);
int vg_bus_is_locked(void);

int vg_live_points_load(FAR const char *path);
int vg_live_points_replace(FAR const struct vg_discover_summary *sum);
uint32_t vg_live_points_gen(void);
int vg_live_points_copy(FAR struct vg_discover_summary *out);

int vg_point_validate_id(FAR const char *id);
int vg_point_validate_name(FAR const char *name);
int vg_point_cmdline_len(int argc, char *argv[]);
int vg_point_format_point(FAR char *buf, size_t bufsz,
                          FAR const struct vg_point_entry *p);
int vg_point_format_ok(FAR char *buf, size_t bufsz,
                       FAR const char *cmd, FAR const char *table, int n);
int vg_point_format_err(FAR char *buf, size_t bufsz,
                        FAR const char *cmd, FAR const char *code,
                        FAR const char *msg);

/* Unique slave addrs from a committed points.json (hits[] first, else
 * unique point "addr" fields). Returns count, 0 if missing/empty, <0 on I/O. */
int vg_point_table_read_slaves(FAR const char *path, uint8_t *addrs, int max);

int vg_discover_test_read(FAR const char *devpath, int baud,
                          uint8_t addr, uint16_t reg, uint16_t qty,
                          FAR uint16_t *out);

/* One open, then FC03 qty=1 for each (addr,reg). ok_out[i]=1 on success. */
int vg_discover_poll_holding(FAR const char *devpath, int baud,
                            const uint8_t *addr, const uint16_t *reg,
                            uint16_t *raw, uint8_t *ok_out, int n,
                            int inter_ms);

int vg_discover_poll_points(FAR const char *devpath, int baud,
                            FAR const struct vg_point_entry *pts, int n,
                            FAR uint16_t *raw, FAR uint8_t *ok_out,
                            int inter_ms);

int vg_discover_state_save(FAR const struct vg_discover_summary *sum,
                           FAR const char *path);

int vg_discover_state_load(FAR struct vg_discover_summary *sum,
                           FAR const char *path);

struct vg_discover_summary *vg_discover_state(void);

/* Host-testable decode helpers */

float vg_discover_decode_int16_scaled(int16_t raw, float scale);
bool  vg_discover_parse_addr_range(FAR const char *spec,
                                   int *min_out, int *max_out);

#endif /* __APP_VELAGUARD_VG_DISCOVER_H */
