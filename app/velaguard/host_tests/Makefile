CC ?= gcc
CFLAGS ?= -Wall -Werror -I..
CFLAGS += -pthread -I$(APPS_INC)

# vg_agent_tools.c is a NuttX source: it includes <nuttx/config.h> and relies
# on OK/ERROR from <sys/types.h>.  stubs/ supplies a stand-in config and
# vg_host_shim.h supplies the two constants; cJSON comes from the apps tree.
OPENVELA_ROOT ?= ../../../..
APPS_INC      := $(OPENVELA_ROOT)/apps/include
AI_AGENT_INC  := $(OPENVELA_ROOT)/packages/ai_agent/include
CJSON_SRC     := $(OPENVELA_ROOT)/apps/netutils/cjson/cJSON/cJSON.c
TEST_AGENT_TOOLS_CFLAGS := $(CFLAGS) -Istubs -include stubs/vg_host_shim.h \
                           -DVG_AGENT_TOOLS_HOST_TEST=1 \
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

test_runtime_anomaly: test_runtime_anomaly.c ../vg_runtime.c ../vg_runtime.h ../vg_frame_stats.c ../vg_frame_stats.h
	$(CC) $(CFLAGS) -o $@ test_runtime_anomaly.c ../vg_runtime.c ../vg_frame_stats.c

test_discover: test_discover.c ../vg_point_table.c ../vg_discover.h ../vg_file_store.c $(CJSON_SRC)
	$(CC) $(CFLAGS) -o $@ test_discover.c ../vg_point_table.c ../vg_file_store.c $(CJSON_SRC) -lm

test_vgpoint: test_vgpoint.c ../vg_point_table.c ../vg_discover.h ../vg_file_store.c $(CJSON_SRC)
	$(CC) $(CFLAGS) -o $@ test_vgpoint.c ../vg_point_table.c ../vg_file_store.c $(CJSON_SRC) -lm

test_alarm_eval: test_alarm_eval.c ../vg_alarm_eval.c ../vg_alarm_eval.h
	$(CC) $(CFLAGS) -o $@ test_alarm_eval.c ../vg_alarm_eval.c

test_time_sync: test_time_sync.c ../vg_sntp.c ../vg_sntp.h
	$(CC) $(CFLAGS) -o $@ test_time_sync.c ../vg_sntp.c

test_hmi_perf: test_hmi_perf.c ../vg_hmi_perf.c ../vg_hmi_perf.h
	$(CC) $(CFLAGS) -pthread -DVG_HMI_PERF_ENABLED=1 -o $@ test_hmi_perf.c ../vg_hmi_perf.c

test_runtime_render: test_runtime_render.c ../vg_point_table.c ../vg_discover.h ../vg_file_store.c $(CJSON_SRC)
	$(CC) $(CFLAGS) -o $@ test_runtime_render.c ../vg_point_table.c ../vg_file_store.c $(CJSON_SRC) -lm

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

test_provision_creds: test_provision_creds.c ../vg_provision.c ../vg_provision.h ../vg_file_store.c
	$(CC) $(CFLAGS) -o $@ test_provision_creds.c ../vg_provision.c ../vg_file_store.c

test_agent_query: test_agent_query.c ../vg_agent_query.c ../vg_agent_query.h
	$(CC) $(CFLAGS) -o $@ test_agent_query.c ../vg_agent_query.c

test_voice_pref: test_voice_pref.c ../vg_voice_pref.c ../vg_voice_pref.h \
                stubs/nuttx/config.h stubs/vg_host_shim.h
	$(CC) $(CFLAGS) -Istubs -include stubs/vg_host_shim.h \
	  -DCONFIG_VG_VOICE_LOOPBACK=1 \
	  -DVG_VOICE_DIR='"/tmp/vg-voice-pref-test"' \
	  -o $@ test_voice_pref.c ../vg_voice_pref.c

test_agent_tools: test_agent_tools.c ../vg_agent_tools.c ../vg_agent_tools.h \
                  ../vg_agent_query.c ../vg_agent_query.h \
                  ../vg_voice_facts.h \
                  stubs/nuttx/config.h stubs/vg_host_shim.h $(CJSON_SRC)
	$(CC) $(TEST_AGENT_TOOLS_CFLAGS) -o $@ test_agent_tools.c \
	  ../vg_agent_tools.c ../vg_agent_query.c $(CJSON_SRC) -lm

