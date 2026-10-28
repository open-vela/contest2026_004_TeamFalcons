#include <assert.h>
#include <errno.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "test_fat_image.h"
#include "vg_ota_install.h"
#include "vg_ota_sha256.h"

#define IMAGE_SIZE 12291u
struct fixture
{
  struct test_fat_image img;
  struct test_block_ctx block;
  struct vg_ota_install_ports ports;
  uint8_t *flash;
  uint8_t image[IMAGE_SIZE];
  uint32_t dir, bcb_sector[2];
  uint32_t events, cut_at, tear, erases, programs, syncs, writes;
  int fail_erase, fail_program, fail_read, mismatch, fail_sync;
  jmp_buf power_cut;
};

static void put32(uint8_t *p, uint32_t v)
{
  p[0]=(uint8_t)v; p[1]=(uint8_t)(v>>8);
  p[2]=(uint8_t)(v>>16); p[3]=(uint8_t)(v>>24);
}
static void digest(const uint8_t *p, uint32_t n, uint8_t out[32])
{
  struct vg_ota_sha256 s;
  vg_ota_sha256_init(&s); vg_ota_sha256_update(&s,p,n);
  vg_ota_sha256_final(&s,out);
}
static void test_sha(void)
{
  uint8_t d[32]; char hex[65]; uint32_t i;
  struct vg_ota_sha256 s;
  digest((const uint8_t *)"",0,d);
  for (i=0;i<32;i++) sprintf(hex+2*i,"%02x",d[i]);
  assert(!strcmp(hex,"e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"));
  digest((const uint8_t *)"abc",3,d);
  for (i=0;i<32;i++) sprintf(hex+2*i,"%02x",d[i]);
  assert(!strcmp(hex,"ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
  vg_ota_sha256_init(&s);
  for (i=0;i<1000000;i++) vg_ota_sha256_update(&s,(const uint8_t *)"a",1);
  vg_ota_sha256_final(&s,d);
  for (i=0;i<32;i++) sprintf(hex+2*i,"%02x",d[i]);
  assert(!strcmp(hex,"cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0"));
}
static int event(struct fixture *f)
{
  return ++f->events==f->cut_at;
}
static int read_block(void *ctx,uint32_t sector,uint8_t *buf,uint32_t n)
{
  struct fixture *f=ctx;
  return test_block_read(&f->block,sector,buf,n);
}
static int write_block(void *ctx,uint32_t sector,const uint8_t *buf)
{
  struct fixture *f=ctx; int cut=event(f);
  assert(sector==f->bcb_sector[0] || sector==f->bcb_sector[1]);
  f->writes++;
  if (cut)
    {
      if (f->tear) memcpy(f->img.mem+sector*512u,buf,32);
      longjmp(f->power_cut,1);
    }
  memcpy(f->img.mem+sector*512u,buf,512);
  return 0;
}
static int sync_block(void *ctx)
{
  struct fixture *f=ctx;
  if (event(f)) longjmp(f->power_cut,1);
  f->syncs++;
  return f->fail_sync ? -EIO : 0;
}
static uint32_t address_offset(uint32_t addr,uint32_t n)
{
  assert(addr>=VG_OTA_SLOT_B_BASE && addr<VG_OTA_SLOT_B_BASE+VG_OTA_SLOT_BYTES);
  assert(n<=VG_OTA_SLOT_B_BASE+VG_OTA_SLOT_BYTES-addr);
  return addr-VG_OTA_SLOT_A_BASE;
}
static int erase_flash(void *ctx,uint32_t addr,uint32_t n)
{
  struct fixture *f=ctx; uint32_t off=address_offset(addr,n);
  assert(n==VG_OTA_ERASE_BYTES && !(addr%VG_OTA_ERASE_BYTES));
  f->erases++;
  if (event(f))
    {
      if (f->tear) memset(f->flash+off,0xff,n/2);
      longjmp(f->power_cut,1);
    }
  if (f->fail_erase) return -EIO;
  memset(f->flash+off,0xff,n);
  return 0;
}
static int program_flash(void *ctx,uint32_t addr,const uint8_t *p,uint32_t n)
{
  struct fixture *f=ctx; uint32_t i,off=address_offset(addr,n);
  int cut=event(f);
  assert(n<=256 && n>0 && !(addr%256));
  f->programs++;
  if (cut && !f->tear) longjmp(f->power_cut,1);
  if (f->fail_program) return -EIO;
  for (i=0;i<(cut?n/2:n);i++) f->flash[off+i]&=p[i];
  if (cut) longjmp(f->power_cut,1);
  return 0;
}
static int read_flash(void *ctx,uint32_t addr,uint8_t *p,uint32_t n)
{
  struct fixture *f=ctx; uint32_t off=address_offset(addr,n);
  if (event(f)) longjmp(f->power_cut,1);
  if (f->fail_read) return -EIO;
  memcpy(p,f->flash+off,n);
  if (f->mismatch) p[0]^=1;
  return 0;
}
static struct vg_ota_fat_entry find_file(struct fixture *f,const char name[11])
{
  struct vg_ota_fat_volume v; struct vg_ota_fat_entry e;
  assert(!vg_ota_fat_mount(&v,read_block,f,f->img.sectors));
  assert(!vg_ota_fat_find(&v,name,f->dir,&e));
  return e;
}
static uint8_t *file_data(struct fixture *f,const char name[11])
{
  struct vg_ota_fat_entry e=find_file(f,name);
  return f->img.mem+(f->img.first_data+(e.first_cluster-2)*f->img.spc)*512u;
}
static void init(struct fixture *f,int fat32)
{
  struct vg_ota_manifest m; struct vg_ota_bcb b;
  uint8_t buf[4096]; uint32_t i;
  memset(f,0,sizeof(*f));
  assert(!test_fat_init(&f->img,fat32,fat32?300:5000,1));
  f->block.img=&f->img;
  f->dir=test_fat_alloc_chain(&f->img,1);
  assert(!test_fat_dir_add(&f->img,0,"VGOTA      ",0x10,f->dir,0));
  f->flash=malloc(2*VG_OTA_SLOT_BYTES); assert(f->flash);
  memset(f->flash,0x55,VG_OTA_SLOT_BYTES);
  memset(f->flash+VG_OTA_SLOT_BYTES,0xaa,VG_OTA_SLOT_BYTES);
  for (i=0;i<IMAGE_SIZE;i++) f->image[i]=(uint8_t)(i*3u+7u);
  put32(f->image,0x24001000); put32(f->image+4,VG_OTA_SLOT_B_BASE+0x101);
  memset(&m,0,sizeof(m)); m.slot=1; m.image_size=IMAGE_SIZE;
  strcpy(m.version,"0.2.0"); strcpy(m.task_id,"T04-TEST");
  strcpy(m.device_id,"vg-test"); strcpy(m.model,VG_OTA_MODEL);
  strcpy(m.key_id,"fixture"); m.flags=VG_OTA_MANIFEST_FIXTURE;
  m.issued_at=1; m.expires_at=2;
  digest(f->image,IMAGE_SIZE,m.sha256);
  assert(!test_fat_add_file(&f->img,f->dir,"IMAGE   BIN",f->image,IMAGE_SIZE));
  assert(!vg_ota_manifest_encode(&m,buf));
  assert(!test_fat_add_file(&f->img,f->dir,"MANIFESTBIN",buf,512));
  memset(&b,0,sizeof(b)); b.slot=1; b.image_size=IMAGE_SIZE;
  strcpy(b.version,m.version); strcpy(b.task_id,m.task_id);
  memcpy(b.sha256,m.sha256,32); b.flags=VG_OTA_BCB_LOCAL_CONFIRMED;
  b.state=VG_OTA_BCB_STATE_PENDING; b.seq=10;
  assert(!vg_ota_bcb_encode(&b,buf,4096));
  assert(!test_fat_add_file(&f->img,f->dir,"BCB0    BIN",buf,4096));
  b.slot=1; b.state=VG_OTA_BCB_STATE_CONFIRMED; b.seq=9;
  assert(!vg_ota_bcb_encode(&b,buf,4096));
  assert(!test_fat_add_file(&f->img,f->dir,"BCB1    BIN",buf,4096));
  for (i=0;i<2;i++)
    {
      struct vg_ota_fat_entry e=find_file(f,i?"BCB1    BIN":"BCB0    BIN");
      f->bcb_sector[i]=f->img.first_data+e.first_cluster-2;
    }
  f->ports.ctx=f; f->ports.read=read_block; f->ports.sectors=f->img.sectors;
  f->ports.write_sector=write_block; f->ports.sync=sync_block;
  f->ports.erase=erase_flash; f->ports.program=program_flash;
  f->ports.read_flash=read_flash; f->ports.device_id="vg-test";
  f->ports.allow_fixture=1;
}
static int run(struct fixture *f,struct vg_ota_boot_decision *d)
{
  assert(!vg_ota_boot_decide(read_block,f,f->img.sectors,d));
  assert(d->reason==VG_OTA_BOOT_REASON_A_PENDING);
  return vg_ota_install_pending(&f->ports,d);
}
static void done(struct fixture *f)
{
  uint32_t i;
  for (i=0;i<VG_OTA_SLOT_BYTES;i++) assert(f->flash[i]==0x55);
  free(f->flash); test_fat_free(&f->img);
}
static void test_success(void)
{
  struct fixture f; struct vg_ota_boot_decision d; int fat32;
  for (fat32=0;fat32<2;fat32++)
    {
      init(&f,fat32);
      assert(!run(&f,&d));
      assert(d.slot==1 && d.reason==VG_OTA_BOOT_REASON_B_INSTALLED);
      assert(d.bcb_state==VG_OTA_BCB_STATE_TRIAL && d.bcb_seq==12);
      assert(f.writes==2 && f.syncs==2 && f.erases==2);
      assert(!memcmp(f.flash+VG_OTA_SLOT_BYTES,f.image,IMAGE_SIZE));
      assert(!vg_ota_boot_decide(read_block,&f,f.img.sectors,&d));
      assert(d.slot==1 && d.reason==VG_OTA_BOOT_REASON_B_TRIAL);
      done(&f);
    }
}
static void test_reject(void)
{
  struct fixture f; struct vg_ota_boot_decision d; int mode;
  for (mode=0;mode<7;mode++)
    {
      init(&f,0);
      if (mode==0) file_data(&f,"MANIFESTBIN")[48]^=1;
      if (mode==1) file_data(&f,"IMAGE   BIN")[100]^=1;
      if (mode==2) f.ports.device_id="wrong-device";
      if (mode==3) f.ports.allow_fixture=0;
      if (mode==4)
        {
          uint8_t *p=file_data(&f,"BCB0    BIN"); struct vg_ota_bcb b;
          assert(!vg_ota_bcb_decode(p,4096,&b)); b.flags=0;
          assert(!vg_ota_bcb_encode(&b,p,4096));
        }
      if (mode==5)
        {
          uint8_t *p=file_data(&f,"MANIFESTBIN");
          put32(p+12,VG_OTA_SLOT_BYTES+1); put32(p+508,vg_ota_crc32(p,508));
        }
      if (mode==6)
        {
          uint8_t *p=file_data(&f,"MANIFESTBIN");
          p[16]='9'; put32(p+508,vg_ota_crc32(p,508));
        }
      assert(run(&f,&d)<0 && d.slot==0);
      assert(!f.erases && !f.programs && !f.writes);
      done(&f);
    }
}
static void test_errors(void)
{
  struct fixture f; struct vg_ota_boot_decision d; int mode;
  for (mode=0;mode<5;mode++)
    {
      init(&f,0);
      f.fail_erase=mode==0; f.fail_program=mode==1; f.fail_read=mode==2;
      f.mismatch=mode==3; f.fail_sync=mode==4;
      assert(run(&f,&d)<0 && d.slot==0);
      assert(!vg_ota_boot_decide(read_block,&f,f.img.sectors,&d));
      assert(d.slot==0 && d.reason==VG_OTA_BOOT_REASON_A_FAILED);
      if (mode==4) assert(!f.erases);
      done(&f);
    }
}
static void test_power_cuts(void)
{
  struct fixture *f=malloc(sizeof(*f)); struct vg_ota_boot_decision d;
  uint32_t events,cut,tear;
  assert(f); init(f,0); assert(!run(f,&d)); events=f->events; done(f);
  for (tear=0;tear<2;tear++) for (cut=1;cut<=events;cut++)
    {
      init(f,0); f->cut_at=cut; f->tear=tear;
      if (!setjmp(f->power_cut)) { run(f,&d); assert(0); }
      assert(!vg_ota_boot_decide(read_block,f,f->img.sectors,&d));
      if (d.slot==VG_OTA_BCB_SLOT_B)
        {
          assert(d.bcb_state==VG_OTA_BCB_STATE_TRIAL);
          assert(!memcmp(f->flash+VG_OTA_SLOT_BYTES,f->image,IMAGE_SIZE));
        }
      if (f->erases && d.slot==0) assert(d.reason==VG_OTA_BOOT_REASON_A_FAILED);
      done(f);
    }
  printf("test_ota_install: power cut boundaries=%u modes=2\n",events);
  free(f);
}

static void test_trial_advance(void)
{
  struct fixture f;
  struct vg_ota_boot_decision d;

  init(&f, 0);
  assert(!run(&f, &d));
  assert(d.slot == VG_OTA_BCB_SLOT_B && d.reason == VG_OTA_BOOT_REASON_B_INSTALLED);
  assert(d.bcb_trial_count == 1);

  /* Reboot attempt 2 under TRIAL */
  assert(!vg_ota_boot_decide(read_block, &f, f.img.sectors, &d));
  assert(d.slot == VG_OTA_BCB_SLOT_B && d.reason == VG_OTA_BOOT_REASON_B_TRIAL);
  assert(d.bcb_trial_count == 1);

  assert(!vg_ota_trial_advance(&f.ports, &d));
  assert(d.slot == VG_OTA_BCB_SLOT_B && d.reason == VG_OTA_BOOT_REASON_B_TRIAL);
  assert(d.bcb_trial_count == 2);

  /* Reboot attempt 3 under TRIAL */
  assert(!vg_ota_boot_decide(read_block, &f, f.img.sectors, &d));
  assert(d.slot == VG_OTA_BCB_SLOT_B && d.reason == VG_OTA_BOOT_REASON_B_TRIAL);
  assert(d.bcb_trial_count == 2);

  assert(!vg_ota_trial_advance(&f.ports, &d));
  assert(d.slot == VG_OTA_BCB_SLOT_B && d.reason == VG_OTA_BOOT_REASON_B_TRIAL);
  assert(d.bcb_trial_count == 3);

  /* Reboot attempt 4 under TRIAL: trial_count 3 >= MAX_TRIALS -> rolled back to A */
  assert(!vg_ota_boot_decide(read_block, &f, f.img.sectors, &d));
  assert(d.slot == VG_OTA_BCB_SLOT_A && d.reason == VG_OTA_BOOT_REASON_A_FAILED);

  /* Advance persists FAILED to eMMC */
  d.reason = VG_OTA_BOOT_REASON_B_TRIAL;
  assert(!vg_ota_trial_advance(&f.ports, &d));
  assert(d.slot == VG_OTA_BCB_SLOT_A && d.reason == VG_OTA_BOOT_REASON_A_FAILED);
  assert(d.bcb_state == VG_OTA_BCB_STATE_FAILED);

  /* Subsequent boots stay permanently on A */
  assert(!vg_ota_boot_decide(read_block, &f, f.img.sectors, &d));
  assert(d.slot == VG_OTA_BCB_SLOT_A && d.reason == VG_OTA_BOOT_REASON_A_FAILED);
  assert(d.bcb_state == VG_OTA_BCB_STATE_FAILED);

  done(&f);
}

int main(void)
{
  test_sha(); test_success(); test_trial_advance(); test_reject(); test_errors(); test_power_cuts();
  puts("test_ota_install: OK");
  return 0;
}
