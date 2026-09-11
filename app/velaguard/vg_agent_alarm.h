#ifndef __APP_VELAGUARD_VG_AGENT_ALARM_H
#define __APP_VELAGUARD_VG_AGENT_ALARM_H

#ifdef CONFIG_VG_AGENT_OPS
int vg_pending_alarm_write(const char *body);
int vg_agent_alarm_start(void);
#else
static inline int vg_pending_alarm_write(const char *body)
{
  (void)body;
  return 0;
}
static inline int vg_agent_alarm_start(void) { return 0; }
#endif

#endif
