#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "test_fat_image.h"
#include "vg_ota_boot.h"

#define NAME_VGOTA "VGOTA      "
#define NAME_BCB0  "BCB0    BIN"
#define NAME_BCB1  "BCB1    BIN"

struct fixture
{
  struct test_fat_image img;
  struct test_block_ctx block;
  uint32_t dir_cluster;
};

static void fill_bcb(struct vg_ota_bcb *bcb, uint32_t state, uint32_t slot,
                     uint32_t seq)
{
  uint32_t i;

  memset(bcb, 0, sizeof(*bcb));
  bcb->seq = seq;
  bcb->state = state;
  bcb->slot = slot;
  bcb->image_size = 1511968;
  strcpy(bcb->version, "0.2.0");
  strcpy(bcb->task_id, "TASK-0001");

  for (i = 0; i < VG_OTA_BCB_SHA256_BYTES; i++)
    {
      bcb->sha256[i] = (uint8_t)(i + 1u);
    }
}

static void fixture_init(struct fixture *f, int fat32)
{
  memset(f, 0, sizeof(*f));
  assert(test_fat_init(&f->img, fat32, fat32 ? 300 : 5000, 1) == 0);
  f->block.img = &f->img;
}

static void add_staging(struct fixture *f)
{
  f->dir_cluster = test_fat_alloc_chain(&f->img, 2);
  assert(f->dir_cluster >= 2);
  assert(test_fat_dir_add(&f->img, 0, NAME_VGOTA, 0x10,
                          f->dir_cluster, 0) == 0);
}

static void add_bcb(struct fixture *f, const char name[11],
                    uint32_t state, uint32_t slot, uint32_t seq)
{
  struct vg_ota_bcb bcb;
  uint8_t buf[VG_OTA_BCB_FILE_BYTES];

  fill_bcb(&bcb, state, slot, seq);
  assert(vg_ota_bcb_encode(&bcb, buf, sizeof(buf)) == 0);
  assert(test_fat_add_file(&f->img, f->dir_cluster, name, buf,
                           sizeof(buf)) == 0);
}

static void add_bcb_with_trials(struct fixture *f, const char name[11],
                                uint32_t state, uint32_t slot, uint32_t seq,
                                uint32_t trials)
{
  struct vg_ota_bcb bcb;
  uint8_t buf[VG_OTA_BCB_FILE_BYTES];

  fill_bcb(&bcb, state, slot, seq);
  bcb.trial_count = trials;
  assert(vg_ota_bcb_encode(&bcb, buf, sizeof(buf)) == 0);
  assert(test_fat_add_file(&f->img, f->dir_cluster, name, buf,
                           sizeof(buf)) == 0);
}

static void decide(struct fixture *f, struct vg_ota_boot_decision *d)
{
  assert(vg_ota_boot_decide(test_block_read, &f->block, f->img.sectors,
                            d) == 0);
}

static void fixture_done(struct fixture *f)
{
  test_fat_free(&f->img);
}

static void test_boot_b(void)
{
  struct fixture f;
  struct vg_ota_boot_decision d;
  int fat32;

  for (fat32 = 0; fat32 <= 1; fat32++)
    {
      fixture_init(&f, fat32);
      add_staging(&f);
      add_bcb(&f, NAME_BCB0, VG_OTA_BCB_STATE_CONFIRMED, VG_OTA_BCB_SLOT_B,
              1);
      decide(&f, &d);
      assert(d.slot == VG_OTA_BCB_SLOT_B);
      assert(d.reason == VG_OTA_BOOT_REASON_B_CONFIRMED);
      assert(d.bcb_which == 0);
      assert(d.bcb_valid0 == 1);
      assert(d.bcb_valid1 == 0);
      assert(d.bcb_seq == 1);
      assert(d.bcb_state == VG_OTA_BCB_STATE_CONFIRMED);
      fixture_done(&f);

      fixture_init(&f, fat32);
      add_staging(&f);
      add_bcb(&f, NAME_BCB0, VG_OTA_BCB_STATE_TRIAL, VG_OTA_BCB_SLOT_B, 3);
      decide(&f, &d);
      assert(d.slot == VG_OTA_BCB_SLOT_B);
      assert(d.reason == VG_OTA_BOOT_REASON_B_TRIAL);
      fixture_done(&f);
    }
}

