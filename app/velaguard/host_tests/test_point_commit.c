/* SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "../vg_discover.h"
#include "../vg_file_store.h"

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t changed = PTHREAD_COND_INITIALIZER;
static pthread_t first;
static int first_written, second_done, interleave = 1;
static int fail_write, notifications;
static struct vg_discover_summary tables[2];

void vg_mqtt_notify_point_table(void) { notifications++; }

int __real_vg_file_replace(const char *, const void *, size_t, mode_t);
int __wrap_vg_file_replace(const char *path, const void *data, size_t n, mode_t mode)
{
  if (fail_write) return -ENOSPC;
  int rc = __real_vg_file_replace(path, data, n, mode);
  pthread_mutex_lock(&lock);
  int is_first = interleave && pthread_equal(pthread_self(), first);
  pthread_mutex_unlock(&lock);
  if (!rc && is_first)
    {
      struct timespec deadline;
      clock_gettime(CLOCK_REALTIME, &deadline);
      deadline.tv_sec += 1;
      pthread_mutex_lock(&lock);
      first_written = 1;
      pthread_cond_broadcast(&changed);
      /* Hold A after disk replacement so B can overtake its RAM update.
       * A serialized commit makes B wait until this bounded pause ends. */
      while (!second_done)
        if (pthread_cond_timedwait(&changed, &lock, &deadline) == ETIMEDOUT) break;
      pthread_mutex_unlock(&lock);
    }
  return rc;
}

static void *commit(void *arg)
{
  int index = *(int *)arg;
  assert(!vg_point_table_apply(&tables[index], "commit-test.json", NULL, true));
  if (index == 1)
    {
      pthread_mutex_lock(&lock);
      second_done = 1;
      pthread_cond_broadcast(&changed);
      pthread_mutex_unlock(&lock);
    }
  return NULL;
}

int main(void)
{
  int a = 0, b = 1;
  pthread_t second;
  struct vg_discover_summary disk, ram;
  for (int i = 0; i < 2; i++)
    {
      tables[i].baud = 9600;
      tables[i].n_points = 1;
      struct vg_point_entry *p = &tables[i].points[0];
      strcpy(p->id, "temp"); strcpy(p->dtype, "int16");
      p->addr = 1; p->fc = 3; p->qty = 1; p->scale = 1;
      p->reg = i;
    }
  /* The first thread waits for publication of its own pthread_t. */
  pthread_mutex_lock(&lock);
  assert(!pthread_create(&first, NULL, commit, &a));
  while (!first_written) pthread_cond_wait(&changed, &lock);
  pthread_mutex_unlock(&lock);
  assert(!pthread_create(&second, NULL, commit, &b));
  pthread_join(first, NULL); pthread_join(second, NULL);
  interleave = 0;
  assert(!vg_point_table_read(&disk, "commit-test.json"));
  assert(vg_live_points_copy(&ram) == 1);
  assert(disk.points[0].reg == ram.points[0].reg);
  assert(disk.points[0].reg == 1);
  assert(notifications == 2);
  uint32_t generation = vg_live_points_gen();
  fail_write = 1;
  assert(vg_point_table_commit(&tables[0], "commit-test.json") == -ENOSPC);
  fail_write = 0;
  assert(vg_live_points_gen() == generation && notifications == 2);
  assert(!vg_point_table_read(&disk, "commit-test.json") && disk.points[0].reg == 1);
  tables[0].n_points = 0;
  assert(!vg_point_table_commit(&tables[0], "commit-test.json"));
  assert(vg_live_points_copy(&ram) == 0 && notifications == 3);
  unlink("commit-test.json"); unlink("commit-test.json.bak");
  puts("test_point_commit: OK (concurrent commits, failed write, explicit empty table)");
  return 0;
}
