/****************************************************************************
 * app/velaguard/vg_discover_modbus.c
 *
 * Shared nanoMODBUS session for discovery (internal).
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "vg_discover.h"
#include "modbus_port_openvela.h"
#include "nanomodbus.h"

#define VG_DISC_READ_TO_MS   500
#define VG_DISC_BYTE_TO_MS   20
#define VG_DISC_MAX_REGS     16
#define VG_DISC_SCAN_TO_MS   400

struct vg_disc_nmbs
{
  struct vg_modbus_port_s port;
  nmbs_t                  nmbs;
  bool                    ready;
};

static int vg_disc_nmbs_open(FAR struct vg_disc_nmbs *s, FAR const char *dev)
{
  nmbs_platform_conf pc;
  nmbs_error err;

  if (s == NULL || dev == NULL)
    {
      return -EINVAL;
    }

  memset(s, 0, sizeof(*s));
  s->port.fd = -1;

  if (vg_modbus_port_open(&s->port, dev) < 0)
    {
      return -errno;
    }

  vg_modbus_port_bind(&s->port, &pc);
  err = nmbs_client_create(&s->nmbs, &pc);
  if (err != NMBS_ERROR_NONE)
    {
      vg_modbus_port_close(&s->port);
      return -EIO;
    }

  nmbs_set_read_timeout(&s->nmbs, VG_DISC_READ_TO_MS);
  nmbs_set_byte_timeout(&s->nmbs, VG_DISC_BYTE_TO_MS);
  s->ready = true;
  return 0;
}

static void vg_disc_nmbs_close(FAR struct vg_disc_nmbs *s)
{
  if (s == NULL || !s->ready)
    {
      return;
    }

  vg_modbus_port_close(&s->port);
  s->ready = false;
}

static nmbs_error vg_disc_read_holding(FAR struct vg_disc_nmbs *s,
                                       uint8_t addr, uint16_t start,
                                       uint16_t qty, FAR uint16_t *regs)
{
  if (!s->ready || qty == 0 || qty > VG_DISC_MAX_REGS)
    {
      return NMBS_ERROR_INVALID_ARGUMENT;
    }

  nmbs_set_destination_rtu_address(&s->nmbs, addr);
  return nmbs_read_holding_registers(&s->nmbs, start, qty, regs);
}

static nmbs_error vg_disc_read_input(FAR struct vg_disc_nmbs *s,
                                     uint8_t addr, uint16_t start,
                                     uint16_t qty, FAR uint16_t *regs)
{
  if (!s->ready || qty == 0 || qty > VG_DISC_MAX_REGS)
    {
      return NMBS_ERROR_INVALID_ARGUMENT;
    }

  nmbs_set_destination_rtu_address(&s->nmbs, addr);
  return nmbs_read_input_registers(&s->nmbs, start, qty, regs);
}

/* Probe tries qty=16 first; fall back to 2/1 for narrow mock slaves. */
static nmbs_error vg_disc_probe_read_holding(FAR struct vg_disc_nmbs *s,
                                             uint8_t addr, uint16_t start,
                                             FAR uint16_t *regs,
                                             FAR uint16_t *qty_out)
{
  static const uint16_t try_qty[] =
    {
      VG_DISCOVER_PROBE_QTY, 2, 1
    };
  int i;

  for (i = 0; i < (int)(sizeof(try_qty) / sizeof(try_qty[0])); i++)
    {
      nmbs_error err;

      memset(regs, 0, sizeof(uint16_t) * try_qty[i]);
      err = vg_disc_read_holding(s, addr, start, try_qty[i], regs);
      if (err == NMBS_ERROR_NONE)
        {
          if (qty_out != NULL)
            {
              *qty_out = try_qty[i];
            }

          return err;
        }
    }

  if (qty_out != NULL)
    {
      *qty_out = 0;
    }

  return NMBS_ERROR_TRANSPORT;
}

/* Address scan: any FC03 response (incl. exception) proves the slave is alive.
 * velaguard.mthings: 32 slaves; addr=2 rejects reg0 with exception 02 then OK @reg2.
 */
