/****************************************************************************
 * app/velaguard/vgdiscover.c
 *
 * NSH: Modbus bus discovery @ 9600 — scan, probe, dump, test-read, apply.
 ****************************************************************************/

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <nuttx/config.h>

#include "vg_discover.h"

#ifndef CONFIG_VG_DISCOVER_DEVPATH
#  define CONFIG_VG_DISCOVER_DEVPATH "/dev/rs485"
#endif

#ifndef CONFIG_VG_DISCOVER_BAUD
#  define CONFIG_VG_DISCOVER_BAUD 9600
#endif

#ifndef CONFIG_VG_DISCOVER_ADDR_MIN
#  define CONFIG_VG_DISCOVER_ADDR_MIN 1
#endif

#ifndef CONFIG_VG_DISCOVER_ADDR_MAX
#  define CONFIG_VG_DISCOVER_ADDR_MAX 32
#endif

#ifndef CONFIG_VG_DISCOVER_INTER_MS
#  define CONFIG_VG_DISCOVER_INTER_MS 5
#endif

#ifndef CONFIG_VG_DISCOVER_REG_MAX
#  define CONFIG_VG_DISCOVER_REG_MAX 120
#endif

#ifndef CONFIG_VG_DISCOVER_CANDIDATE_PATH
#  define CONFIG_VG_DISCOVER_CANDIDATE_PATH \
          "/data/velaguard/discover/point_table_candidate.json"
#endif

#ifndef CONFIG_VG_DISCOVER_POINTS_PATH
#  define CONFIG_VG_DISCOVER_POINTS_PATH "/data/velaguard/config/points.json"
#endif

#ifndef CONFIG_VG_CONFIG_BASEDIR
#  define CONFIG_VG_CONFIG_BASEDIR "/data/velaguard/config"
#endif

static void usage(FAR const char *prog)
{
  printf("Usage:\n");
  printf("  %s scan [-a min-max]     # fixed %d baud\n", prog,
         CONFIG_VG_DISCOVER_BAUD);
  printf("  %s probe -a <addr> [-t 3|4|both]\n", prog);
  printf("  %s dump [-o path]\n", prog);
  printf("  %s test-read -a <addr> -r <reg> [-c qty]\n", prog);
  printf("  %s apply [--confirm]\n", prog);
}

static int parse_opt_u8(int argc, char *argv[], int start,
                        FAR const char *flag, FAR uint8_t *out)
{
  int i;

  for (i = start; i < argc; i++)
    {
      if (strcmp(argv[i], flag) == 0)
        {
          if (i + 1 >= argc) return -EINVAL;
          char *end;
          errno = 0;
          long v = strtol(argv[i + 1], &end, 0);

          if (end != argv[i + 1] && *end == 0 && errno == 0 && v >= 1 && v <= 247)
            {
              *out = (uint8_t)v;
              return 0;
            }

          return -EINVAL;
        }
    }

  return -ENOENT;
}

static int parse_opt_u16(int argc, char *argv[], int start,
                         FAR const char *flag, FAR uint16_t *out)
{
  int i;

  for (i = start; i < argc; i++)
    {
      if (strcmp(argv[i], flag) == 0)
        {
          if (i + 1 >= argc) return -EINVAL;
          char *end;
          errno = 0;
          long v = strtol(argv[i + 1], &end, 0);

          if (end != argv[i + 1] && *end == 0 && errno == 0 && v >= 0 && v <= 65535)
            {
              *out = (uint16_t)v;
              return 0;
            }

          return -EINVAL;
        }
    }

  return -ENOENT;
}

