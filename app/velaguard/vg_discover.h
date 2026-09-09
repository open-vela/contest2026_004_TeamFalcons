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
  char     tag[24];
  char     dtype[16];
  float    scale;
  char     unit[8];
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

int vg_point_table_apply(FAR const struct vg_discover_summary *sum,
                         FAR const char *points_path,
                         FAR const char *config_basedir,
                         bool confirm);

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
