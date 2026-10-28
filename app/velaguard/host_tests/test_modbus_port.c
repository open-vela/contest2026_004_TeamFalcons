/* SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include <errno.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>
#include "../modbus_port_openvela.h"

/* Model a reply buffered while tcdrain finishes. A post-TX input flush
 * discards a valid response even though the request has already left. */
static const uint8_t reply[] = {1, 3, 2, 0, 123, 0xf8, 0x67};
static size_t available;
static int drain_error;
static int input_flushes;

int __wrap_poll(struct pollfd *fds, nfds_t n, int timeout)
{
  (void)timeout;
  assert(n == 1 && fds[0].fd == 123);
  fds[0].revents = fds[0].events == POLLOUT ? POLLOUT :
                   (available ? POLLIN : 0);
  return fds[0].revents != 0;
}

ssize_t __wrap_write(int fd, const void *p, size_t n)
{
  assert(fd == 123 && p != NULL);
  return n;
}

ssize_t __wrap_read(int fd, void *p, size_t n)
{
  assert(fd == 123);
  if (n > available) n = available;
  memcpy(p, reply + sizeof(reply) - available, n);
  available -= n;
  return n;
}

int __wrap_tcdrain(int fd)
{
  assert(fd == 123);
  if (drain_error) { errno = EIO; return -1; }
  available = sizeof(reply);
  return 0;
}

int __wrap_tcflush(int fd, int selector)
{
  assert(fd == 123 && selector == TCIFLUSH);
  input_flushes++;
  available = 0;
  return 0;
}

int main(void)
{
  struct vg_modbus_port_s port = {.fd = 123};
  nmbs_platform_conf pc;
  uint8_t request[8] = {1, 3, 0, 0, 0, 1, 0x84, 0x0a};
  uint8_t actual[sizeof(reply)];
  vg_modbus_port_bind(&port, &pc);
  available = sizeof(reply); /* stale bytes are cleared before a request */
  pc.flush(NULL, pc.arg);
  assert(!available && input_flushes == 1);
  for (int i = 0; i < 10; i++)
    {
      assert(pc.write(request, sizeof(request), 500, pc.arg) == sizeof(request));
      assert(pc.read(actual, sizeof(actual), 20, pc.arg) == sizeof(actual));
      assert(!memcmp(actual, reply, sizeof(reply)));
    }
  assert(input_flushes == 1);
  drain_error = 1;
  assert(pc.write(request, sizeof(request), 500, pc.arg) == -1);
  puts("test_modbus_port: OK (early reply preserved, pre-request flush, drain failure)");
  return 0;
}