static bool vg_disc_err_means_alive(nmbs_error err)
{
  return err == NMBS_ERROR_NONE ||
         err == NMBS_EXCEPTION_ILLEGAL_FUNCTION ||
         err == NMBS_EXCEPTION_ILLEGAL_DATA_ADDRESS ||
         err == NMBS_EXCEPTION_ILLEGAL_DATA_VALUE ||
         err == NMBS_EXCEPTION_SERVER_DEVICE_FAILURE;
}

static nmbs_error vg_disc_scan_probe_holding(FAR struct vg_disc_nmbs *s,
                                             uint8_t addr,
                                             FAR uint16_t *regs,
                                             FAR uint16_t *start_out)
{
  static const uint16_t try_start[] = { 0, 2, 1 };
  static const uint16_t try_qty[] = { 1, 2 };
  int i;
  int j;

  nmbs_set_read_timeout(&s->nmbs, VG_DISC_SCAN_TO_MS);

  for(i = 0; i < (int)(sizeof(try_start) / sizeof(try_start[0])); i++)
    {
      for(j = 0; j < (int)(sizeof(try_qty) / sizeof(try_qty[0])); j++)
        {
          nmbs_error err;

          err = vg_disc_read_holding(s, addr, try_start[i], try_qty[j], regs);
          if(vg_disc_err_means_alive(err))
            {
              if(start_out != NULL)
                {
                  *start_out = try_start[i];
                }

              nmbs_set_read_timeout(&s->nmbs, VG_DISC_READ_TO_MS);
              return NMBS_ERROR_NONE;
            }
        }
    }

  if(start_out != NULL)
    {
      *start_out = 0;
    }

  nmbs_set_read_timeout(&s->nmbs, VG_DISC_READ_TO_MS);
  return NMBS_ERROR_TRANSPORT;
}

static nmbs_error vg_disc_probe_read_input(FAR struct vg_disc_nmbs *s,
                                           uint8_t addr, uint16_t start,
                                           FAR uint16_t *regs,
                                           FAR uint16_t *qty_out)
{
  static const uint16_t try_qty[] =
    {
      VG_DISCOVER_PROBE_QTY, 2, 1
    };
  int i;

  for (i = 0; i < (int)(sizeof(try_qty) / sizeof(try_qty[0])); i++)
    {
      nmbs_error err;

      memset(regs, 0, sizeof(uint16_t) * try_qty[i]);
      err = vg_disc_read_input(s, addr, start, try_qty[i], regs);
      if (err == NMBS_ERROR_NONE)
        {
          if (qty_out != NULL)
            {
              *qty_out = try_qty[i];
            }

          return err;
        }
    }

  if (qty_out != NULL)
    {
      *qty_out = 0;
    }

  return NMBS_ERROR_TRANSPORT;
}

int vg_discover_test_read(FAR const char *devpath, int baud,
                          uint8_t addr, uint16_t reg, uint16_t qty,
                          FAR uint16_t *out)
{
  struct vg_disc_nmbs s;
  nmbs_error err;

  (void)baud;

  if (devpath == NULL || out == NULL || addr == 0)
    {
      return -EINVAL;
    }

  if (vg_disc_nmbs_open(&s, devpath) != 0)
    {
      return -errno;
    }

  memset(out, 0, sizeof(uint16_t) * qty);
  err = vg_disc_read_holding(&s, addr, reg, qty, out);
  vg_disc_nmbs_close(&s);

  return (err == NMBS_ERROR_NONE) ? 0 : -EIO;
}

