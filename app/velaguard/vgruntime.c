/****************************************************************************
 * app/velaguard/vgruntime.c — NSH dump for since-boot runtime report inputs
 ****************************************************************************/

#include <stdio.h>
#include <string.h>

#include "vg_runtime.h"

int main(int argc, char *argv[])
{
  vg_runtime_init();

  if (argc >= 2 && strcmp(argv[1], "dump") == 0)
    {
      vg_runtime_fprint_dump(stdout);
      return 0;
    }

  if (argc >= 2 && strcmp(argv[1], "report") == 0)
    {
      const char *path = "/data/velaguard/reports/runtime-report.md";
      int rc;

      if (argc >= 3 && argv[2][0] != '\0')
        {
          path = argv[2];
        }

      rc = vg_runtime_write_report(path);
      if (rc != 0)
        {
          printf("vgruntime: write %s failed (%d)\n", path, rc);
          return 1;
        }

      printf("vgruntime: wrote %s\n", path);
      vg_runtime_fprint_report(stdout);
      return 0;
    }

  printf("Usage: vgruntime dump | report [path]\n");
  return 1;
}
