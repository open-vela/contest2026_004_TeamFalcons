/* SPDX-License-Identifier: Apache-2.0 */
/* 使用真实播放时钟的模拟驱动；只在播放完成后归还缓冲并核对采样顺序。 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <mqueue.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "vg_tts_player.h"
#include "../vg_tts_player.c"

static int g_fail;
#define CHECK(c) do { if (!(c)) { fprintf(stderr,"FAIL %d: %s\n",__LINE__,#c); __atomic_add_fetch(&g_fail,1,__ATOMIC_RELAXED); } } while (0)

static pthread_mutex_t d_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t d_wake = PTHREAD_COND_INITIALIZER;
static pthread_t d_thread;
static bool d_thread_valid, d_quit, d_stop, d_running;
static bool d_hold, d_lose, d_open_fail;
static struct ap_buffer_s *d_queue[8], *d_alloc[8];
static unsigned d_qcount, d_alloc_count;
static mqd_t d_mq;
static unsigned d_header;
static uint32_t d_starts, d_enqueues, d_frees;
static size_t d_skip[8];
static int16_t d_played[250000];
static size_t d_played_n;
static uint8_t pattern[400000];
static volatile int started_flag;
static struct audio_caps_s d_caps;

static uint32_t wall_ms(void) { return now_ms(); }
static void post_msg(int id, void *ptr)
{
  struct audio_msg_s msg = {0};
  msg.msg_id = id; msg.u.ptr = ptr;
  CHECK(mq_send(d_mq,(char *)&msg,sizeof(msg),0)==0);
}
static int alloc_index(struct ap_buffer_s *p)
{
  for (unsigned i=0;i<d_alloc_count;i++) if(d_alloc[i]==p) return (int)i;
  CHECK(false); return 0;
}
static void *driver_worker(void *unused)
{
  (void)unused;
  pthread_mutex_lock(&d_lock);
  while (!d_quit)
    {
      while (!d_quit && !d_stop && (!d_running || !d_qcount || d_hold))
        pthread_cond_wait(&d_wake,&d_lock);
      if(d_quit) break;
      if(d_stop)
        {
          if(!d_lose) for(unsigned i=0;i<d_qcount;i++) post_msg(AUDIO_MSG_DEQUEUE,d_queue[i]);
          d_qcount=0; d_stop=false; d_running=false; continue;
        }
      struct ap_buffer_s *p=d_queue[0];
      int slot=alloc_index(p);
      size_t skip=d_skip[slot];
      size_t samples=(p->nbytes-skip)/4u;
      uint32_t finish=wall_ms()+(uint32_t)((samples+15u)/16u);
      while (!d_stop && !d_quit && (int32_t)(wall_ms()-finish)<0)
        {
          struct timespec t;
          clock_gettime(CLOCK_REALTIME,&t);
          t.tv_nsec+=1000000;
          if(t.tv_nsec>=1000000000){t.tv_sec++;t.tv_nsec-=1000000000;}
          pthread_cond_timedwait(&d_wake,&d_lock,&t);
        }
      if(d_stop || d_quit) continue;
      const int16_t *src=(const int16_t *)(p->samp+skip);
      for(size_t i=0;i<samples;i++)
        {
          CHECK(src[2*i]==src[2*i+1]);
          CHECK(d_played_n<sizeof(d_played)/sizeof(d_played[0]));
          if(d_played_n<sizeof(d_played)/sizeof(d_played[0])) d_played[d_played_n++]=src[2*i];
        }
      bool final=(p->flags&AUDIO_APB_FINAL)!=0;
      for(unsigned i=1;i<d_qcount;i++) d_queue[i-1]=d_queue[i];
      d_qcount--;
      post_msg(AUDIO_MSG_DEQUEUE,p);
      if(final) post_msg(AUDIO_MSG_COMPLETE,NULL);
    }
  pthread_mutex_unlock(&d_lock);
  return NULL;
}

int __wrap_open(const char *path,int flags,...)
{
  (void)flags;
  if(d_open_fail || strcmp(path,"/dev/audio/pcm0p")){errno=ENOENT;return -1;}
  return 42;
}
int __wrap_close(int fd) { CHECK(fd==42);return 0; }
int __wrap_ioctl(int fd,unsigned long req,...)
{
  va_list ap; void *arg; (void)fd;
  va_start(ap,req);arg=va_arg(ap,void *);va_end(ap);
  switch(req)
    {
      case AUDIOIOC_GETCAPS: {struct audio_caps_s *c=arg;c->ac_type=AUDIO_TYPE_OUTPUT;return c->ac_len;}
      case AUDIOIOC_RESERVE: d_header=0;return 0;
      case AUDIOIOC_CONFIGURE: d_caps=((struct audio_caps_desc_s *)arg)->caps;return 0;
      case AUDIOIOC_GETBUFFERINFO: {struct ap_buffer_info_s *i=arg;i->nbuffers=4;i->buffer_size=8192;return 0;}
      case AUDIOIOC_ALLOCBUFFER:
        {
          struct audio_buf_desc_s *d=arg;
          struct ap_buffer_s *p=calloc(1,sizeof(*p)); CHECK(p!=NULL);
          p->samp=calloc(1,d->numbytes);p->nmaxbytes=d->numbytes;CHECK(p->samp!=NULL);
          d_alloc[d_alloc_count++]=p;*d->u.pbuffer=p;return sizeof(*d);
        }
      case AUDIOIOC_FREEBUFFER:
        {
          struct ap_buffer_s *p=((struct audio_buf_desc_s *)arg)->u.buffer;
          int slot=alloc_index(p);d_alloc[slot]=NULL;
          free(p->samp);free(p);d_frees++;return 0;
        }
      case AUDIOIOC_REGISTERMQ:
        d_mq=(mqd_t)(intptr_t)arg;d_quit=false;d_stop=false;d_running=false;
        CHECK(pthread_create(&d_thread,NULL,driver_worker,NULL)==0);d_thread_valid=true;return 0;
      case AUDIOIOC_START:
        pthread_mutex_lock(&d_lock);d_running=true;__atomic_add_fetch(&d_starts,1,__ATOMIC_RELAXED);
        pthread_cond_signal(&d_wake);pthread_mutex_unlock(&d_lock);return 0;
      case AUDIOIOC_ENQUEUEBUFFER:
        {
          struct ap_buffer_s *p=((struct audio_buf_desc_s *)arg)->u.buffer;
          pthread_mutex_lock(&d_lock);
          size_t skip=0;
          if(!d_header)
            {
              CHECK(p->nbytes>=44 && !memcmp(p->samp,"RIFF",4));
              CHECK(!memcmp(p->samp+8,"WAVE",4));
              CHECK(p->samp[22]==2 && p->samp[24]==0x80 && p->samp[25]==0x3e && p->samp[34]==16);
              skip=44;d_header=1;
            }
          CHECK(d_qcount<8);for(unsigned i=0;i<d_qcount;i++)CHECK(d_queue[i]!=p);
          d_skip[alloc_index(p)]=skip;d_queue[d_qcount++]=p;
          __atomic_add_fetch(&d_enqueues,1,__ATOMIC_RELAXED);
          pthread_cond_signal(&d_wake);pthread_mutex_unlock(&d_lock);return 0;
        }
      case AUDIOIOC_STOP:
        pthread_mutex_lock(&d_lock);d_stop=true;pthread_cond_signal(&d_wake);pthread_mutex_unlock(&d_lock);return 0;
      case AUDIOIOC_UNREGISTERMQ:
        pthread_mutex_lock(&d_lock);d_quit=true;pthread_cond_signal(&d_wake);pthread_mutex_unlock(&d_lock);
        if(d_thread_valid){pthread_join(d_thread,NULL);d_thread_valid=false;}return 0;
      case AUDIOIOC_RELEASE: return 0;
      default: CHECK(false);return -1;
    }
}

static void reset_driver(void)
{
  CHECK(!d_thread_valid);
  for(unsigned i=0;i<d_alloc_count;i++) CHECK(d_alloc[i]==NULL);
  d_alloc_count=d_qcount=0;d_starts=d_enqueues=d_frees=0;
  d_played_n=0;d_hold=d_lose=false;started_flag=0;
}
static uint32_t underruns(void)
{
  pthread_mutex_lock(&g_pl.lock);uint32_t n=g_pl.stats.underruns;pthread_mutex_unlock(&g_pl.lock);return n;
}
static void wait_counter(uint32_t *value,unsigned target,unsigned timeout)
{
  uint32_t t=wall_ms();while(__atomic_load_n(value,__ATOMIC_RELAXED)<target && wall_ms()-t<timeout)usleep(1000);
  CHECK(__atomic_load_n(value,__ATOMIC_RELAXED)>=target);
}
static void wait_underruns(unsigned target,unsigned timeout)
{
  uint32_t t=wall_ms();while(underruns()<target && wall_ms()-t<timeout)usleep(1000);
  CHECK(underruns()==target);
}
static void started(void *unused) {(void)unused;__atomic_store_n(&started_flag,1,__ATOMIC_RELEASE);}
static int start_player(unsigned ms,unsigned budget,volatile int *cancel)
{
  return vg_tts_player_start(ms,wall_ms()+budget,cancel,started,NULL);
}
static void finish_and_check(size_t bytes,unsigned limit)
{
  struct vg_tts_player_stats s;
  CHECK(vg_tts_player_eof()==0);CHECK(vg_tts_player_wait(limit,&s)==0);
  CHECK(s.complete && !s.failed && s.leaked_buffers==0);
  CHECK(s.pushed_bytes==bytes && s.played_bytes==bytes*2 && s.returned_bytes==s.played_bytes);
  CHECK(d_played_n==bytes/2 && !memcmp(d_played,pattern,bytes));
}
static void test_threshold_and_short(void)
{
  reset_driver();CHECK(vg_tts_player_prebuf_ms()==2500);
  CHECK(start_player(2500,10000,NULL)==0);
  CHECK(start_player(50,1000,NULL)==-EBUSY);
  CHECK(d_caps.ac_channels==2 && d_caps.ac_controls.hw[0]==16000 && d_caps.ac_controls.b[2]==16);
  CHECK(vg_tts_player_push(pattern,79998)==0);usleep(100000);CHECK(d_starts==0);
  uint32_t t=wall_ms();CHECK(vg_tts_player_push(pattern+79998,2)==0);wait_counter(&d_starts,1,200);
  CHECK(wall_ms()-t<=200);CHECK(vg_tts_player_push(pattern+80000,16000)==0);
  finish_and_check(96000,5000);CHECK(underruns()==0);
  reset_driver();CHECK(start_player(2500,5000,NULL)==0);
  CHECK(vg_tts_player_push(pattern,16000)==0);usleep(100000);CHECK(d_starts==0);
  finish_and_check(16000,2000);CHECK(d_starts==1);
  CHECK(vg_tts_player_set_prebuf_ms(1)==50);
  CHECK(vg_tts_player_set_prebuf_ms(5000)==4000);
  CHECK(vg_tts_player_set_prebuf_ms(2500)==2500);
}
static void test_inflight_and_jitter(void)
{
  reset_driver();CHECK(start_player(50,5000,NULL)==0);
  CHECK(vg_tts_player_push(pattern,8000)==0);wait_counter(&d_starts,1,500);
  usleep(100000);CHECK(underruns()==0);
  CHECK(vg_tts_player_push(pattern+8000,8000)==0);
  finish_and_check(16000,2000);CHECK(underruns()==0);
}
static void test_rebuffer_steps(void)
{
  reset_driver();CHECK(start_player(50,20000,NULL)==0);
  CHECK(vg_tts_player_push(pattern,1600)==0);wait_counter(&d_starts,1,500);
  size_t off=1600;
  for(unsigned n=1;n<=3;n++)
    {
      wait_underruns(n,5000);
      size_t bytes=(2500+n*500)*32;
      CHECK(vg_tts_player_push(pattern+off,bytes-2)==0);usleep(100000);
      CHECK(d_starts==n);
      CHECK(vg_tts_player_push(pattern+off+bytes-2,2)==0);
      wait_counter(&d_starts,n+1,200);off+=bytes;
    }
  finish_and_check(off,6000);CHECK(underruns()==3);
  CHECK(g_pl.stats.rebuffer_ms==4000 && g_pl.stats.restarts==3);
}
static volatile int helper_result,helper_done;
static void *push_big(void *unused)
{
  (void)unused;helper_result=vg_tts_player_push(pattern,160000);
  __atomic_store_n(&helper_done,1,__ATOMIC_RELEASE);return NULL;
}
static void test_full_queue_stop(bool cancel_it)
{
  struct vg_tts_player_stats s;pthread_t t;volatile int cancel=0;
  reset_driver();d_hold=true;helper_done=0;
  CHECK(start_player(50,cancel_it?5000:250,&cancel)==0);
  CHECK(pthread_create(&t,NULL,push_big,NULL)==0);wait_counter(&d_enqueues,4,200);
  usleep(50000);CHECK(!helper_done);
  if(cancel_it) cancel=1;
  struct timespec join_until;
  clock_gettime(CLOCK_REALTIME,&join_until);join_until.tv_sec+=2;
  uint32_t begin=wall_ms();
  int joined=pthread_timedjoin_np(t,NULL,&join_until);
  CHECK(joined==0);
  if(joined!=0)exit(EXIT_FAILURE);
  CHECK(wall_ms()-begin<1000);
  CHECK(helper_result==(cancel_it?-ECANCELED:-ETIMEDOUT));
  CHECK(vg_tts_player_wait(2000,&s)==0 && !s.complete && s.leaked_buffers==0);
}
static void test_stalled_driver(void)
{
  struct vg_tts_player_stats s;
  reset_driver();d_hold=true;CHECK(start_player(50,5000,NULL)==0);
  CHECK(vg_tts_player_push(pattern,20000)==0);wait_counter(&d_starts,1,500);
  usleep(1500000);
  CHECK(vg_tts_player_push(pattern,2)==-ETIMEDOUT);
  CHECK(vg_tts_player_wait(2000,&s)==0 && s.failed && !s.complete);
  CHECK(s.err==-ETIMEDOUT && s.leaked_buffers==0 && s.restarts==0);
}
static void test_abort_phases(void)
{
  struct vg_tts_player_stats s;volatile int cancel=0;
  for(unsigned phase=0;phase<3;phase++)
    {
      reset_driver();cancel=0;CHECK(start_player(phase?50:2500,5000,&cancel)==0);
      CHECK(vg_tts_player_push(pattern,phase==1?32000:1600)==0);
      if(phase==1)wait_counter(&d_starts,1,500);
      if(phase==2)wait_underruns(1,1000);
      cancel=1;CHECK(vg_tts_player_wait(2000,&s)==0);
      CHECK(!s.complete && !s.failed && s.leaked_buffers==0);
    }
  reset_driver();CHECK(start_player(2500,5000,NULL)==0);vg_tts_player_abort();
  CHECK(vg_tts_player_wait(2000,&s)==0 && !s.complete);
}
static void test_eof_past_deadline(void)
{
  struct vg_tts_player_stats s;
  reset_driver();CHECK(start_player(2500,150,NULL)==0);
  CHECK(vg_tts_player_push(pattern,16000)==0);
  finish_and_check(16000,2000);CHECK(d_played_n==8000);
  reset_driver();CHECK(start_player(2500,80,NULL)==0);
  CHECK(vg_tts_player_push(pattern,16000)==0);usleep(120000);
  CHECK(vg_tts_player_wait(2000,&s)==0 && s.failed && !s.started && !s.complete);
}
static void test_setup_bisect_and_ownership(void)
{
  struct vg_tts_player_stats s;
  reset_driver();d_open_fail=true;CHECK(start_player(50,5000,NULL)<0);d_open_fail=false;
  CHECK(vg_tts_player_push(pattern,2)==-EPIPE);
  for(int level=1;level<=2;level++)
    {
      reset_driver();CHECK(vg_tts_player_set_bisect_level(level)==level);
      CHECK(start_player(2500,5000,NULL)==0);CHECK(vg_tts_player_push(pattern,1600)==0);
      CHECK(vg_tts_player_eof()==0);CHECK(vg_tts_player_wait(2000,&s)==0);
      CHECK(s.complete && s.played_bytes==0 && s.leaked_buffers==0);
    }
  CHECK(vg_tts_player_set_bisect_level(0)==0);
  reset_driver();d_hold=d_lose=true;CHECK(start_player(50,5000,NULL)==0);
  CHECK(vg_tts_player_push(pattern,16000)==0);wait_counter(&d_starts,1,500);
  vg_tts_player_abort();CHECK(vg_tts_player_wait(2000,&s)==0);
  CHECK(s.leaked_buffers==4 && d_frees==0 && !s.complete);
  /* 模拟设备已关闭，测试释放其遗留内存；产品不能推断此条件。 */
  for(unsigned i=0;i<d_alloc_count;i++)if(d_alloc[i]){free(d_alloc[i]->samp);free(d_alloc[i]);d_alloc[i]=NULL;}
}
int main(void)
{
  setvbuf(stdout,NULL,_IONBF,0);
  /* 避免短周期图案使整块重复或交换也通过逐采样比较。 */
  uint32_t seed=0x12345678u;
  for(size_t i=0;i<sizeof(pattern);i+=2)
    {
      seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;
      pattern[i]=(uint8_t)seed;pattern[i+1]=(uint8_t)(seed>>8);
    }
  test_threshold_and_short();puts("threshold/short OK");
  test_inflight_and_jitter();puts("inflight/jitter OK");
  test_rebuffer_steps();puts("rebuffer 3000/3500/4000 and sample order OK");
  test_full_queue_stop(false);test_full_queue_stop(true);puts("full queue timeout/cancel OK");
  test_stalled_driver();puts("driver liveness failure OK");
  test_abort_phases();test_eof_past_deadline();puts("cancel phases and EOF deadline OK");
  test_setup_bisect_and_ownership();puts("setup/bisect/ownership OK");
  printf("test_tts_player: %s (%d failures)\n",g_fail?"FAIL":"OK",g_fail);
  return g_fail?1:0;
}