int vg_discover_poll_holding(FAR const char *devpath, int baud,
                            const uint8_t *addr, const uint16_t *reg,
                            uint16_t *raw, uint8_t *ok_out, int n,
                            int inter_ms)
{
  struct vg_disc_nmbs s;
  int i;

  (void)baud;

  if (devpath == NULL || addr == NULL || reg == NULL ||
      raw == NULL || ok_out == NULL || n <= 0)
    {
      return -EINVAL;
    }

  if (vg_disc_nmbs_open(&s, devpath) != 0)
    {
      return -errno;
    }

  for (i = 0; i < n; i++)
    {
      nmbs_error err;

      raw[i] = 0;
      ok_out[i] = 0;
      if (addr[i] == 0)
        {
          continue;
        }

      err = vg_disc_read_holding(&s, addr[i], reg[i], 1, &raw[i]);
      if (err == NMBS_ERROR_NONE)
        {
          ok_out[i] = 1;
        }

      if (inter_ms > 0 && i + 1 < n)
        {
          usleep((useconds_t)inter_ms * 1000);
        }
    }

  vg_disc_nmbs_close(&s);
  return 0;
}

int vg_discover_poll_points(FAR const char *devpath, int baud,
                            FAR const struct vg_point_entry *pts, int n,
                            FAR uint16_t *raw, FAR uint8_t *ok_out,
                            int inter_ms)
{
  struct vg_disc_nmbs s;
  int i;

  (void)baud;

  if (devpath == NULL || pts == NULL || raw == NULL ||
      ok_out == NULL || n <= 0)
    {
      return -EINVAL;
    }

  if (vg_disc_nmbs_open(&s, devpath) != 0)
    {
      return -errno;
    }

  for (i = 0; i < n; i++)
    {
      nmbs_error err;
      uint16_t qty = pts[i].qty;
      uint16_t tmp[VG_DISC_MAX_REGS];

      raw[i] = 0;
      ok_out[i] = 0;
      if (pts[i].addr == 0)
        {
          continue;
        }

      if (qty < 1)
        {
          qty = 1;
        }

      if (qty > VG_DISC_MAX_REGS)
        {
          qty = VG_DISC_MAX_REGS;
        }

      if (pts[i].fc == 4)
        {
          err = vg_disc_read_input(&s, pts[i].addr, pts[i].reg, qty, tmp);
        }
      else
        {
          err = vg_disc_read_holding(&s, pts[i].addr, pts[i].reg, qty, tmp);
        }

      if (err == NMBS_ERROR_NONE)
        {
          raw[i] = tmp[0];
          ok_out[i] = 1;
        }

      if (inter_ms > 0 && i + 1 < n)
        {
          usleep((useconds_t)inter_ms * 1000);
        }
    }

  vg_disc_nmbs_close(&s);
  return 0;
}

#define VG_DISC_RETRY_INTER_MS 100

static bool vg_disc_hit_has(FAR const struct vg_discover_summary *sum,
                            uint8_t addr)
{
  int i;

  if(sum == NULL)
    {
      return false;
    }

  for(i = 0; i < sum->n_hits; i++)
    {
      if(sum->hits[i].alive && sum->hits[i].addr == addr)
        {
          return true;
        }
    }

  return false;
}

static int vg_bus_scan_pass(FAR struct vg_disc_nmbs *s,
                            FAR struct vg_discover_summary *sum,
                            int addr_min, int addr_max, int inter_ms,
                            bool skip_known)
{
  int addr;
  int n = 0;

  for(addr = addr_min; addr <= addr_max; addr++)
    {
      uint16_t regs[2];
      uint16_t probe_start;
      nmbs_error err;

      if(sum->n_hits >= VG_DISCOVER_MAX_SLAVES)
        {
          break;
        }

      if(skip_known && vg_disc_hit_has(sum, (uint8_t)addr))
        {
          continue;
        }

      sum->scan_addr = addr;
      err = vg_disc_scan_probe_holding(s, (uint8_t)addr, regs, &probe_start);
      if(err == NMBS_ERROR_NONE)
        {
          sum->hits[sum->n_hits].addr      = (uint8_t)addr;
          sum->hits[sum->n_hits].alive     = true;
          sum->hits[sum->n_hits].probe_reg = probe_start;
          sum->n_hits++;
          n++;
        }

      if(inter_ms > 0)
        {
          usleep((useconds_t)inter_ms * 1000);
        }
    }

  return n;
}

