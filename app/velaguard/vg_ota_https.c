#ifdef __NuttX__
#include <nuttx/config.h>
#endif
#include "vg_ota_https.h"
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/entropy.h>
#include <mbedtls/net_sockets.h>
#include <mbedtls/ssl.h>
#include <mbedtls/x509_crt.h>

#define OTA_IO_TIMEOUT_MS 15000u
struct ota_tls_session {
  mbedtls_net_context net;
  mbedtls_ssl_context ssl;
  mbedtls_ssl_config config;
  mbedtls_x509_crt ca;
  mbedtls_ctr_drbg_context random;
  mbedtls_entropy_context entropy;
  uint64_t remaining;
  uint64_t deadline_ms;
};
static uint64_t tls_now_ms(void)
{
  struct timespec ts;
  if (clock_gettime(CLOCK_MONOTONIC,&ts)!=0) return 0;
  return (uint64_t)ts.tv_sec*1000u+ts.tv_nsec/1000000u;
}
static int tls_send(void *ctx,const unsigned char *buf,size_t size)
{
  struct ota_tls_session *s=ctx;
  if (s->deadline_ms && tls_now_ms()>=s->deadline_ms) return MBEDTLS_ERR_SSL_TIMEOUT;
  return mbedtls_net_send(&s->net,buf,size);
}
static int tls_recv_timeout(void *ctx,unsigned char *buf,size_t size,uint32_t timeout)
{
  struct ota_tls_session *s=ctx;
  if (s->deadline_ms) {
    uint64_t now=tls_now_ms();
    if (now>=s->deadline_ms) return MBEDTLS_ERR_SSL_TIMEOUT;
    uint64_t left=s->deadline_ms-now;
    if (!timeout || left<timeout) timeout=(uint32_t)left;
  }
  return mbedtls_net_recv_timeout(&s->net,buf,size,timeout ? timeout : OTA_IO_TIMEOUT_MS);
}
static int tls_recv(void *ctx,unsigned char *buf,size_t size)
{
  return tls_recv_timeout(ctx,buf,size,OTA_IO_TIMEOUT_MS);
}
int vg_ota_tls_entropy_ready(void)
{
#ifdef __NuttX__
# if defined(CONFIG_DEV_URANDOM_XORSHIFT128) || defined(CONFIG_DEV_URANDOM_CONGRUENTIAL) || !defined(CONFIG_VG_OTA_VERIFIED_ENTROPY)
  return 0;
# endif
#endif
  return 1;
}
void vg_ota_https_init(struct vg_ota_https *c, const char *ca)
{
  memset(c,0,sizeof(*c));
  c->ca_pem=ca;
}
void vg_ota_https_close(void *ctx)
{
  struct vg_ota_https *c=ctx;
  struct ota_tls_session *s=c ? c->session : NULL;
  if (!s) return;
  /* 不等待对端 close_notify，避免错误路径拖住本地任务。 */
  mbedtls_net_free(&s->net);
  mbedtls_ssl_free(&s->ssl);
  mbedtls_ssl_config_free(&s->config);
  mbedtls_x509_crt_free(&s->ca);
  mbedtls_ctr_drbg_free(&s->random);
  mbedtls_entropy_free(&s->entropy);
  memset(s,0,sizeof(*s));
  free(s);
  c->session=NULL;
}
static int dial_eth0(struct ota_tls_session *s, const struct vg_ota_url *url)
{
  struct addrinfo hints,*list=NULL;
  struct pollfd waitfd;
  int ret, flags, error=0;
  socklen_t error_len=sizeof(error);
  memset(&hints,0,sizeof(hints));
  hints.ai_family=AF_INET;
  hints.ai_socktype=SOCK_STREAM;
  if (getaddrinfo(url->host,url->port,&hints,&list)!=0 || !list) return -EHOSTUNREACH;
  s->net.fd=socket(list->ai_family,list->ai_socktype,list->ai_protocol);
  if (s->net.fd<0) { freeaddrinfo(list); return -EIO; }
#ifdef __NuttX__
# ifdef CONFIG_NET_BINDTODEVICE
  if (setsockopt(s->net.fd,SOL_SOCKET,SO_BINDTODEVICE,"eth0",5)!=0) {
    freeaddrinfo(list); return -ENETDOWN;
  }
# else
  freeaddrinfo(list); return -ENOTSUP;
# endif
#endif
  flags=fcntl(s->net.fd,F_GETFL,0);
  if (flags<0 || fcntl(s->net.fd,F_SETFL,flags|O_NONBLOCK)<0) { freeaddrinfo(list); return -EIO; }
  ret=connect(s->net.fd,list->ai_addr,list->ai_addrlen);
  freeaddrinfo(list);
  if (ret<0 && errno!=EINPROGRESS) return -ECONNREFUSED;
  if (ret<0) {
    waitfd.fd=s->net.fd; waitfd.events=POLLOUT; waitfd.revents=0;
    if (poll(&waitfd,1,OTA_IO_TIMEOUT_MS)<=0 ||
        getsockopt(s->net.fd,SOL_SOCKET,SO_ERROR,&error,&error_len)!=0 || error) return -ETIMEDOUT;
  }
  if (fcntl(s->net.fd,F_SETFL,flags)<0) return -EIO;
  struct timeval timeout={15,0};
  if (setsockopt(s->net.fd,SOL_SOCKET,SO_SNDTIMEO,&timeout,sizeof(timeout))!=0 ||
      setsockopt(s->net.fd,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof(timeout))!=0) return -EIO;
  return 0;
}
static int open_tls_endpoint(struct vg_ota_https *c,const struct vg_ota_url *endpoint)
{
  struct ota_tls_session *s;
  int ret=-EIO;
  vg_ota_https_close(c);
  if (!c->ca_pem || !c->ca_pem[0]) return -ENOKEY;
  if (!vg_ota_tls_entropy_ready()) return -EAGAIN;
  if (time(NULL)<1767225600) return -ETIME;
#if !defined(MBEDTLS_HAVE_TIME_DATE)
  return -ENOTSUP;
#endif
  s=calloc(1,sizeof(*s));
  if (!s) return -ENOMEM;
  c->session=s;
  s->deadline_ms=tls_now_ms()+45000u;
  mbedtls_net_init(&s->net);
  mbedtls_ssl_init(&s->ssl);
  mbedtls_ssl_config_init(&s->config);
  mbedtls_x509_crt_init(&s->ca);
  mbedtls_ctr_drbg_init(&s->random);
  mbedtls_entropy_init(&s->entropy);
  if (mbedtls_ctr_drbg_seed(&s->random,mbedtls_entropy_func,&s->entropy,
                           (const uint8_t *)"VelaGuard-OTA",13)!=0) goto fail;
  if (mbedtls_x509_crt_parse(&s->ca,(const uint8_t *)c->ca_pem,strlen(c->ca_pem)+1)!=0) { ret=-EKEYREJECTED; goto fail; }
  if (mbedtls_ssl_config_defaults(&s->config,MBEDTLS_SSL_IS_CLIENT,MBEDTLS_SSL_TRANSPORT_STREAM,MBEDTLS_SSL_PRESET_DEFAULT)!=0) goto fail;
  mbedtls_ssl_conf_authmode(&s->config,MBEDTLS_SSL_VERIFY_REQUIRED);
  mbedtls_ssl_conf_ca_chain(&s->config,&s->ca,NULL);
  mbedtls_ssl_conf_rng(&s->config,mbedtls_ctr_drbg_random,&s->random);
  mbedtls_ssl_conf_read_timeout(&s->config,OTA_IO_TIMEOUT_MS);
  if (mbedtls_ssl_setup(&s->ssl,&s->config)!=0 || mbedtls_ssl_set_hostname(&s->ssl,endpoint->host)!=0) goto fail;
  ret=dial_eth0(s,endpoint);
  if (ret) goto fail;
  mbedtls_ssl_set_bio(&s->ssl,s,tls_send,tls_recv,tls_recv_timeout);
  ret=mbedtls_ssl_handshake(&s->ssl);
  if (ret!=0 || mbedtls_ssl_get_verify_result(&s->ssl)!=0) {
    ret=mbedtls_ssl_get_verify_result(&s->ssl)!=0 || ret==MBEDTLS_ERR_X509_CERT_VERIFY_FAILED ? -EKEYREJECTED : -ECONNRESET;
    goto fail;
  }
  return 0;
fail:
  vg_ota_https_close(c);
  return ret<0 ? ret : -EIO;
}

