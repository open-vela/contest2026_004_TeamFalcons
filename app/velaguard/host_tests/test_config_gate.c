#include <assert.h>
#include <errno.h>
#include <stdint.h>

#include "../vg_config_gate.h"

int main(void)
{
  uint32_t nsh = 0;
  uint32_t joined = 0;
  uint32_t other = 0;
  uint32_t remote = 0;
  bool created = false;

  assert(vg_config_gate_is_idle());
  assert(vg_config_gate_acquire(VG_CONFIG_GATE_NSH, &nsh, &created) == 0);
  assert(created);
  assert(nsh != 0 && !vg_config_gate_is_idle());
  assert(vg_config_gate_current() == VG_CONFIG_GATE_NSH);
  assert(vg_config_gate_acquire(VG_CONFIG_GATE_NSH, &joined, &created) == 0);
  assert(!created);
  assert(joined == nsh);

  assert(vg_config_gate_acquire(VG_CONFIG_GATE_REMOTE, &remote,
                                &created) == -EBUSY);
  assert(!created);
  assert(remote == nsh);
  assert(vg_config_gate_acquire(VG_CONFIG_GATE_SCAN, &other,
                                &created) == -EBUSY);
  assert(vg_config_gate_acquire(VG_CONFIG_GATE_OTA, &other,
                                &created) == -EBUSY);

  assert(vg_config_gate_token(VG_CONFIG_GATE_NSH, &other) == 0);
  assert(other == nsh);
  assert(vg_config_gate_owns(VG_CONFIG_GATE_NSH, nsh));
  assert(!vg_config_gate_owns(VG_CONFIG_GATE_REMOTE, nsh));
  assert(vg_config_gate_release(VG_CONFIG_GATE_NSH, nsh + 1) == -ESTALE);
  assert(vg_config_gate_owns(VG_CONFIG_GATE_NSH, nsh));
  assert(vg_config_gate_release(VG_CONFIG_GATE_NSH, nsh) == 0);
  assert(vg_config_gate_is_idle());
  assert(vg_config_gate_release(VG_CONFIG_GATE_NSH, nsh) == -EALREADY);

  assert(vg_config_gate_acquire(VG_CONFIG_GATE_SCAN, &other, &created) == 0);
  assert(created);
  assert(vg_config_gate_acquire(VG_CONFIG_GATE_REMOTE, &remote,
                                &created) == -EBUSY);
  assert(vg_config_gate_release(VG_CONFIG_GATE_SCAN, other) == 0);

  assert(vg_config_gate_acquire(VG_CONFIG_GATE_OTA, &other, &created) == 0);
  assert(created);
  assert(!vg_config_gate_is_idle());
  assert(vg_config_gate_release(VG_CONFIG_GATE_OTA, other) == 0);
  assert(vg_config_gate_acquire((enum vg_config_gate_owner_e)99,
                                &other, &created) == -EINVAL);
  return 0;
}
