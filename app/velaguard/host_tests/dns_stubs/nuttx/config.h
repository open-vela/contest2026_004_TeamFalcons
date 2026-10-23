#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <errno.h>
#define FAR
#define CODE
#define begin_packed_struct
#define end_packed_struct __attribute__((packed))
#define OK 0
#define ERROR -1
#define CONFIG_NET_IPv4 1
#define CONFIG_NET_SOLINGER 1
#define CONFIG_NETDB_DNSCLIENT 1
#define CONFIG_NETDB_MAX_IPv4ADDR 1
#define CONFIG_NETDB_DNSCLIENT_NAMESIZE 256
#define CONFIG_NETDB_DNSCLIENT_MAXRESPONSE 512
#define CONFIG_NETDB_DNSCLIENT_RETRIES 3
#define CONFIG_NETDB_DNSCLIENT_ENTRIES 8
#define CONFIG_NETDB_DNSCLIENT_LIFESEC 3600
#define get_errno() errno
#define set_errno(e) (errno=(e))
#ifndef MIN
#define MIN(a,b) ((a)<(b)?(a):(b))
#endif
#ifndef MAX
#define MAX(a,b) ((a)>(b)?(a):(b))
#endif
