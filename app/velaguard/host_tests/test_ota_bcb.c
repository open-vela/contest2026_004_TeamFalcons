#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "vg_ota_bcb.h"

static void fill_bcb(struct vg_ota_bcb *bcb, uint32_t state, uint32_t slot,
                     uint32_t seq, uint32_t size)
{
  uint32_t i;

  memset(bcb, 0, sizeof(*bcb));
  bcb->seq = seq;
  bcb->state = state;
  bcb->slot = slot;
  bcb->image_size = size;
  bcb->trial_count = 0;
  strcpy(bcb->version, "0.2.0");
  strcpy(bcb->task_id, "TASK-0001");

  for (i = 0; i < VG_OTA_BCB_SHA256_BYTES; i++)
    {
      bcb->sha256[i] = (uint8_t)(i + 1u);
    }
}

static void test_roundtrip(void)
{
  struct vg_ota_bcb in;
  struct vg_ota_bcb out;
  uint8_t buf[VG_OTA_BCB_FILE_BYTES];

  fill_bcb(&in, VG_OTA_BCB_STATE_CONFIRMED, VG_OTA_BCB_SLOT_B, 7, 1511968);
  assert(vg_ota_bcb_encode(&in, buf, sizeof(buf)) == 0);
  assert(buf[0] == 'V' && buf[1] == 'G' && buf[2] == 'B' && buf[3] == 'C');
  assert(vg_ota_bcb_decode(buf, sizeof(buf), &out) == 0);
  assert(out.seq == 7);
  assert(out.state == VG_OTA_BCB_STATE_CONFIRMED);
  assert(out.slot == VG_OTA_BCB_SLOT_B);
  assert(out.image_size == 1511968);
  assert(strcmp(out.version, "0.2.0") == 0);
  assert(strcmp(out.task_id, "TASK-0001") == 0);
  assert(memcmp(out.sha256, in.sha256, sizeof(in.sha256)) == 0);
}

static void test_rejects(void)
{
  struct vg_ota_bcb bcb;
  struct vg_ota_bcb out;
  uint8_t buf[VG_OTA_BCB_FILE_BYTES];

  fill_bcb(&bcb, VG_OTA_BCB_STATE_CONFIRMED, VG_OTA_BCB_SLOT_B, 1, 1024);
  assert(vg_ota_bcb_encode(&bcb, buf, sizeof(buf)) == 0);

  buf[8] ^= 0x40;
  assert(vg_ota_bcb_decode(buf, sizeof(buf), &out) == -EBADMSG);
  buf[8] ^= 0x40;

  assert(vg_ota_bcb_decode(buf, sizeof(buf) - 1, &out) == -EINVAL);

  buf[0] = 0;
  assert(vg_ota_bcb_decode(buf, sizeof(buf), &out) == -EINVAL);
  buf[0] = 'V';

  buf[4] = 2;
  assert(vg_ota_bcb_decode(buf, sizeof(buf), &out) == -EINVAL);
  buf[4] = 1;

  fill_bcb(&bcb, 9, VG_OTA_BCB_SLOT_B, 1, 1024);
  assert(vg_ota_bcb_encode(&bcb, buf, sizeof(buf)) == -EINVAL);

  fill_bcb(&bcb, VG_OTA_BCB_STATE_CONFIRMED, 2, 1, 1024);
  assert(vg_ota_bcb_encode(&bcb, buf, sizeof(buf)) == -EINVAL);

  fill_bcb(&bcb, VG_OTA_BCB_STATE_CONFIRMED, VG_OTA_BCB_SLOT_B, 1, 0);
  assert(vg_ota_bcb_encode(&bcb, buf, sizeof(buf)) == -EINVAL);

  fill_bcb(&bcb, VG_OTA_BCB_STATE_CONFIRMED, VG_OTA_BCB_SLOT_B, 1,
           VG_OTA_SLOT_BYTES + 1u);
  assert(vg_ota_bcb_encode(&bcb, buf, sizeof(buf)) == -EINVAL);

  fill_bcb(&bcb, VG_OTA_BCB_STATE_CONFIRMED, VG_OTA_BCB_SLOT_B, 1, 1024);
  memset(bcb.version, 'X', VG_OTA_BCB_VERSION_BYTES);
  assert(vg_ota_bcb_encode(&bcb, buf, sizeof(buf)) == -EINVAL);

  fill_bcb(&bcb, VG_OTA_BCB_STATE_CONFIRMED, VG_OTA_BCB_SLOT_B, 1, 1024);
  bcb.version[0] = 0x1f;
  assert(vg_ota_bcb_encode(&bcb, buf, sizeof(buf)) == -EINVAL);

  fill_bcb(&bcb, VG_OTA_BCB_STATE_CONFIRMED, VG_OTA_BCB_SLOT_B, 1, 1024);
  memset(bcb.task_id, 0, sizeof(bcb.task_id));
  assert(vg_ota_bcb_encode(&bcb, buf, sizeof(buf)) == -EINVAL);

  fill_bcb(&bcb, VG_OTA_BCB_STATE_CONFIRMED, VG_OTA_BCB_SLOT_B, 1, 1024);
  memset(bcb.sha256, 0, sizeof(bcb.sha256));
  assert(vg_ota_bcb_encode(&bcb, buf, sizeof(buf)) == -EINVAL);
}

