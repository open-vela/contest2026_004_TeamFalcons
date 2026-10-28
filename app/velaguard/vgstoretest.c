/* SPDX-License-Identifier: Apache-2.0 */
/* Persistent-storage diagnostic. Only touches diagnostics/store-probe.bin;
 * never formats a volume or writes point tables, reports or credentials. */
#include <nuttx/config.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "vg_discover.h"
#include "vg_file_store.h"

#define PROBE_DIR "/data/velaguard/diagnostics"
#define PROBE_PATH PROBE_DIR "/store-probe.bin"

static int check_file(const char *path, const unsigned char *want, size_t len)
{
  unsigned char buf[512];
  FILE *f = fopen(path, "r");
  if (!f) return -errno;
  size_t n = fread(buf, 1, sizeof(buf), f);
  int rc = n == len && !ferror(f) && !memcmp(buf, want, len) &&
           fgetc(f) == EOF ? 0 : -EIO;
  if (fclose(f) && rc == 0) rc = -EIO;
  return rc;
}

int main(int argc, char **argv)
{
  unsigned char pattern[512];
  unsigned char previous[512];
  long rounds = 50;
  char *end;
  int rc;
  uint32_t start = vg_live_now_ms();
  if (argc > 2) return 1;
  if (argc == 2)
    {
      errno = 0;
      rounds = strtol(argv[1], &end, 10);
      if (errno || end == argv[1] || *end || rounds < 1 || rounds > 10000)
        { puts("vgstoretest: ERR rounds=1..10000"); return 1; }
    }
  rc = vg_point_store_root_ok("/data");
  if (rc != 0) goto fail;
  if (mkdir(PROBE_DIR, 0700) && errno != EEXIST) { rc = -errno; goto fail; }
  for (long round = 0; round < rounds; round++)
    {
      for (size_t i = 0; i < sizeof(pattern); i++)
        pattern[i] = (unsigned char)((i * 37 + round * 13 + start) & 255);
      rc = vg_file_replace(PROBE_PATH, pattern, sizeof(pattern), 0600);
      if (rc == 0) rc = check_file(PROBE_PATH, pattern, sizeof(pattern));
      if (rc == 0 && round > 0)
        rc = check_file(PROBE_PATH ".bak", previous, sizeof(previous));
      if (rc != 0) goto fail;
      memcpy(previous, pattern, sizeof(pattern));
    }
  printf("vgstoretest: OK rounds=%ld bytes=%u elapsed_ms=%u\n", rounds,
         (unsigned)(rounds * sizeof(pattern)), (unsigned)(vg_live_now_ms() - start));
  return 0;
fail:
  printf("vgstoretest: ERR rc=%d elapsed_ms=%u\n", rc,
         (unsigned)(vg_live_now_ms() - start));
  return 1;
}
