# Contest 2026 team 004 velaguard app
if(CONFIG_LVX_USE_DEMO_CONTEST2026_004_VELAGUARD_APP)
  nuttx_add_application(
    NAME velaguard
    SRCS velaguard.c
    STACKSIZE 2048)
endif()
