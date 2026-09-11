CC ?= gcc
CFLAGS ?= -Wall -Werror -I..

.PHONY: test clean

test_net_policy: test_net_policy.c ../vg_net_policy.c ../vg_net_policy.h
	$(CC) $(CFLAGS) -o $@ test_net_policy.c ../vg_net_policy.c

test_config_store: test_config_store.c ../vg_config_store.c ../vg_config_store.h
	$(CC) $(CFLAGS) -o $@ test_config_store.c ../vg_config_store.c

test_frame_stats: test_frame_stats.c ../vg_frame_stats.c ../vg_frame_stats.h
	$(CC) $(CFLAGS) -o $@ test_frame_stats.c ../vg_frame_stats.c

test_discover: test_discover.c ../vg_point_table.c ../vg_discover.h
	$(CC) $(CFLAGS) -o $@ test_discover.c ../vg_point_table.c

test_vgpoint: test_vgpoint.c ../vg_point_table.c ../vg_discover.h
	$(CC) $(CFLAGS) -o $@ test_vgpoint.c ../vg_point_table.c

test_alarm_eval: test_alarm_eval.c ../vg_alarm_eval.c ../vg_alarm_eval.h
	$(CC) $(CFLAGS) -o $@ test_alarm_eval.c ../vg_alarm_eval.c

test: test_net_policy test_config_store test_frame_stats test_discover test_vgpoint test_alarm_eval
	./test_net_policy
	./test_config_store
	./test_frame_stats
	./test_discover
	./test_vgpoint
	./test_alarm_eval

clean:
	rm -f test_net_policy test_config_store test_frame_stats test_discover test_vgpoint test_alarm_eval