test_pdm_filter: test_pdm_filter.c ../vg_pdm_filter.c ../vg_pdm_filter.h \
                 ../vg_pdm_filter_coeffs.h pdm_golden.h
	$(CC) $(CFLAGS) -o $@ test_pdm_filter.c ../vg_pdm_filter.c

test_ptt: test_ptt.c ../vg_ptt.c ../vg_ptt.h
	$(CC) $(CFLAGS) -o $@ test_ptt.c ../vg_ptt.c

test_ptt_cycles: test_ptt_cycles.c ../vg_ptt.c ../vg_ptt.h
	$(CC) $(CFLAGS) -o $@ test_ptt_cycles.c ../vg_ptt.c

test: test_tls_stream_upload test_tls_voice_session test_agent_timing test_live_memory test_tool_audit test_runtime_concurrent test_tls_send_pressure test_tls_pool test_context_cache test_voice_cloud test_dns_control test_llm_runtime_config test_voice_scope test_voice_facts test_net_policy test_config_store test_frame_stats test_runtime test_runtime_anomaly test_voice_session test_voice_intent test_voice_answer test_voice_pref test_discover test_vgpoint test_alarm_eval test_time_sync test_hmi_perf test_runtime_render test_hmi_sched test_mqtt_payload test_ai_contract test_advice_policy test_provision_creds test_agent_query test_agent_tools test_maintenance test_file_store test_shell_runner test_local_client test_file_policy test_discover_io test_agent_round test_shell_output test_modbus_port test_point_commit test_mqtt_session test_ptt test_ptt_cycles test_pdm_filter
	./test_net_policy
	./test_config_store
	./test_frame_stats
	./test_runtime
	./test_runtime_anomaly
	./test_voice_session
	./test_voice_intent
	python3 ../../../scripts/voice_test/dns_fixture.py --run ./test_dns_control
	./test_tls_stream_upload
	./test_tls_voice_session
	./test_agent_timing
	./test_live_memory
	./test_tool_audit
	./test_runtime_concurrent
	./test_tls_send_pressure
	./test_tls_pool
	./test_context_cache
	./test_voice_cloud
	./test_llm_runtime_config
	./test_voice_scope
	./test_voice_facts
	./test_voice_answer
	./test_voice_pref
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
	./test_maintenance
	./test_file_store
	./test_shell_runner
	./test_local_client
	./test_file_policy
	./test_discover_io
	./test_agent_round
	./test_shell_output
	./test_modbus_port
	./test_point_commit
	./test_mqtt_session
	./test_ptt
	./test_ptt_cycles
	./test_pdm_filter
	python3 test_boot_stack.py
	python3 test_board_pins.py

clean:
	rm -f test_dns_control
	rm -f test_tls_stream_upload test_tls_voice_session test_agent_timing test_live_memory test_tool_audit
	rm -f test_runtime_concurrent
	rm -f test_tls_send_pressure test_tls_pool
	rm -f test_context_cache
	rm -f test_voice_cloud
	rm -f test_llm_runtime_config
	rm -f test_voice_scope
	rm -f test_voice_facts
	rm -f test_modbus_port test_point_commit test_mqtt_session test_ptt test_ptt_cycles
	rm -f test_voice_intent test_voice_answer test_voice_pref test_shell_output test_agent_round test_discover_io test_file_policy test_maintenance test_file_store test_shell_runner test_local_client test_net_policy test_config_store test_frame_stats test_runtime test_runtime_anomaly test_voice_session test_voice_intent test_voice_answer test_discover test_vgpoint test_alarm_eval test_time_sync test_hmi_perf test_runtime_render test_hmi_sched test_mqtt_payload test_ai_contract test_advice_policy test_provision_creds test_agent_query test_agent_tools

test_maintenance: test_maintenance.c ../vg_point_table.c ../vg_file_store.c $(CJSON_SRC)
	$(CC) $(CFLAGS) -o $@ $^ -lm


AI_AGENT_ROOT := $(OPENVELA_ROOT)/packages/ai_agent