int main(int argc, char *argv[])
{
  struct vg_discover_summary local;
  FAR struct vg_discover_summary *sum = &local;
  FAR const char *dev = CONFIG_VG_DISCOVER_DEVPATH;
  int ret;
  int i;

  if (argc < 2)
    {
      usage(argv[0]);
      return 1;
    }

  vg_discover_reset(sum);

  if (strcmp(argv[1], "scan") == 0)
    {
      int amin = CONFIG_VG_DISCOVER_ADDR_MIN;
      int amax = CONFIG_VG_DISCOVER_ADDR_MAX;

      if (argc >= 4 && strcmp(argv[2], "-a") == 0)
        {
          if (!vg_discover_parse_addr_range(argv[3], &amin, &amax))
            {
              fprintf(stderr, "vgdiscover: bad -a range\n");
              return 1;
            }
        }

      printf("vgdiscover: scan %s @%d addr %d..%d\n",
             dev, CONFIG_VG_DISCOVER_BAUD, amin, amax);

      if (vg_bus_lock_timeout(1000) != 0) { fprintf(stderr, "vgdiscover: bus busy\n"); return 1; }
      ret = vg_bus_scan(sum, dev, CONFIG_VG_DISCOVER_BAUD,
                        amin, amax, CONFIG_VG_DISCOVER_INTER_MS);
      vg_bus_unlock();
      if (ret < 0)
        {
          fprintf(stderr, "vgdiscover: scan failed %d\n", ret);
          return 1;
        }

      printf("vgdiscover: found %d slave(s):\n", sum->n_hits);
      for (i = 0; i < sum->n_hits; i++)
        {
          printf("  addr=%u\n", (unsigned)sum->hits[i].addr);
        }

      if (vg_discover_state_save(sum, NULL) != 0)
        {
          fprintf(stderr, "vgdiscover: state save failed\n");
        }

      return (sum->n_hits > 0) ? 0 : 1;
    }

  if (strcmp(argv[1], "probe") == 0)
    {
      uint8_t addr = 0;
      int fc = 0;
      int ai;

      for (ai = 2; ai < argc; ai++)
        {
          if (strcmp(argv[ai], "-t") == 0 && ai + 1 < argc)
            {
              ai++;
              if (strcmp(argv[ai], "3") == 0)
                {
                  fc = 3;
                }
              else if (strcmp(argv[ai], "4") == 0)
                {
                  fc = 4;
                }
              else if (strcmp(argv[ai], "both") == 0)
                {
                  fc = 0;
                }
              else
                {
                  fprintf(stderr, "vgdiscover: -t must be 3, 4, or both\n");
                  return 1;
                }
            }
        }

      if (parse_opt_u8(argc, argv, 2, "-a", &addr) != 0)
        {
          fprintf(stderr, "vgdiscover: probe requires -a <addr>\n");
          return 1;
        }

      (void)vg_discover_state_load(sum, NULL);
      sum->n_blocks = 0;

      if (vg_bus_lock_timeout(1000) != 0) { fprintf(stderr, "vgdiscover: bus busy\n"); return 1; }
      ret = vg_reg_probe_slave(sum, dev, CONFIG_VG_DISCOVER_BAUD,
                               addr, fc, CONFIG_VG_DISCOVER_REG_MAX);
      vg_bus_unlock();
      if (ret < 0)
        {
          fprintf(stderr, "vgdiscover: probe failed %d\n", ret);
          return 1;
        }

      printf("vgdiscover: probe addr=%u blocks=%d\n",
             (unsigned)addr, sum->n_blocks);
      for (i = 0; i < sum->n_blocks; i++)
        {
          FAR struct vg_reg_block *b = &sum->blocks[i];

          printf("  fc=%u start=%u count=%u sample[0]=%u sample[1]=%u\n",
                 (unsigned)b->fc, (unsigned)b->start, (unsigned)b->count,
                 (unsigned)b->sample[0], (unsigned)b->sample[1]);
        }

      (void)vg_discover_state_save(sum, NULL);
      return (sum->n_blocks > 0) ? 0 : 1;
    }

  if (strcmp(argv[1], "dump") == 0)
    {
      FAR const char *out = CONFIG_VG_DISCOVER_CANDIDATE_PATH;

      if (argc >= 4 && strcmp(argv[2], "-o") == 0)
        {
          out = argv[3];
        }

      if (vg_discover_state_load(sum, NULL) != 0)
        {
          fprintf(stderr, "vgdiscover: no saved state — run scan/probe first\n");
          return 1;
        }

      if (access(out, F_OK) == 0) { fprintf(stderr, "vgdiscover: candidate exists; inspect or abort it first\n"); return 1; }
      vg_point_table_infer(sum);
      if (sum->n_points == 0) { fprintf(stderr, "vgdiscover: no inferred points\n"); return 1; }
      if (sum->flags) fprintf(stderr, "vgdiscover: partial/capacity-limited results flags=%u\n", (unsigned)sum->flags);
      ret = vg_point_table_create_candidate(sum, out);
      if (ret != 0)
        {
          fprintf(stderr, "vgdiscover: dump failed %d\n", ret);
          return 1;
        }

      printf("vgdiscover: wrote %d points → %s\n", sum->n_points, out);
      (void)vg_discover_state_save(sum, NULL);
      return 0;
    }

  if (strcmp(argv[1], "test-read") == 0)
    {
      uint8_t addr = 0;
      uint16_t reg = 0;
      uint16_t qty = 2;
      uint16_t vals[16];
      unsigned int j;

      if (parse_opt_u8(argc, argv, 2, "-a", &addr) != 0)
        {
          fprintf(stderr, "vgdiscover: test-read requires -a <addr>\n");
          return 1;
        }

      if (parse_opt_u16(argc, argv, 2, "-r", &reg) != 0) { fprintf(stderr, "vgdiscover: invalid -r\n"); return 1; }
      ret = parse_opt_u16(argc, argv, 2, "-c", &qty);
      if (ret != 0 && ret != -ENOENT) { fprintf(stderr, "vgdiscover: invalid -c\n"); return 1; }

      if (qty == 0 || qty > 16 || (uint32_t)reg + qty > 65536) { fprintf(stderr, "vgdiscover: invalid quantity\n"); return 1; }

      if (vg_bus_lock_timeout(1000) != 0) { fprintf(stderr, "vgdiscover: bus busy\n"); return 1; }
      ret = vg_discover_test_read(dev, CONFIG_VG_DISCOVER_BAUD,
                                  addr, reg, qty, vals);
      vg_bus_unlock();
      if (ret != 0)
        {
          fprintf(stderr, "vgdiscover: test-read failed %d\n", ret);
          return 1;
        }

      printf("vgdiscover: addr=%u reg=%u:", (unsigned)addr, (unsigned)reg);
      for (j = 0; j < qty; j++)
        {
          float s = vg_discover_decode_int16_scaled((int16_t)vals[j], 0.1f);

          printf(" [%u]=%u (%.1f)", (unsigned)(reg + j),
                 (unsigned)vals[j], (double)s);
        }

      printf("\n");
      return 0;
    }

  if (strcmp(argv[1], "apply") == 0)
    {
      bool confirm = false;

      for (i = 2; i < argc; i++)
        {
          if (strcmp(argv[i], "--confirm") == 0)
            {
              confirm = true;
            }
        }

      if (vg_point_table_read(sum, CONFIG_VG_DISCOVER_CANDIDATE_PATH) != 0)
        { fprintf(stderr, "vgdiscover: no valid candidate; run dump first\n"); return 1; }

      ret = vg_point_table_apply(sum, CONFIG_VG_DISCOVER_POINTS_PATH,
                                 CONFIG_VG_CONFIG_BASEDIR, confirm);
      return (ret == 0) ? 0 : 1;
    }

  usage(argv[0]);
  return 1;
}