int vg_bus_scan(FAR struct vg_discover_summary *sum,
                FAR const char *devpath,
                int baud, int addr_min, int addr_max, int inter_ms)
{
  struct vg_disc_nmbs s;
  int n = 0;
  int retry_inter;

  if (sum == NULL || devpath == NULL || addr_min < 1 || addr_max > 247 ||
      addr_min > addr_max)
    {
      return -EINVAL;
    }

  if (vg_disc_nmbs_open(&s, devpath) != 0)
    {
      return -errno;
    }

  sum->baud = baud;
  snprintf(sum->devpath, sizeof(sum->devpath), "%s", devpath);
  sum->n_hits = 0;
  sum->scan_addr_min = addr_min;
  sum->scan_addr_max = addr_max;
  sum->scan_addr = 0;

  n = vg_bus_scan_pass(&s, sum, addr_min, addr_max, inter_ms, false);

  retry_inter = inter_ms > 0 ? inter_ms : VG_DISC_RETRY_INTER_MS;
  if(retry_inter < VG_DISC_RETRY_INTER_MS)
    {
      retry_inter = VG_DISC_RETRY_INTER_MS;
    }

  if(sum->n_hits < (addr_max - addr_min + 1))
    {
      n += vg_bus_scan_pass(&s, sum, addr_min, addr_max, retry_inter, true);
    }

  sum->scan_addr = 0;
  vg_disc_nmbs_close(&s);
  return n;
}

int vg_reg_probe_slave(FAR struct vg_discover_summary *sum,
                       FAR const char *devpath, int baud,
                       uint8_t addr, int fc_filter, int reg_max)
{
  struct vg_disc_nmbs s;
  int fail_streak = 0;
  uint16_t start;

  if (sum == NULL || devpath == NULL || addr == 0)
    {
      return -EINVAL;
    }

  if (reg_max <= 0)
    {
      reg_max = 120;
    }

  if (vg_disc_nmbs_open(&s, devpath) != 0)
    {
      return -errno;
    }

  sum->baud = baud;
  snprintf(sum->devpath, sizeof(sum->devpath), "%s", devpath);

  for (start = 0; start < (uint16_t)reg_max && fail_streak < 3; )
    {
      uint16_t regs[VG_DISCOVER_PROBE_QTY];
      uint16_t got_qty = 0;
      nmbs_error err;
      int do_holding = (fc_filter == 0 || fc_filter == 3);
      int do_input   = (fc_filter == 0 || fc_filter == 4);

      if (sum->n_blocks >= VG_DISCOVER_MAX_BLOCKS)
        {
          break;
        }

      memset(regs, 0, sizeof(regs));
      err = NMBS_ERROR_TRANSPORT;

      if (do_holding)
        {
          err = vg_disc_probe_read_holding(&s, addr, start, regs, &got_qty);
          if (err == NMBS_ERROR_NONE)
            {
              FAR struct vg_reg_block *b = &sum->blocks[sum->n_blocks];

              b->addr   = addr;
              b->fc     = 3;
              b->start  = start;
              b->count  = got_qty;
              memcpy(b->sample, regs, sizeof(b->sample));
              sum->n_blocks++;
              fail_streak = 0;
              start += VG_DISCOVER_BLOCK_STEP;
              continue;
            }
        }

      if (do_input)
        {
          err = vg_disc_probe_read_input(&s, addr, start, regs, &got_qty);
          if (err == NMBS_ERROR_NONE)
            {
              FAR struct vg_reg_block *b = &sum->blocks[sum->n_blocks];

              b->addr   = addr;
              b->fc     = 4;
              b->start  = start;
              b->count  = got_qty;
              memcpy(b->sample, regs, sizeof(b->sample));
              sum->n_blocks++;
              fail_streak = 0;
              start += VG_DISCOVER_BLOCK_STEP;
              continue;
            }
        }

      fail_streak++;
      start += VG_DISCOVER_BLOCK_STEP;
    }

  vg_disc_nmbs_close(&s);
  return sum->n_blocks;
}