static void test_boot_a_states(void)
{
  struct fixture f;
  struct vg_ota_boot_decision d;

  fixture_init(&f, 0);
  add_staging(&f);
  add_bcb(&f, NAME_BCB0, VG_OTA_BCB_STATE_PENDING, VG_OTA_BCB_SLOT_B, 4);
  decide(&f, &d);
  assert(d.slot == VG_OTA_BCB_SLOT_A);
  assert(d.reason == VG_OTA_BOOT_REASON_A_PENDING);
  fixture_done(&f);

  fixture_init(&f, 0);
  add_staging(&f);
  add_bcb(&f, NAME_BCB0, VG_OTA_BCB_STATE_FAILED, VG_OTA_BCB_SLOT_B, 4);
  decide(&f, &d);
  assert(d.slot == VG_OTA_BCB_SLOT_A);
  assert(d.reason == VG_OTA_BOOT_REASON_A_FAILED);
  fixture_done(&f);

  fixture_init(&f, 0);
  add_staging(&f);
  add_bcb(&f, NAME_BCB0, VG_OTA_BCB_STATE_CONFIRMED, VG_OTA_BCB_SLOT_A,
          4);
  decide(&f, &d);
  assert(d.slot == VG_OTA_BCB_SLOT_A);
  assert(d.reason == VG_OTA_BOOT_REASON_SLOT_A);
  fixture_done(&f);

  fixture_init(&f, 0);
  add_staging(&f);
  add_bcb(&f, NAME_BCB0, VG_OTA_BCB_STATE_CONFIRMED, VG_OTA_BCB_SLOT_B,
          5);
  add_bcb(&f, NAME_BCB1, VG_OTA_BCB_STATE_PENDING, VG_OTA_BCB_SLOT_B, 6);
  decide(&f, &d);
  assert(d.slot == VG_OTA_BCB_SLOT_A);
  assert(d.reason == VG_OTA_BOOT_REASON_A_PENDING);
  assert(d.bcb_which == 1);
  assert(d.bcb_valid0 == 1 && d.bcb_valid1 == 1);
  fixture_done(&f);
}

static void test_missing(void)
{
  struct fixture f;
  struct vg_ota_boot_decision d;

  fixture_init(&f, 0);
  decide(&f, &d);
  assert(d.slot == VG_OTA_BCB_SLOT_A);
  assert(d.reason == VG_OTA_BOOT_REASON_A_NO_STAGING);
  fixture_done(&f);

  fixture_init(&f, 0);
  add_staging(&f);
  decide(&f, &d);
  assert(d.slot == VG_OTA_BCB_SLOT_A);
  assert(d.reason == VG_OTA_BOOT_REASON_A_NO_BCB);
  fixture_done(&f);
}

