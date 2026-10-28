#include "../vg_file_store.h"
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static int fault;
static int writes;
ssize_t __real_write(int, const void *, size_t);
ssize_t __real_read(int, void *, size_t);
int __real_fsync(int);
int __real_rename(const char *, const char *);
ssize_t __wrap_write(int fd, const void *p, size_t n)
{
  writes++;
  if (fault == 1) { errno = ENOSPC; return -1; }
  if (fault == 7 && writes == 2) {
    /* New primary verified correctly, but the backup write is corrupt. */
    char corrupt[256];
    assert(n <= sizeof(corrupt));
    memcpy(corrupt, p, n);
    corrupt[0] ^= 1;
    return __real_write(fd, corrupt, n);
  }
  return __real_write(fd, p, fault == 2 && n > 2 ? 2 : n);
}
ssize_t __wrap_read(int fd, void *p, size_t n)
{
  ssize_t got = __real_read(fd, p, n);
  if (fault == 3 && got > 0) ((char *)p)[0] ^= 1;
  return got;
}
int __wrap_fsync(int fd)
{
  if (fault == 4) { errno = EIO; return -1; }
  return __real_fsync(fd);
}
int __wrap_rename(const char *a, const char *b)
{
  if (fault == 6 && !strcmp(b, "store-test")) { unlink(b); errno = EIO; return -1; }
  if (fault == 5) { errno = EIO; return -1; }
  return __real_rename(a, b);
}
static void expect_file(const char *path, const char *want)
{
  char buf[256];
  FILE *f = fopen(path, "r");
  assert(f);
  size_t n = fread(buf, 1, sizeof(buf), f);
  fclose(f);
  assert(n == strlen(want) && !memcmp(buf, want, n));
}
static void *writer(void *arg)
{
  const char *s = arg;
  for (int i = 0; i < 100; i++)
    assert(!vg_file_replace("store-test", s, strlen(s), 0600));
  return NULL;
}
int main(void)
{
  const char *old = "old valid content";
  const char *next = "complete replacement";
  assert(!vg_file_replace("store-test", old, strlen(old), 0600));
  assert(vg_file_create("store-test", next, strlen(next), 0600) == -EEXIST);
  expect_file("store-test", old);
  for (int i = 1; i <= 6; i++) {
    if (i == 2) continue;
    fault = i;
    assert(vg_file_replace("store-test", next, strlen(next), 0600) < 0);
    fault = 0;
    expect_file("store-test", old);
  }
  writes = 0;
  fault = 7;
  assert(vg_file_replace("store-test", next, strlen(next), 0600) < 0);
  fault = 0;
  expect_file("store-test", old);
  expect_file("store-test.bak", old);
  fault = 2;
  assert(!vg_file_replace("store-test", next, strlen(next), 0600));
  fault = 0;
  expect_file("store-test", next);
  expect_file("store-test.bak", old);
  pthread_t a, b;
  assert(!pthread_create(&a, NULL, writer, (void *)old));
  assert(!pthread_create(&b, NULL, writer, (void *)next));
  pthread_join(a, NULL); pthread_join(b, NULL);
  char buf[64] = {0};
  FILE *f = fopen("store-test", "r");
  assert(f); fread(buf, 1, sizeof(buf) - 1, f); fclose(f);
  assert(!strcmp(buf, old) || !strcmp(buf, next));
  unlink("store-test");
  fault = 6;
  assert(vg_file_replace("store-test", next, strlen(next), 0600) < 0);
  fault = 0;
  assert(access("store-test", F_OK) != 0); /* do not resurrect an aborted file */
  unlink("store-test.bak");

  /* Name budget.  The board's vfat rejects a file name component longer than
   * VG_STORE_NAME_MAX, and the backup temporary used to push the product's
   * candidate name to 34 characters, which made every candidate edit fail.
   * Cover both sides: a name this store can serve completes, and one it cannot
   * is refused before any file appears. */

  const char *cand = "point_table_candidate.json";
  char cand_bak[64];
  assert(strlen(cand) + 5 <= VG_STORE_NAME_MAX);
  snprintf(cand_bak, sizeof(cand_bak), "%s.bak", cand);
  assert(!vg_file_replace(cand, old, strlen(old), 0600));
  expect_file(cand, old);
  assert(!vg_file_replace(cand, next, strlen(next), 0600));
  expect_file(cand, next);
  expect_file(cand_bak, old);
  unlink(cand);
  unlink(cand_bak);

  char toolong[VG_STORE_NAME_MAX + 1];
  char toolong_tmp[VG_STORE_NAME_MAX + 8];
  memset(toolong, 'a', VG_STORE_NAME_MAX);
  toolong[VG_STORE_NAME_MAX] = '\0';
  snprintf(toolong_tmp, sizeof(toolong_tmp), "%s.tmp", toolong);
  assert(vg_file_replace(toolong, next, strlen(next), 0600) == -ENAMETOOLONG);
  assert(access(toolong, F_OK) != 0);
  assert(access(toolong_tmp, F_OK) != 0);

  puts("test_file_store: OK (ENOSPC, short write, primary/backup verification, sync, rename, concurrent, name budget)");
  return 0;
}
