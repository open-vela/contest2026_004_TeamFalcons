#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include "../vg_discover.h"
#include "../modbus_port_openvela.h"

static unsigned char response[64];
static unsigned response_n, response_pos;
static int open_error, opens, last_qty;
int vg_modbus_port_open(struct vg_modbus_port_s *p, const char *path)
{ (void)path; opens++; if (open_error) { errno = open_error; return -1; } p->fd = 1; return 0; }
void vg_modbus_port_close(struct vg_modbus_port_s *p) { p->fd = -1; }
static int32_t fake_write(const uint8_t *buf, uint16_t n, int32_t ms, void *arg)
{
  (void)ms; (void)arg;
  assert(n == 8);
  last_qty = (buf[4] << 8) | buf[5];
  response[0] = buf[0]; response[1] = buf[1];
  if (last_qty == 1 && buf[2] == 0 && buf[3] == 0) {
    response[2] = 2; response[3] = 0; response[4] = 123; response_n = 5;
  } else { response[1] |= 0x80; response[2] = 2; response_n = 3; }
  uint16_t crc = 0xffff;
  for (unsigned i = 0; i < response_n; i++) {
    crc ^= response[i];
    for (int j = 0; j < 8; j++) crc = crc & 1 ? (crc >> 1) ^ 0xa001 : crc >> 1;
  }
  response[response_n++] = crc & 255; response[response_n++] = crc >> 8;
  response_pos = 0;
  return n;
}
static int32_t fake_read(uint8_t *buf, uint16_t n, int32_t ms, void *arg)
{
  (void)ms; (void)arg;
  if (n > response_n - response_pos) n = response_n - response_pos;
  memcpy(buf, response + response_pos, n); response_pos += n; return n;
}
void vg_modbus_port_bind(struct vg_modbus_port_s *p, nmbs_platform_conf *pc)
{
  (void)p; nmbs_platform_conf_create(pc); pc->transport = NMBS_TRANSPORT_RTU;
  pc->read = fake_read; pc->write = fake_write;
}
int main(void)
{
  struct vg_point_entry point = {0};
  uint16_t value;
  uint8_t ok;
  assert(vg_discover_test_read("fake", 9600, 1, 0, 0, &value) == -EINVAL);
  assert(vg_discover_test_read("fake", 9600, 1, 0, 17, &value) == -EINVAL);
  assert(opens == 0);
  open_error = ENODEV;
  assert(vg_discover_test_read("fake", 9600, 1, 0, 1, &value) == -ENODEV);
  open_error = 0;
  point.addr = 1; point.fc = 3; point.qty = 4; strcpy(point.dtype, "int16");
  assert(!vg_discover_poll_points("fake", 9600, &point, 1, &value, &ok, 0));
  assert(last_qty == 1 && ok && value == 123);
  struct vg_discover_summary sum = {0};
  sum.n_blocks = VG_DISCOVER_MAX_BLOCKS;
  assert(vg_reg_probe_slave(&sum, "fake", 9600, 1, 3, 32) >= 0);
  assert(sum.flags & VG_DISCOVER_TRUNCATED);
  puts("test_discover_io: OK (bounds before I/O, errno, narrow scalar, capacity)");
  return 0;
}