static void test_corruption(void)
{
  struct fixture f;
  struct vg_ota_boot_decision d;
  uint8_t *entry;

  fixture_init(&f, 0);
  add_staging(&f);
  add_bcb(&f, NAME_BCB0, VG_OTA_BCB_STATE_CONFIRMED, VG_OTA_BCB_SLOT_B, 2);
  add_bcb(&f, NAME_BCB1, VG_OTA_BCB_STATE_CONFIRMED, VG_OTA_BCB_SLOT_B, 3);
  entry = test_fat_entry_raw(&f.img, f.dir_cluster, NAME_BCB0);
  assert(entry != 0);
  f.img.mem[(f.img.first_data +
             (((uint32_t)entry[26] | ((uint32_t)entry[27] << 8)) - 2u)) *
            512] ^= 0x01;
  decide(&f, &d);
  assert(d.slot == VG_OTA_BCB_SLOT_B);
  assert(d.reason == VG_OTA_BOOT_REASON_B_CONFIRMED);
  assert(d.bcb_valid0 == 0);
  assert(d.bcb_valid1 == 1);
  assert(d.bcb_which == 1);
  fixture_done(&f);

  fixture_init(&f, 0);
  add_staging(&f);
  add_bcb(&f, NAME_BCB0, VG_OTA_BCB_STATE_CONFIRMED, VG_OTA_BCB_SLOT_B, 2);
  add_bcb(&f, NAME_BCB1, VG_OTA_BCB_STATE_CONFIRMED, VG_OTA_BCB_SLOT_B, 3);
  entry = test_fat_entry_raw(&f.img, f.dir_cluster, NAME_BCB0);
  f.img.mem[(f.img.first_data +
             (((uint32_t)entry[26] | ((uint32_t)entry[27] << 8)) - 2u)) *
            512 + 4] ^= 0x01;
  entry = test_fat_entry_raw(&f.img, f.dir_cluster, NAME_BCB1);
  f.img.mem[(f.img.first_data +
             (((uint32_t)entry[26] | ((uint32_t)entry[27] << 8)) - 2u)) *
            512 + 4] ^= 0x01;
  decide(&f, &d);
  assert(d.slot == VG_OTA_BCB_SLOT_A);
  assert(d.reason == VG_OTA_BOOT_REASON_A_BCB_INVALID);
  assert(d.bcb_valid0 == 0 && d.bcb_valid1 == 0);
  fixture_done(&f);
}

static void test_size_and_chain(void)
{
  struct fixture f;
  struct vg_ota_boot_decision d;
  uint8_t *entry;
  uint32_t first;

  fixture_init(&f, 0);
  add_staging(&f);
  add_bcb(&f, NAME_BCB0, VG_OTA_BCB_STATE_CONFIRMED, VG_OTA_BCB_SLOT_B, 2);
  entry = test_fat_entry_raw(&f.img, f.dir_cluster, NAME_BCB0);
  entry[28] = 0xff;
  entry[29] = 0x0f;
  decide(&f, &d);
  assert(d.slot == VG_OTA_BCB_SLOT_A);
  assert(d.reason == VG_OTA_BOOT_REASON_A_BCB_INVALID);
  fixture_done(&f);

  fixture_init(&f, 0);
  add_staging(&f);
  add_bcb(&f, NAME_BCB0, VG_OTA_BCB_STATE_CONFIRMED, VG_OTA_BCB_SLOT_B, 2);
  entry = test_fat_entry_raw(&f.img, f.dir_cluster, NAME_BCB0);
  first = (uint32_t)entry[26] | ((uint32_t)entry[27] << 8);
  test_fat_set_entry(&f.img, first + 3, 0xffff);
  decide(&f, &d);
  assert(d.slot == VG_OTA_BCB_SLOT_A);
  assert(d.reason == VG_OTA_BOOT_REASON_A_BCB_INVALID);
  fixture_done(&f);
}

static void test_seq_wrap(void)
{
  struct fixture f;
  struct vg_ota_boot_decision d;

  fixture_init(&f, 0);
  add_staging(&f);
  add_bcb(&f, NAME_BCB0, VG_OTA_BCB_STATE_CONFIRMED, VG_OTA_BCB_SLOT_A,
          0xffffffffu);
  add_bcb(&f, NAME_BCB1, VG_OTA_BCB_STATE_CONFIRMED, VG_OTA_BCB_SLOT_B, 0);
  decide(&f, &d);
  assert(d.slot == VG_OTA_BCB_SLOT_B);
  assert(d.bcb_which == 1);
  fixture_done(&f);

  fixture_init(&f, 0);
  add_staging(&f);
  add_bcb(&f, NAME_BCB0, VG_OTA_BCB_STATE_CONFIRMED, VG_OTA_BCB_SLOT_B, 0);
  add_bcb(&f, NAME_BCB1, VG_OTA_BCB_STATE_CONFIRMED, VG_OTA_BCB_SLOT_B,
          0x80000000u);
  decide(&f, &d);
  assert(d.slot == VG_OTA_BCB_SLOT_A);
  assert(d.reason == VG_OTA_BOOT_REASON_A_BCB_INVALID);
  fixture_done(&f);

  fixture_init(&f, 0);
  add_staging(&f);
  add_bcb(&f, NAME_BCB0, VG_OTA_BCB_STATE_CONFIRMED, VG_OTA_BCB_SLOT_B,
          0xfffffffeu);
  add_bcb(&f, NAME_BCB1, VG_OTA_BCB_STATE_CONFIRMED, VG_OTA_BCB_SLOT_A, 0);
  decide(&f, &d);
  assert(d.slot == VG_OTA_BCB_SLOT_A);
  assert(d.reason == VG_OTA_BOOT_REASON_SLOT_A);
  assert(d.bcb_which == 1);
  fixture_done(&f);
}

