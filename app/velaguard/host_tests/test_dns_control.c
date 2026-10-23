/* SPDX-License-Identifier: Apache-2.0 */
#include <nuttx/config.h>
#include <nuttx/net/dns.h>
#include "netdb/lib_dns.h"
#include <assert.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <time.h>
#include <unistd.h>

static struct sockaddr_in server;
static int saves;
int dns_bind(sa_family_t family, bool stream)
{ int fd=socket(family,stream?SOCK_STREAM:SOCK_DGRAM,0); return fd<0?-errno:fd; }
bool dns_is_queryfamily(sa_family_t family) { return family == AF_INET; }
int dns_foreach_nameserver(dns_callback_t cb, void *arg)
{ return cb(arg,(struct sockaddr *)&server,sizeof(server)); }
void dns_save_answer(const char *host, const union dns_addr_u *addr, int n, uint32_t ttl)
{ (void)host;(void)addr;(void)n;(void)ttl;saves++; }
int dns_find_answer(const char *host,union dns_addr_u *addr,int *n)
{ (void)host;(void)addr;(void)n;return -ENOENT; }
static uint32_t now_ms(void)
{ struct timespec ts;clock_gettime(CLOCK_MONOTONIC,&ts);return ts.tv_sec*1000u+ts.tv_nsec/1000000u; }
struct request {uint32_t start,budget,cancel;};
static int budget(void *arg)
{ struct request *r=arg;uint32_t used=now_ms()-r->start;
  if(r->cancel && used>=r->cancel)return -ECANCELED;
  return used>=r->budget?0:(int)(r->budget-used); }
static int fd_count(void)
{ int n=0;DIR *d=opendir("/proc/self/fd");assert(d);while(readdir(d))n++;closedir(d);return n; }
static void query(const char *name,int expected,uint32_t limit,uint32_t cancel)
{
 struct sockaddr_storage result[4];int n=4;
 struct request r={now_ms(),limit,cancel};
 struct dns_query_control_s ctl={budget,&r,(struct sockaddr *)&server,sizeof(server)};
 int fds=fd_count();int rc=dns_query_controlled(name,result,&n,&ctl);
 uint32_t used=now_ms()-r.start;
 printf("case=%s rc=%d count=%d ms=%u\n",name,rc,n,used);
 if(expected==0){assert(rc==0 && n==1);assert(result[0].ss_family==AF_INET);
   assert(((struct sockaddr_in *)&result[0])->sin_addr.s_addr==htonl(0x7f000001));}
 else if(expected==-1)assert(rc<0 && n==0);
 else assert(rc==expected && n==0);
 assert(used <= (cancel?cancel:limit)+500);
 assert(fd_count()==fds);
}
int main(int argc,char **argv)
{
 assert(argc==2);setvbuf(stdout,NULL,_IONBF,0);signal(SIGPIPE,SIG_IGN);
 server.sin_family=AF_INET;server.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
 server.sin_port=htons((uint16_t)atoi(argv[1]));
 for(int i=0;i<4;i++){
  query("ok.test",0,1000,0);
  query("stall.test",-ETIMEDOUT,150,0);
  query("stall.test",-ECANCELED,1000,60);
  query("tcp.test",0,1000,0);
  query("tcpstall.test",-ETIMEDOUT,150,0);
  query("half.test",-ECANCELED,1000,60);
  query("wrongid.test",-1,1000,0);
  query("short.test",-1,1000,0);
  query("ok.test",0,1000,0);
 }
 assert(saves==0); /* Per-query fixture override cannot poison shared cache. */
 struct sockaddr_storage result[4];int n=4;
 struct request r={now_ms(),1000,0};
 struct dns_query_control_s ctl={budget,&r,NULL,0};
 assert(!dns_query_controlled("ok.test",result,&n,&ctl));assert(saves==1);
 n=4;assert(dns_query_controlled("",result,&n,&ctl)==-EINVAL && n==0);
 puts("test_dns_control: OK (36 fault/recovery calls, split TCP, cancellation, deadline, no fd leak, cache isolation)");
 return 0;
}