int vg_ota_tls_connect(struct vg_ota_https *c,const char *host,const char *port)
{
  char authority[VG_OTA_HOST_MAX+24];
  struct vg_ota_url endpoint;
  if (!c || !host || !port || strlen(host)>=VG_OTA_HOST_MAX || strlen(port)>5) return -EINVAL;
  snprintf(authority,sizeof(authority),"https://%s:%s/",host,port);
  if (vg_ota_url_parse(authority,&endpoint)) return -EINVAL;
  int ret=open_tls_endpoint(c,&endpoint);
  if (!ret) ((struct ota_tls_session *)c->session)->deadline_ms=0;
  return ret;
}

ssize_t vg_ota_tls_send(struct vg_ota_https *c,const void *bytes,size_t length)
{
  struct ota_tls_session *s=c ? c->session : NULL;
  if (!s || !bytes) return -EINVAL;
  int ret=mbedtls_ssl_write(&s->ssl,bytes,length);
  return ret>0 ? ret : -EIO;
}

ssize_t vg_ota_tls_receive(struct vg_ota_https *c,void *bytes,size_t length,uint32_t timeout_ms)
{
  struct ota_tls_session *s=c ? c->session : NULL;
  if (!s || !bytes) return -EINVAL;
  mbedtls_ssl_conf_read_timeout(&s->config,timeout_ms);
  int ret=mbedtls_ssl_read(&s->ssl,bytes,length);
  if (ret==MBEDTLS_ERR_SSL_TIMEOUT || ret==MBEDTLS_ERR_SSL_WANT_READ) return -EAGAIN;
  if (!ret || ret==MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY) return -ECONNRESET;
  return ret>0 ? ret : -EIO;
}

