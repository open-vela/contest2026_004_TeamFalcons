/****************************************************************************
 * app/velaguard/vg_voice_pref.c
 ****************************************************************************/

#include <nuttx/config.h>

#ifdef CONFIG_VG_VOICE_LOOPBACK

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <syslog.h>
#include <unistd.h>

#include "vg_voice_pref.h"
#include "vg_voice_round.h"

int vg_voice_pref_parse(const char *text, bool *muted)
{
  if (text == NULL || muted == NULL)
    {
      return -EINVAL;
    }

  if (strncmp(text, "muted=", 6) == 0)
    {
      if (text[6] == '0' || text[6] == '1')
        {
          *muted = (text[6] == '1');
          return 0;
        }
    }

  return -EINVAL;
}

int vg_voice_pref_load(bool *muted)
{
  char buf[32];
  FILE *f;
  size_t n;

  if (muted == NULL)
    {
      return -EINVAL;
    }

  *muted = false;

  f = fopen(VG_VOICE_PREF_PATH, "r");
  if (f == NULL)
    {
      return -errno;
    }

  n = fread(buf, 1, sizeof(buf) - 1, f);
  fclose(f);
  buf[n] = '\0';

  if (vg_voice_pref_parse(buf, muted) != 0)
    {
      /* A corrupt preference is not a reason to fail the boot; the default
       * is speech enabled, which is the visible state. */

      *muted = false;
      return -EINVAL;
    }

  return 0;
}

int vg_voice_pref_save(bool muted)
{
  char tmp[sizeof(VG_VOICE_PREF_PATH) + 8];
  char buf[16];
  int fd;
  int n;

  mkdir(VG_VOICE_DIR, 0700);

  snprintf(tmp, sizeof(tmp), "%s.tmp", VG_VOICE_PREF_PATH);

  fd = open(tmp, O_CREAT | O_TRUNC | O_WRONLY, 0600);
  if (fd < 0)
    {
      return -errno;
    }

  n = snprintf(buf, sizeof(buf), "muted=%d\n", muted ? 1 : 0);

  if (write(fd, buf, (size_t)n) != n)
    {
      int err = errno;

      close(fd);
      unlink(tmp);
      return -err;
    }

  if (fsync(fd) < 0)
    {
      int err = errno;

      close(fd);
      unlink(tmp);
      return -err;
    }

  if (close(fd) < 0)
    {
      unlink(tmp);
      return -EIO;
    }

  if (rename(tmp, VG_VOICE_PREF_PATH) < 0)
    {
      int err = errno;

      unlink(tmp);
      return -err;
    }

  return 0;
}

void vg_voice_pref_set_muted(bool muted)
{
  int ret;

  vg_voice_round_set_muted(muted);

  ret = vg_voice_pref_save(muted);
  if (ret != 0)
    {
      syslog(LOG_WARNING, "[vgvoice] mute preference not saved: %d\n", ret);
    }
}

void vg_voice_pref_sweep(void)
{
  /* The recording, the mono upload copy and the TTS intermediate all belong
   * to one round.  Anything still here at boot came from a crash. */

  unlink(VG_VC_CLIP_PATH);
  unlink(VG_VC_CLIP_MONO_PATH);
  unlink(VG_VC_TTS_PCM_PATH);
  unlink(VG_VC_TTS_WAV_PATH);
}

#endif /* CONFIG_VG_VOICE_LOOPBACK */
