#include <assert.h>
#include "tools/tool_shell.c"
int main(void)
{
  const char *blocked[]={"vgota confirm", "vgota control start", "vgota provision commit",
     "vgota fixture arm", "vgota reboot", "/bin/vgota confirm", "sh -c vg ota", "echo x; vg ota", "vgota status"};
  for(unsigned i=0;i<sizeof(blocked)/sizeof(blocked[0]);i++) {
    char json[256],out[512];
    assert(is_blocked(blocked[i]));
    snprintf(json,sizeof(json),"{\"command\":\"%s\"}",blocked[i]);
    assert(tool_run_shell_execute(json,out,sizeof(out))==ERROR);
    assert(strstr(out,"blocked for safety"));
  }
  puts("test_ota_agent_gate: OK (actual C allowlist blocks OTA and shell wrappers)");
  return 0;
}
