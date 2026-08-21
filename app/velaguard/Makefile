############################################################################
# Contest 2026 team 000 hello app - Makefile
#
# 注意：MAINSRC 与 PROGNAME 按位置一一对应（第 i 个 PROGNAME 对应第 i 个
# MAINSRC 的 main，经 -Dmain=xxx_main 重命名）。velaguard.c 注册为 NSH 命令
# velaguard_app，同时其 main 重命名为 velaguard_app_main 作为系统入口
# （CONFIG_INIT_ENTRYPOINT），主线程常驻并托管 NSH 线程（见 velaguard.c 头部注释）。
############################################################################

include $(APPDIR)/Make.defs

PROGNAME  = velaguard_app vgpwm vgrs485 vgesp vgmqtt vgmodbus
PRIORITY  = SCHED_PRIORITY_DEFAULT
STACKSIZE = 4096
MODULE    = $(CONFIG_VG_BRINGUP_TOOLS)

MAINSRC = velaguard.c velaguard_pwm.c velaguard_rs485.c velaguard_esp.c \
          velaguard_mqtt.c modbus_collector.c
CSRCS   = modbus_port_openvela.c nanomodbus.c

ifeq ($(CONFIG_VG_NET_FAILOVER),y)
PROGNAME += vgnet
MAINSRC  += vgnet.c
CSRCS    += vg_net_policy.c vg_eth.c vg_esp_bearer.c vg_tcp_transport.c \
            vg_mqtt_session.c vg_net_mgr.c
endif

# nanomodbus.c 用 #include "nanomodbus.h"；只留 client，裁掉 server。
CFLAGS += ${INCDIR_PREFIX}$(CURDIR)/nanomodbus
CFLAGS += -DNMBS_SERVER_DISABLED
VPATH += :nanomodbus
DEPPATH += --dep-path nanomodbus

include $(APPDIR)/Application.mk