test_file_store: test_file_store.c ../vg_file_store.c ../vg_file_store.h
	$(CC) $(CFLAGS) -o $@ test_file_store.c ../vg_file_store.c -Wl,--wrap=write,--wrap=read,--wrap=fsync,--wrap=rename

test_shell_runner: $(AI_AGENT_ROOT)/tests/test_shell_runner.c $(AI_AGENT_ROOT)/src/tools/tool_shell_runner.c
	$(CC) $(CFLAGS) -o $@ $^

test_local_client: $(AI_AGENT_ROOT)/tests/test_local_client.c $(AI_AGENT_ROOT)/src/sdk/velaclaw_client_local.c
	$(CC) $(CFLAGS) -DOK=0 -DERROR=-1 -I$(AI_AGENT_ROOT)/include -I$(AI_AGENT_ROOT)/src -o $@ $^


test_file_policy: $(AI_AGENT_ROOT)/tests/test_file_policy.c $(AI_AGENT_ROOT)/src/tools/tool_files.c $(CJSON_SRC)
	$(CC) $(CFLAGS) -DOK=0 -DERROR=-1 '-DCONFIG_EXAMPLES_AI_AGENT_VELA_DATA_DIR="data/agent"' '-DVG_DATA_ROOT="data/velaguard"' -I$(AI_AGENT_ROOT)/tests/stub -I$(AI_AGENT_ROOT)/include -I$(AI_AGENT_ROOT)/src -I$(APPS_INC)/../netutils/cjson/cJSON -o $@ $^ -lm


test_discover_io: test_discover_io.c ../vg_discover_modbus.c ../nanomodbus/nanomodbus.c ../vg_frame_stats.c
	$(CC) $(CFLAGS) -Istubs -I../nanomodbus -o $@ $^

test_modbus_port: test_modbus_port.c ../modbus_port_openvela.c ../nanomodbus/nanomodbus.c
	$(CC) $(CFLAGS) -Istubs -include stubs/vg_host_shim.h -o $@ $^ -Wl,--wrap=poll,--wrap=read,--wrap=write,--wrap=tcdrain,--wrap=tcflush

test_point_commit: test_point_commit.c ../vg_point_table.c ../vg_file_store.c $(CJSON_SRC)
	$(CC) $(CFLAGS) -o $@ $^ -Wl,--wrap=vg_file_replace -lm

test_mqtt_session: test_mqtt_session.c ../vg_mqtt_session.c ../vg_mqtt_payload.c
	$(CC) $(CFLAGS) -Istubs -include stubs/vg_host_shim.h -I$(OPENVELA_ROOT)/apps/netutils/mqttc/MQTT-C/include -ffunction-sections -fdata-sections -o $@ test_mqtt_session.c ../vg_mqtt_payload.c -Wl,--gc-sections -lm


test_agent_round: test_agent_round.c ../vg_agent_round.c ../vg_agent_round.h
	$(CC) $(CFLAGS) -Istubs -I$(AI_AGENT_INC) -I$(AI_AGENT_ROOT)/src -o $@ test_agent_round.c ../vg_agent_round.c -Wl,--wrap=clock_gettime


test_shell_output: $(AI_AGENT_ROOT)/tests/test_shell_output.c $(AI_AGENT_ROOT)/src/tools/tool_shell.c $(AI_AGENT_ROOT)/src/tools/tool_shell_runner.c $(CJSON_SRC)
	$(CC) $(CFLAGS) -DOK=0 -DERROR=-1 -DCONFIG_VG_HMI -I$(AI_AGENT_ROOT)/tests/stub -I$(AI_AGENT_ROOT)/include -I$(AI_AGENT_ROOT)/src -I$(APPS_INC)/../netutils/cjson/cJSON -o $@ $(AI_AGENT_ROOT)/tests/test_shell_output.c $(AI_AGENT_ROOT)/src/tools/tool_shell_runner.c $(CJSON_SRC) -lm

test_voice_session: test_voice_session.c ../vg_voice_session.c ../vg_voice_session.h
	$(CC) $(CFLAGS) -o $@ test_voice_session.c ../vg_voice_session.c