int vg_ota_https_open(void *ctx, const char *url, uint32_t offset,
                       const char *etag, struct vg_ota_http_response *response)
{
  struct vg_ota_https *c=ctx;
  struct vg_ota_url endpoint;
  struct ota_tls_session *s;
  char *header=NULL, *request=NULL;
  size_t used=0, sent=0;
  int ret=-EIO, length;
  if (!c || !response || vg_ota_url_parse(url,&endpoint)) return -EINVAL;
  ret=open_tls_endpoint(c,&endpoint);
  if (ret) return ret;
  s=c->session;
  request=malloc(1024);
  header=malloc(VG_OTA_HTTP_HEADER_MAX);
  if (!request || !header) { ret=-ENOMEM; goto fail; }
  length=vg_ota_http_request(&endpoint,offset,etag,request,1024);
  if (length<0) { ret=length; goto fail; }
  while (sent<(size_t)length) {
    ret=mbedtls_ssl_write(&s->ssl,(const uint8_t *)request+sent,(size_t)length-sent);
    if (ret<=0) { ret=-EIO; goto fail; }
    sent+=(size_t)ret;
  }
  while (used<VG_OTA_HTTP_HEADER_MAX-1) {
    ret=mbedtls_ssl_read(&s->ssl,(uint8_t *)header+used,1);
    if (ret!=1) { ret=-EIO; goto fail; }
    used++;
    if (used>=4 && !memcmp(header+used-4,"\r\n\r\n",4)) break;
  }
  header[used]=0;
  ret=vg_ota_http_parse_response(header,response);
  if (ret) goto fail;
  s->remaining=response->length;
  s->deadline_ms=0;
  memset(request,0,1024); free(request); free(header);
  return 0;
fail:
  if (request) { memset(request,0,1024); free(request); }
  free(header);
  vg_ota_https_close(c);
  return ret<0 ? ret : -EIO;
}
ssize_t vg_ota_https_read(void *ctx, uint8_t *buf, size_t length)
{
  struct vg_ota_https *c=ctx;
  struct ota_tls_session *s=c ? c->session : NULL;
  int ret;
  if (!s || !buf) return -EINVAL;
  if (!s->remaining) return 0;
  if ((uint64_t)length>s->remaining) length=(size_t)s->remaining;
  ret=mbedtls_ssl_read(&s->ssl,buf,length);
  if (ret<=0) return ret==0 || ret==MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY ? 0 : -EIO;
  s->remaining-=(unsigned)ret;
  return ret;
}