static void encode_pair(uint8_t buf0[VG_OTA_BCB_FILE_BYTES],
                        uint8_t buf1[VG_OTA_BCB_FILE_BYTES],
                        uint32_t state0, uint32_t slot0, uint32_t seq0,
                        uint32_t state1, uint32_t slot1, uint32_t seq1)
{
  struct vg_ota_bcb bcb;

  fill_bcb(&bcb, state0, slot0, seq0, 1024);
  assert(vg_ota_bcb_encode(&bcb, buf0, VG_OTA_BCB_FILE_BYTES) == 0);
  fill_bcb(&bcb, state1, slot1, seq1, 1024);
  assert(vg_ota_bcb_encode(&bcb, buf1, VG_OTA_BCB_FILE_BYTES) == 0);
}

static void test_seq(void)
{
  assert(vg_ota_bcb_seq_newer(2, 1) == 1);
  assert(vg_ota_bcb_seq_newer(1, 2) == 0);
  assert(vg_ota_bcb_seq_newer(5, 5) == 0);
  assert(vg_ota_bcb_seq_newer(0, 0xffffffffu) == 1);
  assert(vg_ota_bcb_seq_newer(0xffffffffu, 0) == 0);
  assert(vg_ota_bcb_seq_newer(0xfffffffeu, 0) == 0);
}

static void test_select(void)
{
  uint8_t buf0[VG_OTA_BCB_FILE_BYTES];
  uint8_t buf1[VG_OTA_BCB_FILE_BYTES];
  struct vg_ota_bcb out;
  uint32_t which;
  int valid0;
  int valid1;

  encode_pair(buf0, buf1, VG_OTA_BCB_STATE_CONFIRMED, VG_OTA_BCB_SLOT_A, 10,
              VG_OTA_BCB_STATE_CONFIRMED, VG_OTA_BCB_SLOT_B, 11);
  assert(vg_ota_bcb_select(buf0, sizeof(buf0), 1, buf1, sizeof(buf1), 1,
                           &out, &which, &valid0, &valid1) == 0);
  assert(valid0 == 1 && valid1 == 1);
  assert(which == 1);
  assert(out.slot == VG_OTA_BCB_SLOT_B);
  assert(out.seq == 11);

  encode_pair(buf0, buf1, VG_OTA_BCB_STATE_CONFIRMED, VG_OTA_BCB_SLOT_B,
              0xffffffffu,
              VG_OTA_BCB_STATE_CONFIRMED, VG_OTA_BCB_SLOT_A, 0);
  assert(vg_ota_bcb_select(buf0, sizeof(buf0), 1, buf1, sizeof(buf1), 1,
                           &out, &which, &valid0, &valid1) == 0);
  assert(which == 1);
  assert(out.slot == VG_OTA_BCB_SLOT_A);

  encode_pair(buf0, buf1, VG_OTA_BCB_STATE_CONFIRMED, VG_OTA_BCB_SLOT_A,
              0xffffffffu,
              VG_OTA_BCB_STATE_CONFIRMED, VG_OTA_BCB_SLOT_B, 0);
  assert(vg_ota_bcb_select(buf0, sizeof(buf0), 1, buf1, sizeof(buf1), 1,
                           &out, &which, &valid0, &valid1) == 0);
  assert(which == 1);
  assert(out.slot == VG_OTA_BCB_SLOT_B);

  encode_pair(buf0, buf1, VG_OTA_BCB_STATE_TRIAL, VG_OTA_BCB_SLOT_B, 4,
              VG_OTA_BCB_STATE_TRIAL, VG_OTA_BCB_SLOT_B, 4);
  assert(vg_ota_bcb_select(buf0, sizeof(buf0), 1, buf1, sizeof(buf1), 1,
                           &out, &which, &valid0, &valid1) == 0);
  assert(which == 0);

  buf0[6] ^= 0x01;
  assert(vg_ota_bcb_select(buf0, sizeof(buf0), 1, buf1, sizeof(buf1), 1,
                           &out, &which, &valid0, &valid1) == 0);
  assert(valid0 == 0 && valid1 == 1);
  assert(which == 1);

  assert(vg_ota_bcb_select(buf0, sizeof(buf0), 1, buf1, sizeof(buf1), 1,
                           &out, &which, &valid0, &valid1) == 0);
  buf1[7] ^= 0x80;
  assert(vg_ota_bcb_select(buf0, sizeof(buf0), 1, buf1, sizeof(buf1), 1,
                           &out, &which, &valid0, &valid1) == -ENOENT);
  assert(valid0 == 0 && valid1 == 0);

  assert(vg_ota_bcb_select(buf0, sizeof(buf0), 0, buf1, sizeof(buf1), 0,
                           &out, &which, &valid0, &valid1) == -ENOENT);
}

int main(void)
{
  test_roundtrip();
  test_rejects();
  test_seq();
  test_select();
  printf("test_ota_bcb: OK\n");
  return 0;
}
