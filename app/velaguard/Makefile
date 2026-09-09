############################################################################
# Contest 2026 team 000 hello app - Makefile
#
# MAINSRC 与 PROGNAME 按位置一一对应。velaguard.c 为 net/min 系统入口；
# vghmi 为 LVGL HMI（Phase B，CONFIG_VG_HMI）。
#
# STACKSIZE / PRIORITY 必须与 PROGNAME 等长（Application.mk 按 word 对齐；
# 词数不足时余下程序继承 lastword）。合并 net+HMI 时不能把 49K 套到
# vgcfg 等工具上。
############################################################################

include $(APPDIR)/Make.defs

PROGNAME  =
MAINSRC   =
CSRCS     =
MODULE    =

ifeq ($(CONFIG_VG_BRINGUP_TOOLS),y)
PROGNAME  += velaguard_app vgpwm vgrs485 vgesp vgmqtt vgmodbus
MAINSRC   += velaguard.c velaguard_pwm.c velaguard_rs485.c velaguard_esp.c \
             velaguard_mqtt.c modbus_collector.c
CSRCS     += modbus_port_openvela.c nanomodbus.c
MODULE    = $(CONFIG_VG_BRINGUP_TOOLS)

ifeq ($(CONFIG_VG_CONFIG_STORE),y)
PROGNAME += vgcfg
MAINSRC  += vgcfg.c
CSRCS    += vg_config_store.c
endif

ifeq ($(CONFIG_VG_FRAME_STATS),y)
PROGNAME += vgstats
MAINSRC  += vgstats.c
CSRCS    += vg_frame_stats.c
CFLAGS   += -DCONFIG_VG_FRAME_STATS=1
endif

ifeq ($(CONFIG_VG_NET_FAILOVER),y)
PROGNAME += vgnet
MAINSRC  += vgnet.c
CSRCS    += vg_net_policy.c vg_eth.c vg_esp_bearer.c vg_tcp_transport.c \
            vg_mqtt_session.c vg_net_mgr.c
endif

ifeq ($(CONFIG_EXAMPLES_AI_AGENT_VELA),y)
CSRCS += vg_agent_seed.c vg_provision.c vg_provision_crypto.c
PROGNAME += vgprovision
MAINSRC += vgprovision.c
endif

ifeq ($(CONFIG_VG_AGENT_OPS),y)
CSRCS += vg_agent_alarm.c vg_modbus_read.c
CFLAGS += -DCONFIG_VG_AGENT_OPS=1
endif

ifeq ($(CONFIG_VG_BUS_DISCOVER),y)
PROGNAME += vgdiscover
MAINSRC  += vgdiscover.c
CSRCS    += vg_discover_modbus.c vg_point_table.c
CFLAGS   += -DCONFIG_VG_BUS_DISCOVER=1
endif

CFLAGS += ${INCDIR_PREFIX}$(CURDIR)/nanomodbus
CFLAGS += -DNMBS_SERVER_DISABLED
VPATH += :nanomodbus
DEPPATH += --dep-path nanomodbus
endif

ifeq ($(CONFIG_VG_HMI),y)
PROGNAME += vghmi
MAINSRC  += vg_hmi.c

# Same sources as gui/CMakeLists.txt (Phase A); do not duplicate under app/velaguard.
# Use basenames + VPATH so PREFIX objects stay flat under build/velaguard/
# (absolute CSRCS would write .o next to gui sources).
VG_CONTEST_ROOT := $(abspath $(CURDIR)/../..)
VG_UI_DIR       := $(VG_CONTEST_ROOT)/gui/main/ui
VG_UI_SUBDIRS   := app shell pages model theme widgets fonts
VG_UI_CSRCS     := $(foreach _d,$(VG_UI_SUBDIRS),$(wildcard $(VG_UI_DIR)/$(_d)/*.c))
VG_UI_CSRCS     := $(filter-out $(VG_UI_DIR)/model/vg_ui_backend.c,$(VG_UI_CSRCS))

CSRCS    += $(notdir $(VG_UI_CSRCS))
CSRCS    += vg_ui_backend_board.c
VPATH    += $(foreach _d,$(VG_UI_SUBDIRS),:$(VG_UI_DIR)/$(_d))
DEPPATH  += $(foreach _d,$(VG_UI_SUBDIRS),--dep-path $(VG_UI_DIR)/$(_d))

# HMI-only discover: skip objects already linked by VG_BRINGUP_TOOLS / VG_BUS_DISCOVER.
ifeq ($(CONFIG_VG_HMI_DISCOVER),y)
ifneq ($(CONFIG_VG_BUS_DISCOVER),y)
CSRCS += vg_discover_modbus.c vg_point_table.c
endif
ifneq ($(CONFIG_VG_BRINGUP_TOOLS),y)
PROGNAME += vgscan
MAINSRC  += vgscan.c
CSRCS += modbus_port_openvela.c nanomodbus.c
VPATH += :nanomodbus
DEPPATH += --dep-path nanomodbus
CFLAGS += ${INCDIR_PREFIX}$(CURDIR)/nanomodbus
CFLAGS += -DNMBS_SERVER_DISABLED
endif
endif

MODULE    = $(CONFIG_VG_HMI)
CFLAGS   += -DCONFIG_VG_HMI=1 -DVG_HMI_BOARD=1
CFLAGS   += ${INCDIR_PREFIX}$(VG_UI_DIR)
CFLAGS   += ${INCDIR_PREFIX}$(VG_CONTEST_ROOT)/gui/main/inc
CFLAGS   += ${INCDIR_PREFIX}$(CURDIR)
endif

VG_STACK_DEFAULT := 4096
VG_HMI_STACK := $(if $(CONFIG_VG_HMI_STACKSIZE),$(CONFIG_VG_HMI_STACKSIZE),49152)
VG_HMI_PRIO  := $(if $(CONFIG_VG_HMI_PRIORITY),$(CONFIG_VG_HMI_PRIORITY),100)

STACKSIZE := $(foreach p,$(PROGNAME),$(strip \
  $(if $(filter vghmi,$(p)),$(VG_HMI_STACK), \
  $(if $(filter vgdiscover vgscan,$(p)),8192,$(VG_STACK_DEFAULT)))))
PRIORITY  := $(foreach p,$(PROGNAME),$(strip \
  $(if $(filter vghmi,$(p)),$(VG_HMI_PRIO),SCHED_PRIORITY_DEFAULT)))

# Contest app is symlinked into packages/demos; without PREFIX, NuttX writes
# .o / .built / Make.dep into the contest source tree (and gui/ via absolute CSRCS).
# Application.mk: PREFIX must exist and end with $(DELIM).
VG_OBJDIR := $(abspath $(CURDIR)/../../build/velaguard)
$(shell mkdir -p "$(VG_OBJDIR)")
PREFIX := $(VG_OBJDIR)$(DELIM)

include $(APPDIR)/Application.mk

# Application.mk appends "--obj-path .". Replace that pair only — do not
# filter-out the word "." or "--dep-path ." loses its argument and mkdep
# treats the next token (our objdir) as the compiler.
DEPPATH := $(subst --obj-path .,--obj-path $(VG_OBJDIR),$(DEPPATH))

# Application.mk clean/distclean still delete unprefixed markers; also clear PREFIX.
clean::
	$(call DELFILE,$(PREFIX).built)
	$(Q) rm -f $(PREFIX)*$(OBJEXT)

distclean::
	$(call DELFILE,$(PREFIX)Make.dep)
	$(call DELFILE,$(PREFIX).depend)
