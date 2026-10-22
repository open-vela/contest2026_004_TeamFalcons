/****************************************************************************
 * host_tests/test_voice_pref.c
 *
 * BVO-13: the speech preference and the file lifecycle around it.
 *
 * The path is compiled to a temporary directory (see the Makefile), so this
 * exercises the real parse, atomic save, load and boot sweep rather than a
 * mock.  Nothing here proves the eMMC survives a power cut; that needs the
 * board.
 ****************************************************************************/

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "vg_voice_pref.h"

static int g_failures;
static bool g_round_muted;

#define CHECK(cond)                                                     \
  do                                                                    \
    {                                                                   \
      if (!(cond))                                                      \
        {                                                               \
          printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);        \
          g_failures++;                                                 \
        }                                                               \
    }                                                                   \
  while (0)

/* The round owns the in-memory switch; this test only needs to observe that
 * the preference layer forwards the value. */

void vg_voice_round_set_muted(bool muted)
{
  g_round_muted = muted;
}

static void reset_dir(void)
{
  unlink(VG_VOICE_PREF_PATH);
  unlink(VG_VC_CLIP_PATH);
  unlink(VG_VC_CLIP_MONO_PATH);
  unlink(VG_VC_TTS_PCM_PATH);
  unlink(VG_VC_TTS_WAV_PATH);
  rmdir(VG_VOICE_DIR);
}

static void write_raw(const char *text)
{
  FILE *f;

  mkdir(VG_VOICE_DIR, 0700);
  f = fopen(VG_VOICE_PREF_PATH, "w");
  if (f != NULL)
    {
      fputs(text, f);
      fclose(f);
    }
}

static void test_parse(void)
{
  bool muted = true;

  CHECK(vg_voice_pref_parse("muted=0\n", &muted) == 0);
  CHECK(muted == false);
  CHECK(vg_voice_pref_parse("muted=1\n", &muted) == 0);
  CHECK(muted == true);

  CHECK(vg_voice_pref_parse("muted=2", &muted) == -EINVAL);
  CHECK(vg_voice_pref_parse("muted=", &muted) == -EINVAL);
  CHECK(vg_voice_pref_parse("", &muted) == -EINVAL);
  CHECK(vg_voice_pref_parse("MUTED=1", &muted) == -EINVAL);
  CHECK(vg_voice_pref_parse(NULL, &muted) == -EINVAL);
  CHECK(vg_voice_pref_parse("muted=1", NULL) == -EINVAL);
}

static void test_roundtrip(void)
{
  bool muted = true;

  reset_dir();

  CHECK(vg_voice_pref_save(true) == 0);
  CHECK(vg_voice_pref_load(&muted) == 0);
  CHECK(muted == true);

  CHECK(vg_voice_pref_save(false) == 0);
  muted = true;
  CHECK(vg_voice_pref_load(&muted) == 0);
  CHECK(muted == false);

  /* No temporary file is left behind. */

  CHECK(access(VG_VOICE_PREF_PATH ".tmp", F_OK) != 0);
}

static void test_missing_and_corrupt(void)
{
  bool muted = true;

  reset_dir();
  CHECK(vg_voice_pref_load(&muted) == -ENOENT);
  CHECK(muted == false);

  write_raw("garbage");
  muted = true;
  CHECK(vg_voice_pref_load(&muted) == -EINVAL);
  CHECK(muted == false);
}

static void test_set_muted_forwards_and_persists(void)
{
  bool muted = false;

  reset_dir();

  g_round_muted = false;
  vg_voice_pref_set_muted(true);
  CHECK(g_round_muted == true);
  CHECK(vg_voice_pref_load(&muted) == 0);
  CHECK(muted == true);
}

static void test_sweep(void)
{
  static const char *files[] =
    {
      VG_VC_CLIP_PATH, VG_VC_CLIP_MONO_PATH,
      VG_VC_TTS_PCM_PATH, VG_VC_TTS_WAV_PATH
    };
  size_t i;

  reset_dir();
  mkdir(VG_VOICE_DIR, 0700);

  for (i = 0; i < sizeof(files) / sizeof(files[0]); i++)
    {
      {
        FILE *f = fopen(files[i], "w");
        CHECK(f != NULL);
        if (f != NULL)
          {
            fputs("stale", f);
            fclose(f);
          }
      }
    }

  vg_voice_pref_sweep();

  for (i = 0; i < sizeof(files) / sizeof(files[0]); i++)
    {
      CHECK(access(files[i], F_OK) != 0);
    }
}

int main(void)
{
  test_parse();
  test_roundtrip();
  test_missing_and_corrupt();
  test_set_muted_forwards_and_persists();
  test_sweep();

  reset_dir();

  if (g_failures != 0)
    {
      printf("test_voice_pref: %d failure(s)\n", g_failures);
      return 1;
    }

  printf("test_voice_pref: ok\n");
  return 0;
}