test_voice_intent: test_voice_intent.c ../vg_voice_intent.c ../vg_voice_intent.h \
                   ../vg_agent_query.c ../vg_agent_query.h
	$(CC) $(CFLAGS) -o $@ test_voice_intent.c ../vg_voice_intent.c \
	  ../vg_agent_query.c

test_voice_answer: test_voice_answer.c ../vg_voice_answer.c ../vg_voice_answer.h \
                   ../vg_voice_facts.h ../vg_runtime.h ../vg_agent_query.h
	$(CC) $(CFLAGS) -o $@ test_voice_answer.c ../vg_voice_answer.c


test_voice_facts: test_voice_facts.c ../vg_voice_facts.c ../vg_voice_facts.h
	$(CC) $(CFLAGS) -o $@ test_voice_facts.c ../vg_voice_facts.c


test_voice_scope: $(AI_AGENT_ROOT)/tests/test_voice_scope.c $(AI_AGENT_ROOT)/src/tools/voice_scope.c $(AI_AGENT_ROOT)/src/llm/llm_parse.c $(CJSON_SRC)
	$(CC) $(CFLAGS) -DCONFIG_VG_HMI -ffunction-sections -fdata-sections -include $(AI_AGENT_ROOT)/tests/stub/host_compat.h -I$(AI_AGENT_ROOT)/tests/stub -I$(AI_AGENT_ROOT)/src -I$(AI_AGENT_ROOT)/include -I$(APPS_INC)/../netutils/cjson/cJSON -o $@ $^ -Wl,--gc-sections -lm


test_llm_runtime_config: $(AI_AGENT_ROOT)/tests/test_llm_runtime_config.c $(AI_AGENT_ROOT)/src/llm/llm_proxy.c
	$(CC) $(CFLAGS) -ffunction-sections -fdata-sections -include $(AI_AGENT_ROOT)/tests/stub/host_compat.h -I$(AI_AGENT_ROOT)/tests/stub -I$(AI_AGENT_ROOT)/src -I$(AI_AGENT_ROOT)/include -I$(APPS_INC)/../netutils/cjson/cJSON -o $@ $^ -Wl,--gc-sections


test_dns_control: test_dns_control.c $(OPENVELA_ROOT)/nuttx/libs/libc/netdb/lib_dnsquery.c $(OPENVELA_ROOT)/nuttx/include/nuttx/net/dns.h
	$(CC) $(CFLAGS) -Idns_stubs -I$(OPENVELA_ROOT)/nuttx/libs/libc -idirafter $(OPENVELA_ROOT)/nuttx/include -o $@ test_dns_control.c $(OPENVELA_ROOT)/nuttx/libs/libc/netdb/lib_dnsquery.c


test_voice_cloud: test_voice_cloud.c ../vg_voice_cloud.c ../vg_voice_pcm.c $(CJSON_SRC)
	$(CC) $(CFLAGS) -Istubs -include stubs/vg_host_shim.h -DCONFIG_VG_VOICE_LOOPBACK=1 -DCONFIG_EXAMPLES_AI_AGENT_VELA=1 -I$(AI_AGENT_INC) -I$(AI_AGENT_ROOT)/src -DVG_VOICE_DIR='"/tmp/vg-cloud-test-dir"' -o $@ $^ -lm


test_context_cache: $(AI_AGENT_ROOT)/tests/test_context_cache.c $(AI_AGENT_ROOT)/src/core/context_builder.c $(CJSON_SRC)
	$(CC) $(CFLAGS) -ffunction-sections -fdata-sections -include $(AI_AGENT_ROOT)/tests/stub/host_compat.h -I$(AI_AGENT_ROOT)/tests/stub -I$(AI_AGENT_ROOT)/src -I$(AI_AGENT_ROOT)/include -I$(APPS_INC)/../netutils/cjson/cJSON -DCONTEXT_BODY_REFRESH_MS=30 -DCONFIG_EXAMPLES_AI_AGENT_VELA_DATA_DIR='"/tmp/vg-context-cache-test-data"' -o $@ $^ -Wl,--gc-sections -lm


