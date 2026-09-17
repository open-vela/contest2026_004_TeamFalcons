CC ?= gcc
CFLAGS ?= -Wall -Werror -I..

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

test: test_net_policy test_config_store test_frame_stats test_runtime test_discover test_vgpoint test_alarm_eval test_time_sync test_hmi_perf test_runtime_render test_hmi_sched test_mqtt_payload test_ai_contract
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

clean:
	rm -f test_net_policy test_config_store test_frame_stats test_runtime test_discover test_vgpoint test_alarm_eval test_time_sync test_hmi_perf test_runtime_render test_hmi_sched test_mqtt_payload test_ai_contract