static void test_fat_errors(void)
{
  struct fixture f;
  struct vg_ota_boot_decision d;
  struct vg_ota_bcb bcb;
  uint8_t buf[VG_OTA_BCB_FILE_BYTES];
  uint8_t *entry;

  fixture_init(&f, 0);
  f.img.mem[3] = 'E';
  f.img.mem[4] = 'X';
  f.img.mem[5] = 'F';
  f.img.mem[6] = 'A';
  f.img.mem[7] = 'T';
  decide(&f, &d);
  assert(d.slot == VG_OTA_BCB_SLOT_A);
  assert(d.reason == VG_OTA_BOOT_REASON_A_FAT_ERROR);
  fixture_done(&f);

  fixture_init(&f, 0);
  f.block.fail_enabled = 1;
  f.block.fail_sector = 0;
  f.block.fail_count = 1;
  decide(&f, &d);
  assert(d.slot == VG_OTA_BCB_SLOT_A);
  assert(d.reason == VG_OTA_BOOT_REASON_A_EMMC_ERROR);
  fixture_done(&f);

  fixture_init(&f, 0);
  add_staging(&f);
  add_bcb(&f, NAME_BCB0, VG_OTA_BCB_STATE_CONFIRMED, VG_OTA_BCB_SLOT_B, 1);
  entry = test_fat_entry_raw(&f.img, f.dir_cluster, NAME_BCB0);
  entry[26] = 0;
  entry[27] = 0;
  decide(&f, &d);
  assert(d.slot == VG_OTA_BCB_SLOT_A);
  assert(d.reason == VG_OTA_BOOT_REASON_A_READ_ERROR);
  fixture_done(&f);

  fixture_init(&f, 0);
  fill_bcb(&bcb, VG_OTA_BCB_STATE_CONFIRMED, VG_OTA_BCB_SLOT_B, 1);
  bcb.image_size = VG_OTA_SLOT_BYTES + 1u;
  assert(vg_ota_bcb_encode(&bcb, buf, sizeof(buf)) == -EINVAL);
  fixture_done(&f);
}

static void test_trial_limits(void)
{
  struct fixture f;
  struct vg_ota_boot_decision d;
  uint32_t t;

  for (t = 0; t <= 5; t++)
    {
      fixture_init(&f, 0);
      add_staging(&f);
      add_bcb_with_trials(&f, NAME_BCB0, VG_OTA_BCB_STATE_TRIAL,
                          VG_OTA_BCB_SLOT_B, 10, t);
      decide(&f, &d);
      assert(d.bcb_trial_count == t);
      if (t < VG_OTA_MAX_TRIALS)
        {
          assert(d.slot == VG_OTA_BCB_SLOT_B);
          assert(d.reason == VG_OTA_BOOT_REASON_B_TRIAL);
        }
      else
        {
          assert(d.slot == VG_OTA_BCB_SLOT_A);
          assert(d.reason == VG_OTA_BOOT_REASON_A_FAILED);
        }
      fixture_done(&f);
    }
}

int main(void)
{
  test_boot_b();
  test_boot_a_states();
  test_trial_limits();
  test_missing();
  test_corruption();
  test_size_and_chain();
  test_seq_wrap();
  test_fat_errors();
  printf("test_ota_boot: OK\n");
  return 0;
}
