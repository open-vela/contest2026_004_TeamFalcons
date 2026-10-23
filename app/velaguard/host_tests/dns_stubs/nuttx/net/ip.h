#pragma once
#include <arpa/inet.h>
#define HTONS htons
#define NTOHS ntohs
#define ip4_addr1(ip) ((unsigned char *)&(ip))[0]
#define ip4_addr2(ip) ((unsigned char *)&(ip))[1]
#define ip4_addr3(ip) ((unsigned char *)&(ip))[2]
#define ip4_addr4(ip) ((unsigned char *)&(ip))[3]
