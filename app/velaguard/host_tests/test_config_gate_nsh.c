#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>

#include "../modbus_port_openvela.h"

#define main vgpoint_host_main
#include "../vgpoint.c"
#undef main

int vg_modbus_port_open(struct vg_modbus_port_s *port, const char *devpath)
{
  (void)port;
  (void)devpath;
  return -ENODEV;
}

void vg_modbus_port_close(struct vg_modbus_port_s *port)
{
  (void)port;
}

void vg_modbus_port_bind(struct vg_modbus_port_s *port,
                         nmbs_platform_conf *conf)
{
  (void)port;
  nmbs_platform_conf_create(conf);
}

static void cleanup(void)
{
  unlink("gate-nsh-candidate.json");
  unlink("gate-nsh-candidate.json.bak");
  unlink("gate-nsh-candidate.json.tmp");
  unlink("gate-nsh-candidate.json.btmp");
  unlink("gate-nsh-points.json");
  unlink("gate-nsh-points.json.bak");
  unlink("gate-nsh-points.json.tmp");
  unlink("gate-nsh-points.json.btmp");
}

int main(void)
{
  char *add[] =
  {
    "vgpoint", "add", "-i", "temp", "-a", "1", "-r", "0"
  };
  char *apply[] =
  {
    "vgpoint", "apply", "--confirm"
  };
  uint32_t token = 0;
  bool created = false;
  struct vg_discover_summary committed;

  cleanup();
  assert(cmd_add(8, add) == 0);
  assert(vg_config_gate_current() == VG_CONFIG_GATE_NSH);
  assert(vg_config_gate_acquire(VG_CONFIG_GATE_REMOTE, &token,
                                &created) == -EBUSY);
  assert(!created);
  assert(cmd_apply(3, apply) == 0);
  assert(vg_config_gate_is_idle());
  assert(vg_point_table_read(&committed, "gate-nsh-points.json") == 0);
  assert(committed.n_points == 1);
  cleanup();
  return 0;
}
