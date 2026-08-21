CC ?= gcc
CFLAGS ?= -Wall -Werror -I..

.PHONY: test clean

test_net_policy: test_net_policy.c ../vg_net_policy.c ../vg_net_policy.h
	$(CC) $(CFLAGS) -o $@ test_net_policy.c ../vg_net_policy.c

test: test_net_policy
	./test_net_policy

clean:
	rm -f test_net_policy
