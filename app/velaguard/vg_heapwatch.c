/****************************************************************************
 * app/velaguard/vg_heapwatch.c
 *
 * DEBUG ONLY - heap corruption watchdog for the SDRAM heap region.
 * Watches the first heap node at 0xd0100000 (region 4, SDRAM, 1MB after
 * the LTDC framebuffer reserve). On change, prints a timestamp and the
 * bytes around the header to catch the writer red-handed.
 ****************************************************************************/

#include <nuttx/config.h>

#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define VG_WATCH_ADDR 0xd0100000UL

static FAR volatile uint32_t *g_node;

static uint32_t mono_ms(void)
{
  struct timespec ts;

  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint32_t)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

static FAR void *vg_heapwatch_thread(FAR void *arg)
{
  uint32_t prev;
  int i;

  (void)arg;

  g_node = (FAR volatile uint32_t *)VG_WATCH_ADDR;
  prev = *g_node;
  printf("heapwatch: start node@%08lx val=%08lx\n",
         VG_WATCH_ADDR, prev);

  for (; ; )
    {
      struct timespec ts;
      struct tm tmv;
      uint32_t now = *g_node;

      if (now != prev)
        {
          uint32_t ms = mono_ms();

          clock_gettime(CLOCK_REALTIME, &ts);
          gmtime_r(&ts.tv_sec, &tmv);
          printf("heapwatch: CHANGE t=%u wall=%02d:%02d:%02d "
                 "%08lx -> %08lx\n",
                 ms, tmv.tm_hour, tmv.tm_min, tmv.tm_sec,
                 prev, now);

          /* dump 48 bytes around the header */

          for (i = -16; i < 32; i++)
            {
              FAR volatile uint8_t *p = (FAR volatile uint8_t *)g_node + i;

              printf("%02x%s", *p, (i % 16 == 15) ? "\n" : " ");
            }

          /* measure the fill: how far the uniform u16 pattern runs */

          {
            FAR volatile uint16_t *u = (FAR volatile uint16_t *)VG_WATCH_ADDR;
            uint32_t n = 0;

            while (n < (4u << 20) / 2 && u[n] == (uint16_t)now)
              {
                n++;
              }

            printf("heapwatch: fill extent=%lu bytes (%lu rows @960)\n",
                   (unsigned long)(n * 2), (unsigned long)(n * 2 / 960));
          }

          prev = now;
        }

      usleep(200000);
    }

  return NULL;
}

void vg_heapwatch_start(void)
{
  pthread_t tid;
  pthread_attr_t attr;

  pthread_attr_init(&attr);
  pthread_attr_setstacksize(&attr, 2048);
  if (pthread_create(&tid, &attr, vg_heapwatch_thread, NULL) != 0)
    {
      printf("heapwatch: create failed\n");
      return;
    }

  pthread_attr_destroy(&attr);
  pthread_detach(tid);
}