test_tls_send_pressure: $(AI_AGENT_ROOT)/tests/test_tls_send_pressure.c $(AI_AGENT_ROOT)/src/infra/vela_tls.c
	$(CC) $(CFLAGS) -ffunction-sections -fdata-sections -include $(AI_AGENT_ROOT)/tests/stub/host_compat.h -I$(AI_AGENT_ROOT)/tests/stub -I$(AI_AGENT_ROOT)/src -I$(AI_AGENT_ROOT)/include -I$(APPS_INC)/../crypto/mbedtls/mbedtls/include -o $@ $< -Wl,--gc-sections


test_runtime_concurrent: test_runtime_concurrent.c ../vg_runtime.c ../vg_frame_stats.c
	$(CC) $(CFLAGS) -o $@ $^


test_live_memory: test_live_memory.c ../vg_point_table.c ../vg_file_store.c $(CJSON_SRC)
	$(CC) $(CFLAGS) -o $@ $^ -lm

test_tool_audit: $(AI_AGENT_ROOT)/tests/test_tool_audit.c $(AI_AGENT_ROOT)/src/tools/tool_audit.c $(AI_AGENT_ROOT)/src/tools/tool_guard.c $(CJSON_SRC)
	$(CC) $(CFLAGS) -DCONFIG_VG_HMI -ffunction-sections -fdata-sections -include $(AI_AGENT_ROOT)/tests/stub/host_compat.h -I$(AI_AGENT_ROOT)/tests/stub -I$(AI_AGENT_ROOT)/src -I$(AI_AGENT_ROOT)/include -I$(APPS_INC)/../netutils/cjson/cJSON -o $@ $< $(AI_AGENT_ROOT)/src/tools/tool_guard.c $(CJSON_SRC) -Wl,--gc-sections -lm


test_tls_voice_session: $(AI_AGENT_ROOT)/tests/test_tls_voice_session.c $(AI_AGENT_ROOT)/src/infra/vela_tls.c
	$(CC) $(CFLAGS) -DCONFIG_VG_HMI -DCONFIG_VG_VOICE_TLS_CACHE -ffunction-sections -fdata-sections -include $(AI_AGENT_ROOT)/tests/stub/host_compat.h -I$(AI_AGENT_ROOT)/tests/stub -I$(AI_AGENT_ROOT)/src -I$(AI_AGENT_ROOT)/include -I$(APPS_INC)/../crypto/mbedtls/mbedtls/include -o $@ $< -Wl,--gc-sections


test_tls_stream_upload: $(AI_AGENT_ROOT)/tests/test_tls_stream_upload.c $(AI_AGENT_ROOT)/src/infra/vela_tls.c $(AI_AGENT_ROOT)/src/infra/agent_timing.c
	$(CC) $(CFLAGS) -ffunction-sections -fdata-sections -include $(AI_AGENT_ROOT)/tests/stub/host_compat.h -I$(AI_AGENT_ROOT)/tests/stub -I$(AI_AGENT_ROOT)/src -I$(AI_AGENT_ROOT)/include -I$(APPS_INC)/../crypto/mbedtls/mbedtls/include -o $@ $< -Wl,--gc-sections

test_agent_timing: $(AI_AGENT_ROOT)/tests/test_agent_timing.c $(AI_AGENT_ROOT)/src/infra/agent_timing.c
	$(CC) $(CFLAGS) -include $(AI_AGENT_ROOT)/tests/stub/host_compat.h -I$(AI_AGENT_ROOT)/tests/stub -I$(AI_AGENT_ROOT)/src -I$(AI_AGENT_ROOT)/include -I$(APPS_INC)/../crypto/mbedtls/mbedtls/include -o $@ $< $(AI_AGENT_ROOT)/src/infra/agent_timing.c



test_tls_pool: $(AI_AGENT_ROOT)/tests/test_tls_pool.c $(AI_AGENT_ROOT)/src/infra/vela_tls.c
	$(CC) $(CFLAGS) -ffunction-sections -fdata-sections -include $(AI_AGENT_ROOT)/tests/stub/host_compat.h -I$(AI_AGENT_ROOT)/tests/stub -I$(AI_AGENT_ROOT)/src -I$(AI_AGENT_ROOT)/include -I$(APPS_INC)/../crypto/mbedtls/mbedtls/include -o $@ $< -Wl,--gc-sections
