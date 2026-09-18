/****************************************************************************
 * app/velaguard/host_tests/test_provision_creds.c
 *
 * The two guards that keep a lost LLM credential from looking like a broken
 * feature.  Both were added after the 2026-09-17 board came back with no
 * backend: the page said "AI 建议不可用" while the credentials were simply
 * absent, and the restore path had reported success onto a store that was not
 * checkable at the time.
 ****************************************************************************/

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "../vg_provision.h"

/* vg_provision.c reaches for the sealer, the opener and the chip UID, none of
 * which exists off-target.  The predicates under test touch none of them, so
 * stub the three here rather than link the crypto translation unit - its
 * desktop branch carries an unused static that -Werror rejects.
 */

int vg_provision_crypto_seal(const uint8_t uid[VG_PROVISION_UID_LEN],
                             const uint8_t *plain, size_t plain_len,
                             uint8_t *out, size_t outsz, size_t *out_len)
{
  (void)uid; (void)plain; (void)plain_len;
  (void)out; (void)outsz; (void)out_len;
  return -ENOSYS;
}

int vg_provision_crypto_open(const uint8_t uid[VG_PROVISION_UID_LEN],
                             const uint8_t *blob, size_t blob_len,
                             uint8_t *plain, size_t plainsz,
                             size_t *plain_len)
{
  (void)uid; (void)blob; (void)blob_len;
  (void)plain; (void)plainsz; (void)plain_len;
  return -ENOSYS;
}

int vg_device_id_read_uid(uint8_t uid[VG_PROVISION_UID_LEN]);

int vg_device_id_read_uid(uint8_t uid[VG_PROVISION_UID_LEN])
{
  memset(uid, 0, VG_PROVISION_UID_LEN);
  return 0;
}

static int expect_true(int cond, const char *msg)
{
  if (!cond)
    {
      fprintf(stderr, "FAIL: %s\n", msg);
      return 1;
    }

  return 0;
}

int main(void)
{
  int fails = 0;

  /* The JSON the board actually writes when provisioning applied. */

  fails += expect_true(
      vg_provision_creds_ready_in(
          "{\"llm_host\":\"token-plan-cn.xiaomimimo.com\","
          "\"llm_path\":\"/v1/chat/completions\","
          "\"llm_port\":\"443\",\"model\":\"mimo-v2.5\","
          "\"api_key\":\"tp-abc123\"}"),
      "a provisioned config reads ready");

  /* The board state that produced the symptom: the file exists but carries no
   * LLM keys at all.  Reporting this as "ready" would keep the page blaming
   * the advice feature. */

  fails += expect_true(
      !vg_provision_creds_ready_in("{\"feishu_app_id\":\"x\"}"),
      "a config without LLM keys is not ready");

  /* Empty values, not absent ones.  This is what a masked write leaves behind,
   * and it is the subtler failure: the router gets a host and every call fails
   * instantly with no network traffic. */

  fails += expect_true(
      !vg_provision_creds_ready_in("{\"llm_host\":\"\",\"api_key\":\"tp-x\"}"),
      "an empty host is not ready");
  fails += expect_true(
      !vg_provision_creds_ready_in(
          "{\"llm_host\":\"h.example\",\"api_key\":\"\"}"),
      "an empty key is not ready");

  fails += expect_true(!vg_provision_creds_ready_in(""), "empty text is not ready");
  fails += expect_true(!vg_provision_creds_ready_in(NULL), "NULL is not ready");
  fails += expect_true(!vg_provision_creds_ready_in("{}"), "an empty object is not ready");

  /* Storage guard.  A board whose /data fell back to RAM must be refused.
   * /dev/shm is tmpfs here and /proc is the pseudo filesystem, so both rejected
   * magics are reachable off-target.  Note /tmp is NOT usable as the positive
   * case: on this host it is itself tmpfs (magic 0x1021994), so the working
   * directory stands in for the eMMC mount.
   */

  {
    struct
    {
      const char *path;
      int         want;
      const char *msg;
    } cases[] = {
      { ".",            0,       "a real directory is persistent" },
      { "/proc",         -ENODEV, "procfs is not a store" },
      { "/dev/shm",      -ENODEV, "tmpfs is not a store" },
      { "/no/such/dir",  -ENOENT, "a missing path reports itself" },
      { "",              -EINVAL, "an empty path is rejected" },
    };
    size_t i;

    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++)
      {
        int rc = vg_provision_store_is_persistent(cases[i].path);

        if (cases[i].want == 0)
          {
            fails += expect_true(rc == 0, cases[i].msg);
          }
        else
          {
            fails += expect_true(rc == cases[i].want, cases[i].msg);
          }
      }
  }

  /* The plain NULL guard, separate from the loop above because it is the one
   * caller error the sign of the return cannot express. */

  fails += expect_true(vg_provision_store_is_persistent(NULL) == -EINVAL,
                       "NULL path is rejected");

  if (fails == 0)
    {
      printf("test_provision_creds: OK\n");
    }
  else
    {
      printf("test_provision_creds: %d failure(s)\n", fails);
    }

  return (fails == 0) ? 0 : 1;
}
