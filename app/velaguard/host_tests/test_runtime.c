/****************************************************************************
 * app/velaguard/host_tests/test_runtime.c
 *
 * Host tests for since-boot runtime accounting (vg_runtime + boot stats).
 ****************************************************************************/

#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "vg_frame_stats.h"
#include "vg_runtime.h"

void vg_mqtt_enqueue_alarm(const char *id, const char *kind,
                           const char *state, float value, float thr,
                           const char *level, long long ts_ms)
{
  (void)id;
  (void)kind;
  (void)state;
  (void)value;
  (void)thr;
  (void)level;
  (void)ts_ms;
}

static int g_fail;

static void expect(int cond, const char *msg)
{
  if (!cond)
    {
      fprintf(stderr, "FAIL: %s\n", msg);
      g_fail++;
    }
}

static void test_boot_survives_window(void)
{
  struct vg_fs_boot_summary boot;
  struct vg_fs_summary win;
  unsigned int i;

  vg_fs_reset(0);
  for (i = 0; i < VG_FS_WINDOW + 10; i++)
    {
      expect(vg_fs_inject(1, VG_FS_OK, 5) == 0, "inject ok");
    }

  expect(vg_fs_inject(1, VG_FS_CRC, 0) == 0, "inject crc");
  vg_fs_summary(1, &win);
  vg_fs_boot_summary(1, &boot);
  expect(win.total == VG_FS_WINDOW, "window capped");
  expect(boot.total == VG_FS_WINDOW + 11, "boot keeps all");
  expect(boot.crc_err == 1, "boot crc");
  expect(boot.ok == VG_FS_WINDOW + 10, "boot ok");
}

static void test_point_online_and_events(void)
{
  char buf[4096];
  int n;

  vg_runtime_init();
  vg_fs_reset(0);
  (void)vg_fs_inject(1, VG_FS_OK, 8);

  vg_runtime_note_sample("PT01", 1, true, 25.0f, VG_RUNTIME_KIND_NONE, 0,
                         "ge", NULL);
  usleep(20000);
  vg_runtime_note_sample("PT01", 1, true, 90.0f, VG_RUNTIME_KIND_THRESHOLD,
                         80.0f, "ge", "warn");
  vg_runtime_note_sample("PT01", 1, false, 90.0f, VG_RUNTIME_KIND_OFFLINE, 0,
                         "ge", "offline");
  vg_runtime_note_sample("PT01", 1, true, 70.0f, VG_RUNTIME_KIND_NONE, 80.0f,
                         "ge", NULL);

  n = vg_runtime_format_dump(buf, sizeof(buf));
  expect(n > 0, "dump non-empty");
  expect(strstr(buf, "vgruntime: uptime_s=") != NULL, "uptime line");
  expect(strstr(buf, "slave=1") != NULL, "slave boot line");
  expect(strstr(buf, "id=PT01") != NULL, "point line");
  expect(strstr(buf, "action=raise") != NULL, "raise event");
  expect(strstr(buf, "action=clear") != NULL, "clear event");
  expect(strstr(buf, "kind=threshold") != NULL, "threshold kind");
  expect(strstr(buf, "kind=offline") != NULL, "offline kind");
}

static void test_write_report(void)
{
  char path[] = "/tmp/vg_runtime_report.md";
  char buf[4096];
  FILE *fp;
  size_t n;

  expect(vg_runtime_write_report(path) == 0, "write report");
  fp = fopen(path, "r");
  expect(fp != NULL, "open written report");
  if (fp == NULL)
    {
      return;
    }

  n = fread(buf, 1, sizeof(buf) - 1, fp);
  fclose(fp);
  unlink(path);
  buf[n] = '\0';
  expect(strstr(buf, "通信质量") != NULL, "comm section");
  expect(strstr(buf, "点位在线") != NULL, "point section");
  expect(strstr(buf, "异常时间线") != NULL, "event section");
  expect(strstr(buf, "PT01") != NULL, "point id");
  expect(strstr(buf, "越限") != NULL, "threshold raise");
  expect(strstr(buf, "恢复在线") != NULL, "offline clear");
  expect(strstr(buf, "vgruntime: slave=") == NULL, "no dump lines");
}

/* The agent answers "运行报告" from the buffer form while the report page
 * falls back to the file form.  Both must say the same thing, so compare the
 * two renderings byte for byte rather than only checking section headers. */

static void test_buffer_matches_file(void)
{
  char path[] = "/tmp/vg_runtime_report_cmp.md";
  char fbuf[4096];
  char bbuf[4096];
  FILE *fp;
  size_t n;
  int written;

  expect(vg_runtime_write_report(path) == 0, "write report for compare");

  written = vg_runtime_format_report(bbuf, sizeof(bbuf));
  expect(written > 0, "buffer report non-empty");
  expect((size_t)written == strlen(bbuf), "returned length matches strlen");

  fp = fopen(path, "r");
  expect(fp != NULL, "open report for compare");
  if (fp == NULL)
    {
      return;
    }

  n = fread(fbuf, 1, sizeof(fbuf) - 1, fp);
  fclose(fp);
  unlink(path);
  fbuf[n] = '\0';

  expect(n == (size_t)written, "file and buffer agree on length");
  expect(strcmp(fbuf, bbuf) == 0, "file and buffer renderings are identical");

  expect(strstr(bbuf, "通信质量") != NULL, "buffer has comm section");
  expect(strstr(bbuf, "点位在线") != NULL, "buffer has point section");
  expect(strstr(bbuf, "异常时间线") != NULL, "buffer has event section");
}

static void test_format_report_bounds(void)
{
  char small[64];
  int n;
  size_t i;

  n = vg_runtime_format_report(small, sizeof(small));
  expect(n >= 0, "short buffer does not fail");
  expect(n <= (int)sizeof(small) - 1, "short buffer stays in bounds");

  for (i = 0; i < sizeof(small); i++)
    {
      if (small[i] == '\0')
        {
          break;
        }
    }

  expect(i < sizeof(small), "short buffer is NUL-terminated");

  expect(vg_runtime_format_report(NULL, sizeof(small)) == 0,
         "NULL out returns 0");
  expect(vg_runtime_format_report(small, 0) == 0, "zero cap returns 0");
}

int main(void)
{
  test_boot_survives_window();
  test_point_online_and_events();
  test_write_report();
  test_buffer_matches_file();
  test_format_report_bounds();

  if (g_fail != 0)
    {
      fprintf(stderr, "test_runtime: %d failure(s)\n", g_fail);
      return 1;
    }

  printf("test_runtime: OK\n");
  return 0;
}
