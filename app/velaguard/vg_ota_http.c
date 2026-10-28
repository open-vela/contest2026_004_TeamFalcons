#include "vg_ota_http.h"
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

int vg_ota_url_parse(const char *url, struct vg_ota_url *out)
{
  const char *end, *colon, *path;
  size_t len, i;
  unsigned port = 443;
  if (!url || !out || strlen(url) >= VG_OTA_URL_MAX || strncmp(url,"https://",8)) return -EINVAL;
  for (i=0; url[i]; i++) if ((unsigned char)url[i] <= 32 || (unsigned char)url[i] >= 127 || url[i]=='#' || url[i]=='\\') return -EINVAL;
  memset(out,0,sizeof(*out));
  path = strchr(url+8,'/');
  if (!path) return -EINVAL;
  end = path;
  colon = memchr(url+8,':',(size_t)(end-url-8));
  len = (size_t)((colon ? colon : end)-(url+8));
  if (!len || len >= sizeof(out->host) || url[8]=='.' || url[8+len-1]=='.') return -EINVAL;
  for (i=0;i<len;i++) {
    unsigned char c=(unsigned char)url[8+i];
    if (!(isalnum(c) || c=='-' || c=='.')) return -EINVAL;
    out->host[i]=(char)tolower(c);
  }
  if (colon) {
    const char *q=colon+1;
    port=0;
    if (q==end) return -EINVAL;
    for (;q<end;q++) {
      if (!isdigit((unsigned char)*q) || port>6553) return -EINVAL;
      port=port*10+(unsigned)(*q-'0');
    }
    if (!port || port>65535) return -EINVAL;
  }
  snprintf(out->port,sizeof(out->port),"%u",port);
  snprintf(out->target,sizeof(out->target),"%s",path);
  return 0;
}
int vg_ota_url_allowed(const struct vg_ota_url *url, const char *const *hosts, size_t count)
{
  char candidate[VG_OTA_HOST_MAX+32];
  struct vg_ota_url parsed;
  if (!url || !hosts || !count) return 0;
  for (size_t i=0;i<count;i++) {
    if (!hosts[i] || strlen(hosts[i]) >= VG_OTA_HOST_MAX+6) continue;
    snprintf(candidate,sizeof(candidate),"https://%s/",hosts[i]);
    if (!vg_ota_url_parse(candidate,&parsed) && !strcmp(url->host,parsed.host) && !strcmp(url->port,parsed.port)) return 1;
  }
  return 0;
}
int vg_ota_etag_strong(const char *s)
{
  size_t n;
  if (!s || (n=strlen(s))<2 || n>=VG_OTA_ETAG_MAX || s[0]!='"' || s[n-1]!='"') return 0;
  for (size_t i=1;i+1<n;i++) if ((unsigned char)s[i]<33 || (unsigned char)s[i]>=127 || s[i]=='"') return 0;
  return 1;
}
static int decimal(const char *s, uint64_t *value)
{
  uint64_t v=0;
  if (!s[0]) return -EINVAL;
  for (;*s;s++) {
    if (!isdigit((unsigned char)*s) || v>(UINT64_MAX-(unsigned)(*s-'0'))/10) return -EINVAL;
    v=v*10+(unsigned)(*s-'0');
  }
  *value=v;
  return 0;
}
int vg_ota_http_parse_response(const char *header, struct vg_ota_http_response *out)
{
  char local[VG_OTA_HTTP_HEADER_MAX], *line, *next;
  unsigned minor, status;
  int length_seen=0, etag_seen=0, end=0, range_seen=0;
  if (!header || !out || strlen(header)>=sizeof(local)) return -E2BIG;
  memset(out,0,sizeof(*out));
  strcpy(local,header);
  line=strstr(local,"\r\n");
  if (!line) return -EPROTO;
  *line=0;
  if (sscanf(local,"HTTP/1.%u %u",&minor,&status)!=2 || minor>1 || (status!=200 && status!=206)) return -EPROTO;
  out->status=status;
  line+=2;
  while ((next=strstr(line,"\r\n"))) {
    char *colon,*value,*tail;
    *next=0;
    if (!*line) { end=1; if (next[2]) return -EPROTO; break; }
    if (*line==' ' || *line=='\t' || !(colon=strchr(line,':'))) return -EPROTO;
    *colon=0;
    value=colon+1;
    while (*value==' ' || *value=='\t') value++;
    tail=value+strlen(value);
    while (tail>value && (tail[-1]==' ' || tail[-1]=='\t')) *--tail=0;
    for (char *v=line;v<colon;v++) if (!(isalnum((unsigned char)*v) || *v=='-')) return -EPROTO;
    if (!strcasecmp(line,"Content-Length")) {
      if (length_seen++ || decimal(value,&out->length)) return -EPROTO;
    } else if (!strcasecmp(line,"Transfer-Encoding")) return -EPROTO;
    else if (!strcasecmp(line,"Content-Encoding") && strcasecmp(value,"identity")) return -EPROTO;
    else if (!strcasecmp(line,"ETag")) {
      if (etag_seen++ || strlen(value)>=sizeof(out->etag)) return -EPROTO;
      strcpy(out->etag,value);
    } else if (!strcasecmp(line,"Content-Range")) {
      unsigned long long a,b,c;
      int consumed=0;
      if (range_seen++ || sscanf(value,"bytes %llu-%llu/%llu%n",&a,&b,&c,&consumed)!=3 || value[consumed] || a>b || b>=c) return -EPROTO;
      out->has_range=1; out->range_start=a; out->range_end=b; out->range_total=c;
    }
    line=next+2;
  }
  if (!end || !length_seen || (status==206 && !out->has_range) || (status==200 && out->has_range)) return -EPROTO;
  return 0;
}
int vg_ota_http_request(const struct vg_ota_url *url, uint32_t offset, const char *etag,
                         char *out, size_t capacity)
{
  int n;
  if (!url || !out || !capacity || (offset && !vg_ota_etag_strong(etag))) return -EINVAL;
  if (offset) n=snprintf(out,capacity,"GET %s HTTP/1.1\r\nHost: %s:%s\r\nAccept-Encoding: identity\r\nConnection: close\r\nRange: bytes=%lu-\r\nIf-Range: %s\r\n\r\n",url->target,url->host,url->port,(unsigned long)offset,etag);
  else n=snprintf(out,capacity,"GET %s HTTP/1.1\r\nHost: %s:%s\r\nAccept-Encoding: identity\r\nConnection: close\r\n\r\n",url->target,url->host,url->port);
  return n<0 || (size_t)n>=capacity ? -ENOSPC : n;
}
