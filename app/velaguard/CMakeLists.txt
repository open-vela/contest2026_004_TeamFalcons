# Contest 2026 team 004 velaguard app
# velaguard.c 注册为 NSH 命令 velaguard_app；同时其 main 经 Makefile
# -Dmain 重命名为 velaguard_app_main 作为系统入口（CONFIG_INIT_ENTRYPOINT），
# 主线程常驻并托管 NSH 线程（见 velaguard.c 头部注释）。
if(CONFIG_VG_BRINGUP_TOOLS)
  if(CONFIG_VG_NET_FAILOVER)
    nuttx_add_application(
      NAME velaguard
      SRCS velaguard.c vg_net_policy.c vg_eth.c vg_esp_bearer.c vg_tcp_transport.c vg_mqtt_session.c vg_net_mgr.c
      STACKSIZE 4096)
    nuttx_add_application(
      NAME vgnet
      SRCS vgnet.c
      STACKSIZE 4096)
  else()
    nuttx_add_application(
      NAME velaguard
      SRCS velaguard.c
      STACKSIZE 4096)
  endif()
  nuttx_add_application(
    NAME vgpwm
    SRCS velaguard_pwm.c
    STACKSIZE 2048)
  nuttx_add_application(
    NAME vgrs485
    SRCS velaguard_rs485.c
    STACKSIZE 2048)
  nuttx_add_application(
    NAME vgesp
    SRCS velaguard_esp.c
    STACKSIZE 2048)
  # vgmqtt：MQTT 调试工具（阶段 1 M2 里程碑，MQTT-C 发布 status/LWT）
  nuttx_add_application(
    NAME vgmqtt
    SRCS velaguard_mqtt.c
    STACKSIZE 4096)
  # vgmodbus：Modbus RTU 主站（阶段 2，nanoMODBUS + /dev/rs485）
  nuttx_add_application(
    NAME vgmodbus
    SRCS modbus_collector.c modbus_port_openvela.c nanomodbus/nanomodbus.c
    STACKSIZE 4096
    INCLUDE_DIRECTORIES ${CMAKE_CURRENT_LIST_DIR}/nanomodbus
    DEFINITIONS NMBS_SERVER_DISABLED)
endif()
