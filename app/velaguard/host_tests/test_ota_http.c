#include "vg_ota_http.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
int main(void)
{
  struct vg_ota_url u;
  struct vg_ota_http_response r;
  char request[1024];
  const char *hosts[]={"updates.example:8443"};
  assert(!vg_ota_url_parse("https://UPDATES.example:8443/image?token=redacted",&u));
  assert(vg_ota_url_allowed(&u,hosts,1));
  assert(!vg_ota_url_parse("https://evil.updates.example:8443/image",&u) && !vg_ota_url_allowed(&u,hosts,1));
  const char *bad[]={"http://updates.example/i","https://user@updates.example/i","https://updates.example./i","https://updates.example:0/i","https://updates.example:65536/i","https://updates.example/i#fragment","https://updates.example/i\r\nInjected:1","https://updates.example/i path",NULL};
  for(int i=0;bad[i];i++) assert(vg_ota_url_parse(bad[i],&u)<0);
  assert(!vg_ota_url_parse("https://updates.example:8443/i",&u));
  assert(vg_ota_http_request(&u,1024,"\"etag-1\"",request,sizeof(request))>0);
  assert(strstr(request,"Range: bytes=1024-") && strstr(request,"If-Range: \"etag-1\""));
  assert(vg_ota_http_request(&u,1,"W/\"weak\"",request,sizeof(request))<0);
  assert(!vg_ota_http_parse_response("HTTP/1.1 200 OK\r\nContent-Length: 1024\r\nETag: \"a\"\r\n\r\n",&r));
  assert(r.status==200 && r.length==1024 && !strcmp(r.etag,"\"a\""));
  assert(!vg_ota_http_parse_response("HTTP/1.1 206 Partial\r\nContent-Length: 512\r\nContent-Range: bytes 512-1023/1024\r\n\r\n",&r));
  const char *headers[]={
    "HTTP/1.1 302 Found\r\nContent-Length: 0\r\n\r\n",
    "HTTP/1.1 200 OK\r\nContent-Length: 1\r\nContent-Length: 1\r\n\r\n",
    "HTTP/1.1 200 OK\r\nContent-Length: 1\r\nTransfer-Encoding: chunked\r\n\r\n",
    "HTTP/1.1 200 OK\r\nContent-Length: 1\r\nContent-Encoding: gzip\r\n\r\n",
    "HTTP/1.1 200 OK\r\nContent-Length: 18446744073709551616\r\n\r\n",
    "HTTP/1.1 206 Partial\r\nContent-Length: 1\r\n\r\n",NULL};
  for(int i=0;headers[i];i++) assert(vg_ota_http_parse_response(headers[i],&r)<0);
  puts("test_ota_http: OK");
  return 0;
}
