CC ?= gcc
CFLAGS ?= -Wall -Werror -I..

# vg_agent_tools.c is a NuttX source: it includes <nuttx/config.h> and relies
# on OK/ERROR from <sys/types.h>.  stubs/ supplies a stand-in config and
# vg_host_shim.h supplies the two constants; cJSON comes from the apps tree.
OPENVELA_ROOT ?= ../../../..
APPS_INC      := $(OPENVELA_ROOT)/apps/include
AI_AGENT_INC  := $(OPENVELA_ROOT)/packages/ai_agent/include
CJSON_SRC     := $(OPENVELA_ROOT)/apps/netutils/cjson/cJSON/cJSON.c
TEST_AGENT_TOOLS_CFLAGS := $(CFLAGS) -Istubs -include stubs/vg_host_shim.h \
                           -I$(APPS_INC) -I$(AI_AGENT_INC)

.PHONY: test clean

test_net_policy: test_net_policy.c ../vg_net_policy.c ../vg_net_policy.h
	$(CC) $(CFLAGS) -o $@ test_net_policy.c ../vg_net_policy.c

test_config_store: test_config_store.c ../vg_config_store.c ../vg_config_store.h
	$(CC) $(CFLAGS) -o $@ test_config_store.c ../vg_config_store.c

test_frame_stats: test_frame_stats.c ../vg_frame_stats.c ../vg_frame_stats.h
	$(CC) $(CFLAGS) -o $@ test_frame_stats.c ../vg_frame_stats.c

test_runtime: test_runtime.c ../vg_runtime.c ../vg_runtime.h ../vg_frame_stats.c ../vg_frame_stats.h
	$(CC) $(CFLAGS) -o $@ test_runtime.c ../vg_runtime.c ../vg_frame_stats.c

test_discover: test_discover.c ../vg_point_table.c ../vg_discover.h
	$(CC) $(CFLAGS) -o $@ test_discover.c ../vg_point_table.c

test_vgpoint: test_vgpoint.c ../vg_point_table.c ../vg_discover.h
	$(CC) $(CFLAGS) -o $@ test_vgpoint.c ../vg_point_table.c

test_alarm_eval: test_alarm_eval.c ../vg_alarm_eval.c ../vg_alarm_eval.h
	$(CC) $(CFLAGS) -o $@ test_alarm_eval.c ../vg_alarm_eval.c

test_time_sync: test_time_sync.c ../vg_sntp.c ../vg_sntp.h
	$(CC) $(CFLAGS) -o $@ test_time_sync.c ../vg_sntp.c

test_hmi_perf: test_hmi_perf.c ../vg_hmi_perf.c ../vg_hmi_perf.h
	$(CC) $(CFLAGS) -pthread -DVG_HMI_PERF_ENABLED=1 -o $@ test_hmi_perf.c ../vg_hmi_perf.c

test_runtime_render: test_runtime_render.c ../vg_point_table.c ../vg_discover.h
	$(CC) $(CFLAGS) -o $@ test_runtime_render.c ../vg_point_table.c

test_hmi_sched: test_hmi_sched.c ../vg_hmi_sched.h
	$(CC) $(CFLAGS) -o $@ test_hmi_sched.c

test_mqtt_payload: test_mqtt_payload.c ../vg_mqtt_payload.c ../vg_mqtt_payload.h \
                   ../vg_device_id.c ../vg_device_id.h
	$(CC) $(CFLAGS) -o $@ test_mqtt_payload.c ../vg_mqtt_payload.c \
	  ../vg_device_id.c

test_ai_contract: test_ai_contract.c ../vg_ai_contract.c ../vg_ai_contract.h
	$(CC) $(CFLAGS) -o $@ test_ai_contract.c ../vg_ai_contract.c

test_advice_policy: test_advice_policy.c ../vg_advice_policy.c \
                    ../vg_advice_policy.h ../vg_ai_contract.c ../vg_ai_contract.h
	$(CC) $(CFLAGS) -o $@ test_advice_policy.c ../vg_advice_policy.c \
	  ../vg_ai_contract.c

test_provision_creds: test_provision_creds.c ../vg_provision.c ../vg_provision.h
	$(CC) $(CFLAGS) -o $@ test_provision_creds.c ../vg_provision.c

test_agent_query: test_agent_query.c ../vg_agent_query.c ../vg_agent_query.h
	$(CC) $(CFLAGS) -o $@ test_agent_query.c ../vg_agent_query.c

test_agent_tools: test_agent_tools.c ../vg_agent_tools.c ../vg_agent_tools.h \
                  ../vg_agent_query.c ../vg_agent_query.h \
                  stubs/nuttx/config.h stubs/vg_host_shim.h $(CJSON_SRC)
	$(CC) $(TEST_AGENT_TOOLS_CFLAGS) -o $@ test_agent_tools.c \
	  ../vg_agent_tools.c ../vg_agent_query.c $(CJSON_SRC) -lm

test: test_net_policy test_config_store test_frame_stats test_runtime test_discover test_vgpoint test_alarm_eval test_time_sync test_hmi_perf test_runtime_render test_hmi_sched test_mqtt_payload test_ai_contract test_advice_policy test_provision_creds test_agent_query test_agent_tools
	./test_net_policy
	./test_config_store
	./test_frame_stats
	./test_runtime
	./test_discover
	./test_vgpoint
	./test_alarm_eval
	./test_time_sync
	./test_hmi_perf
	./test_runtime_render
	./test_hmi_sched
	./test_mqtt_payload
	./test_ai_contract
	./test_advice_policy
	./test_provision_creds
	./test_agent_query
	./test_agent_tools

clean:
	rm -f test_net_policy test_config_store test_frame_stats test_runtime test_discover test_vgpoint test_alarm_eval test_time_sync test_hmi_perf test_runtime_render test_hmi_sched test_mqtt_payload test_ai_contract test_advice_policy test_provision_creds test_agent_query test_agent_tools
