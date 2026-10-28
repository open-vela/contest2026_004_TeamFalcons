/*
 * Minimal STM32H750B-DK internal-Flash boot stub for QSPI XIP.
 *
 * The register sequence intentionally mirrors xPack OpenOCD's
 * board/stm32h7x_dual_qspi.cfg, then jumps to the vector table of the main
 * image linked at 0x90000000.
 */

#include <stddef.h>
#include <stdint.h>

#include "stm32h750b_emmc.h"
#include "vg_ota_boot.h"
#include "vg_ota_install.h"
#include "vg_ota_mbox.h"

#define REG32(a) (*(volatile uint32_t *)(uintptr_t)(a))
#define REG16(a) (*(volatile uint16_t *)(uintptr_t)(a))

#define APP_A_BASE            0x90000000u
#define APP_B_BASE            0x90800000u
#define APP_SLOT_SIZE         0x00800000u

#define SRAM_BASE             0x20000000u
#define SRAM_LIMIT            0x40000000u

#define FLASH_ACR             0x52002000u

#define RCC_CR                0x58024400u
#define RCC_CFGR              0x58024410u
#define RCC_D1CFGR            0x58024418u
#define RCC_D2CFGR            0x5802441cu
#define RCC_D3CFGR            0x58024420u
#define RCC_PLLCKSELR         0x58024428u
#define RCC_PLLCFGR           0x5802442cu
#define RCC_PLL1DIVR          0x58024430u
#define RCC_AHB3ENR           0x580244d4u
#define RCC_AHB4ENR           0x580244e0u

#define RCC_CR_PLL1ON         (1u << 24)
#define RCC_CR_PLL1RDY        (1u << 25)

#define GPIOD_BASE            0x58020c00u
#define GPIOF_BASE            0x58021400u
#define GPIOG_BASE            0x58021800u
#define GPIOH_BASE            0x58021c00u

#define GPIO_MODER(base)      ((base) + 0x00u)
#define GPIO_OSPEEDR(base)    ((base) + 0x08u)
#define GPIO_AFRL(base)       ((base) + 0x20u)
#define GPIO_AFRH(base)       ((base) + 0x24u)

#define QSPI_CR               0x52005000u
#define QSPI_DCR              0x52005004u
#define QSPI_SR               0x52005008u
#define QSPI_FCR              0x5200500cu
#define QSPI_DLR              0x52005010u
#define QSPI_CCR              0x52005014u
#define QSPI_AR               0x52005018u
#define QSPI_ABR              0x5200501cu
#define QSPI_DR               0x52005020u
#define QSPI_LPTR             0x52005030u

#define QSPI_CR_EN            (1u << 0)
#define QSPI_CR_ABORT         (1u << 1)
#define QSPI_SR_TEF           (1u << 0)
#define QSPI_SR_TCF           (1u << 1)
#define QSPI_SR_FTF           (1u << 2)
#define QSPI_SR_BUSY          (1u << 5)
#define QSPI_SR_FLEVEL_SHIFT  (8)
#define QSPI_SR_FLEVEL_MASK   (0x1fu << QSPI_SR_FLEVEL_SHIFT)
#define QSPI_FCR_CTEF         (1u << 0)
#define QSPI_FCR_CTCF         (1u << 1)
#define QSPI_FCR_CSMF         (1u << 3)
#define QSPI_FCR_CTOF         (1u << 4)
#define QSPI_CCR_IMODE_1      (1u << 8)
#define QSPI_CCR_ADMODE_1     (1u << 10)
#define QSPI_CCR_ADSIZE_32    (3u << 12)
#define QSPI_CCR_DMODE_1      (1u << 24)
#define QSPI_CCR_FMODE_INDRD  (1u << 26)
#define QSPI_CCR_DCYC(n)      ((uint32_t)(n) << 18)

#define SCB_VTOR              0xe000ed08u

extern uint32_t _estack;

void Reset_Handler(void);
void Default_Handler(void);

__attribute__((section(".vectors"), used, aligned(512)))
void (* const g_vectors[])(void) =
{
  (void (*)(void))&_estack,
  Reset_Handler,
  Default_Handler,
  Default_Handler,
  Default_Handler,
  Default_Handler,
  Default_Handler,
  0,
  0,
  0,
  0,
  Default_Handler,
  Default_Handler,
  0,
  Default_Handler,
  Default_Handler
};

static void mmw(uint32_t addr, uint32_t setbits, uint32_t clearbits)
{
  REG32(addr) = (REG32(addr) & ~clearbits) | setbits;
}

static void delay_cycles(uint32_t count)
{
  while (count-- > 0)
    {
      __asm__ volatile ("nop");
    }
}

static void short_delay(void)
{
  delay_cycles(240000u);
}

static void qspi_wait_idle(void)
{
  uint32_t timeout = 1000000u;

  while ((REG32(QSPI_SR) & QSPI_SR_BUSY) != 0 && timeout-- > 0)
    {
      __asm__ volatile ("nop");
    }
}

static void qspi_abort(void)
{
  mmw(QSPI_CR, QSPI_CR_ABORT, 0);
  qspi_wait_idle();
  short_delay();
}

static void clock_init(void)
{
  mmw(FLASH_ACR, 0x00000004u, 0x0000000bu);

  mmw(RCC_CR, 0x00000001u, 0x00000018u);
  mmw(RCC_CFGR, 0x10000000u, 0xee000007u);
  REG32(RCC_D1CFGR) = 0x00000040u;
  REG32(RCC_D2CFGR) = 0x00000440u;
  REG32(RCC_D3CFGR) = 0x00000040u;
  REG32(RCC_PLLCKSELR) = 0x00000040u;
  mmw(RCC_PLLCFGR, 0x0001000cu, 0x00000002u);
  REG32(RCC_PLL1DIVR) = 0x01070217u;

  mmw(RCC_CR, RCC_CR_PLL1ON, 0);
  while ((REG32(RCC_CR) & RCC_CR_PLL1RDY) == 0)
    {
    }

  mmw(RCC_CFGR, 0x00000003u, 0);
  short_delay();
}

static void qspi_gpio_init(void)
{
  mmw(RCC_AHB4ENR, 0x000007ffu, 0);
  mmw(RCC_AHB3ENR, 0x00004000u, 0);
  short_delay();

  mmw(GPIO_MODER(GPIOD_BASE),   0x00800000u, 0x00400000u);
  mmw(GPIO_OSPEEDR(GPIOD_BASE), 0x00c00000u, 0x00000000u);
  mmw(GPIO_AFRH(GPIOD_BASE),    0x00009000u, 0x00006000u);

  mmw(GPIO_MODER(GPIOF_BASE),   0x0028a000u, 0x00145000u);
  mmw(GPIO_OSPEEDR(GPIOF_BASE), 0x003cf000u, 0x00000000u);
  mmw(GPIO_AFRL(GPIOF_BASE),    0x99000000u, 0x66000000u);
  mmw(GPIO_AFRH(GPIOF_BASE),    0x000009a0u, 0x00000650u);

  mmw(GPIO_MODER(GPIOG_BASE),   0x20082000u, 0x10041000u);
  mmw(GPIO_OSPEEDR(GPIOG_BASE), 0x200c2000u, 0x10001000u);
  mmw(GPIO_AFRL(GPIOG_BASE),    0x0a000000u, 0x05000000u);
  mmw(GPIO_AFRH(GPIOG_BASE),    0x09000090u, 0x06000060u);

  mmw(GPIO_MODER(GPIOH_BASE),   0x000000a0u, 0x00000050u);
  mmw(GPIO_OSPEEDR(GPIOH_BASE), 0x000000f0u, 0x00000000u);
  mmw(GPIO_AFRL(GPIOH_BASE),    0x00009900u, 0x00006600u);
}

static void qspi_enter_memory_mapped(void)
{
  /* Dual-QSPI SPI (1-1-1) memory-mapped mode.
   *
   * Hardware evidence on this STM32H750B-DK:
   * - SPI mmap (CCR=0x0d003513) returns a valid vector table at 0x90000000
   * - QPI mmap (CCR=0x0f283fec) returns 0x88888888 and cannot boot XIP
   * - OpenOCD stmqspi also cannot JEDEC-probe the dual MT25TL01G
   *
   * Mirror OpenOCD board/stm32h7x_dual_qspi.cfg qspi_init 0 (SPI path).
   */
  qspi_gpio_init();

  REG32(QSPI_CR) = 0x05500058u;
  REG32(QSPI_DCR) = 0x001a0200u;

  REG32(QSPI_LPTR) = 0x00001000u;
  REG32(QSPI_CCR) = 0x0d002503u;
  mmw(QSPI_CR, QSPI_CR_EN, 0);

  /* Exit QPI in case a previous image left the chips in QPI mode. */
  qspi_abort();
  REG32(QSPI_CCR) = 0x000003f5u;
  short_delay();

  /* Memory-mapped READ with 4-byte addresses (SPI, not QPI). */
  qspi_abort();
  REG32(QSPI_CCR) = 0x0d003513u;
  short_delay();
}

static void qspi_clear_flags(void)
{
  REG32(QSPI_FCR) = QSPI_FCR_CTEF | QSPI_FCR_CTCF |
                    QSPI_FCR_CSMF | QSPI_FCR_CTOF;
}

static int qspi_wait_tcf(void)
{
  uint32_t timeout = 20000000u;

  while (timeout-- > 0)
    {
      uint32_t sr = REG32(QSPI_SR);

      if ((sr & QSPI_SR_TEF) != 0)
        {
          return -1;
        }

      if ((sr & QSPI_SR_TCF) != 0)
        {
          return 0;
        }
    }

  return -1;
}

static void qspi_select_flash(uint32_t flash)
{
  /* Keep dual-flash mode and QSPI enabled; FSELECT chooses the device. */
  REG32(QSPI_CR) = 0x05500059u | (flash != 0 ? (1u << 7) : 0u);
}

static void qspi_select_dual(void)
{
  REG32(QSPI_CR) = 0x05500059u;
}

static int qspi_read_data(uint8_t *buf, uint32_t len)
{
  volatile uint8_t *dr = (volatile uint8_t *)QSPI_DR;
  uint32_t remaining = len;
  uint32_t timeout;
  uint32_t sr;

  while (remaining > 0)
    {
      timeout = 20000000u;
      for (; ; )
        {
          sr = REG32(QSPI_SR);
          if ((sr & QSPI_SR_TEF) != 0)
            {
              return -1;
            }

          if (((sr & QSPI_SR_FLEVEL_MASK) >> QSPI_SR_FLEVEL_SHIFT) != 0 ||
              (sr & QSPI_SR_TCF) != 0)
            {
              break;
            }

          if (timeout-- == 0)
            {
              return -1;
            }
        }

      while (remaining > 0)
        {
          sr = REG32(QSPI_SR);
          if (((sr & QSPI_SR_FLEVEL_MASK) >> QSPI_SR_FLEVEL_SHIFT) == 0)
            {
              break;
            }

          *buf++ = dr[0];
          remaining--;
        }

      if ((REG32(QSPI_SR) & QSPI_SR_TCF) != 0)
        {
          break;
        }
    }

  if (remaining != 0)
    {
      return -1;
    }

  if (qspi_wait_tcf() != 0)
    {
      return -1;
    }

  qspi_clear_flags();
  return 0;
}

static int qspi_write_data(const uint8_t *buf, uint32_t len)
{
  volatile uint8_t *dr = (volatile uint8_t *)QSPI_DR;
  uint32_t remaining = len;
  uint32_t timeout;

  while (remaining > 0)
    {
      timeout = 20000000u;
      while ((REG32(QSPI_SR) & QSPI_SR_FTF) == 0)
        {
          if ((REG32(QSPI_SR) & QSPI_SR_TEF) != 0)
            {
              return -1;
            }

          if (timeout-- == 0)
            {
              return -1;
            }
        }

      *dr = *buf++;
      remaining--;
    }

  if (qspi_wait_tcf() != 0)
    {
      return -1;
    }

  qspi_clear_flags();
  return 0;
}

static int qspi_indirect_read(uint8_t inst, uint32_t addr, int has_addr,
                              uint32_t dummy, uint8_t *buf, uint32_t len)
{
  uint32_t ccr;

  if (len == 0)
    {
      return -1;
    }

  qspi_clear_flags();
  REG32(QSPI_DLR) = len - 1u;

  ccr = (uint32_t)inst | QSPI_CCR_IMODE_1 | QSPI_CCR_DMODE_1 |
        QSPI_CCR_FMODE_INDRD | QSPI_CCR_DCYC(dummy);
  if (has_addr)
    {
      ccr |= QSPI_CCR_ADMODE_1 | QSPI_CCR_ADSIZE_32;
    }

  REG32(QSPI_CCR) = ccr;
  if (has_addr)
    {
      REG32(QSPI_AR) = addr;
    }

  if (qspi_read_data(buf, len) != 0)
    {
      qspi_abort();
      qspi_clear_flags();
      return -1;
    }

  qspi_wait_idle();
  return 0;
}

static int qspi_indirect_write(uint8_t inst, uint32_t addr,
                               const uint8_t *buf, uint32_t len)
{
  if (len == 0)
    {
      return -1;
    }

  qspi_clear_flags();
  REG32(QSPI_DLR) = len - 1u;
  REG32(QSPI_CCR) = (uint32_t)inst | QSPI_CCR_IMODE_1 |
                    QSPI_CCR_ADMODE_1 | QSPI_CCR_ADSIZE_32 |
                    QSPI_CCR_DMODE_1;
  REG32(QSPI_AR) = addr;

  if (qspi_write_data(buf, len) != 0)
    {
      qspi_abort();
      qspi_clear_flags();
      return -1;
    }

  qspi_wait_idle();
  return 0;
}

static int qspi_no_data(uint8_t inst)
{
  qspi_clear_flags();
  REG32(QSPI_CCR) = (uint32_t)inst | QSPI_CCR_IMODE_1;

  if (qspi_wait_tcf() != 0)
    {
      qspi_abort();
      qspi_clear_flags();
      return -1;
    }

  qspi_wait_idle();
  qspi_clear_flags();
  return 0;
}

static int qspi_no_data_addr(uint8_t inst, uint32_t addr)
{
  qspi_clear_flags();
  REG32(QSPI_CCR) = (uint32_t)inst | QSPI_CCR_IMODE_1 |
                    QSPI_CCR_ADMODE_1 | QSPI_CCR_ADSIZE_32;
  REG32(QSPI_AR) = addr;

  if (qspi_wait_tcf() != 0)
    {
      qspi_abort();
      qspi_clear_flags();
      return -1;
    }

  qspi_wait_idle();
  qspi_clear_flags();
  return 0;
}

static int install_range(uint32_t address, uint32_t len)
{
  return address>=APP_B_BASE && address<APP_B_BASE+APP_SLOT_SIZE &&
         len>0 && len<=APP_B_BASE+APP_SLOT_SIZE-address;
}

static int install_ready(void)
{
  uint32_t i;
  uint8_t status[2];
  for (i=0; i<200000u; i++)
    {
      if (qspi_indirect_read(0x05,0,0,0,status,2)) return -1;
      if (!((status[0]|status[1])&1u)) return 0;
    }
  return -1;
}

static int install_write_enable(void)
{
  uint8_t status[2];
  if (install_ready() || qspi_no_data(0x06) ||
      qspi_indirect_read(0x05,0,0,0,status,2)) return -1;
  return (status[0]&2u) && (status[1]&2u) ? 0 : -1;
}

static int install_erase(void *ctx, uint32_t address, uint32_t len)
{
  uint8_t id[6];
  (void)ctx;
  if (!install_range(address,len) || len!=VG_OTA_ERASE_BYTES ||
      address%VG_OTA_ERASE_BYTES) return -1;
  qspi_abort();
  qspi_select_dual();
  /* DFM 将两颗器件的字节交错；ID/状态也必须读取偶数字节。 */
  if (qspi_indirect_read(0x9f,0,0,0,id,6) ||
      id[0]!=0x20 || id[1]!=0x20 || id[2]!=0xba || id[3]!=0xba ||
      id[4]!=0x20 || id[5]!=0x20) return -1;
  if (install_write_enable() ||
      qspi_no_data_addr(0x21,address-APP_A_BASE)) return -1;
  return install_ready();
}

static int install_program(void *ctx, uint32_t address,
                            const uint8_t *data, uint32_t len)
{
  uint8_t page[VG_OTA_PROGRAM_BYTES];
  uint32_t i;
  (void)ctx;
  if (!data || !install_range(address,VG_OTA_PROGRAM_BYTES) ||
      !len || len>sizeof(page) || address%VG_OTA_PROGRAM_BYTES) return -1;
  for (i=0; i<sizeof(page); i++) page[i]=i<len ? data[i] : 0xff;
  qspi_abort();
  qspi_select_dual();
  if (install_write_enable() ||
      qspi_indirect_write(0x12,address-APP_A_BASE,page,sizeof(page))) return -1;
  return install_ready();
}

static int install_read_flash(void *ctx, uint32_t address,
                               uint8_t *data, uint32_t len)
{
  uint32_t i;
  (void)ctx;
  if (!data || !install_range(address,len)) return -1;
  qspi_enter_memory_mapped();
  for (i=0; i<len; i++) data[i]=*(volatile uint8_t *)(uintptr_t)(address+i);
  return 0;
}

static void install_progress(void *ctx, uint32_t stage, uint32_t bytes)
{
  (void)ctx;
  VG_OTA_MBOX->install_stage=stage;
  VG_OTA_MBOX->install_bytes=bytes;
  __asm__ volatile ("dsb 0xf" ::: "memory");
}

static void hardware_device_id(char out[32])
{
  static const char hex[]="0123456789abcdef";
  volatile const uint8_t *uid=(volatile const uint8_t *)0x1ff1e800u;
  uint32_t i;
  out[0]='v'; out[1]='g'; out[2]='-';
  for (i=0; i<12; i++)
    {
      out[3+i*2]=hex[uid[i]>>4];
      out[4+i*2]=hex[uid[i]&15u];
    }
  out[27]=0;
}

static int qspi_scratch_one(uint32_t flash, uint32_t addr)
{
  volatile struct vg_ota_mbox *mbox = VG_OTA_MBOX;
  volatile uint8_t *mem;
  uint8_t pattern[256];
  uint8_t readback[256];
  uint32_t i;

  qspi_abort();
  qspi_select_flash(flash);

  for (i = 0; i < sizeof(pattern); i++)
    {
      pattern[i] = (uint8_t)(0xa5u ^ i ^ (addr >> 16));
    }

  if (qspi_no_data(0x06) != 0)
    {
      return 1;
    }

  if (qspi_no_data_addr(0x21, addr) != 0)
    {
      return 2;
    }

  /* A 4 KiB sector erase is typically tens of milliseconds; allow ample
   * time because this diagnostic does not poll the status register.
   */

  delay_cycles(200000000u);

  qspi_enter_memory_mapped();
  mem = (volatile uint8_t *)(0x90000000u + addr);
  for (i = 0; i < sizeof(readback); i++)
    {
      readback[i] = mem[i];
    }

  for (i = 0; i < sizeof(readback); i++)
    {
      if (readback[i] != 0xffu)
        {
          mbox->fail_index = i;
          mbox->fail_got = readback[i];
          mbox->fail_want = 0xffu;
          return 5;
        }
    }

  qspi_abort();
  qspi_select_flash(flash);

  if (qspi_no_data(0x06) != 0)
    {
      return 6;
    }

  if (qspi_indirect_write(0x12, addr, pattern, sizeof(pattern)) != 0)
    {
      return 7;
    }

  delay_cycles(20000000u);

  qspi_enter_memory_mapped();
  mem = (volatile uint8_t *)(0x90000000u + addr);
  for (i = 0; i < sizeof(readback); i++)
    {
      readback[i] = mem[i];
    }

  for (i = 0; i < sizeof(readback); i++)
    {
      if (readback[i] != pattern[i])
        {
          mbox->fail_index = i;
          mbox->fail_got = readback[i];
          mbox->fail_want = pattern[i];
          return 10;
        }
    }

  return 0;
}

static int qspi_probe_run(void)
{
  volatile struct vg_ota_mbox *mbox = VG_OTA_MBOX;
  uint8_t id[3];
  uint32_t fsize;
  int has_flash2 = 0;
  int ret;

  mbox->state = VG_OTA_MBOX_RUNNING;
  mbox->run_seq = mbox->seq;
  mbox->error = 0;
  mbox->jedec1 = 0;
  mbox->jedec2 = 0;
  mbox->capacity = 0;
  mbox->erase_size = 4096;
  mbox->write_size = 256;
  mbox->sr = 0;
  mbox->cr = 0;
  mbox->ccr = 0;
  mbox->dcr = 0;
  mbox->raw1 = 0;
  mbox->raw2 = 0;
  mbox->fail_index = 0;
  mbox->fail_got = 0;
  mbox->fail_want = 0;

  qspi_select_flash(0);
  if (qspi_indirect_read(0x9f, 0, 0, 0, id, sizeof(id)) != 0)
    {
      mbox->error = 1;
      goto fail;
    }

  mbox->raw1 = ((uint32_t)id[0] << 16) |
               ((uint32_t)id[1] << 8) | id[2];
  mbox->jedec1 = ((uint32_t)id[0] << 16) |
                 ((uint32_t)id[2] << 8) | id[1];

  qspi_select_flash(1);
  if (qspi_indirect_read(0x9f, 0, 0, 0, id, sizeof(id)) != 0)
    {
      /* The second device may not be populated.  Its absence is a valid
       * P0 result; keep the single-flash capacity and test flash 1.
       */

      qspi_abort();
      qspi_clear_flags();
      has_flash2 = 0;
    }
  else
    {
      mbox->raw2 = ((uint32_t)id[0] << 16) |
                   ((uint32_t)id[1] << 8) | id[2];
      mbox->jedec2 = ((uint32_t)id[0] << 16) |
                     ((uint32_t)id[2] << 8) | id[1];
      has_flash2 = 1;
    }

  fsize = (REG32(QSPI_DCR) >> 16) & 0x1fu;
  mbox->capacity = 1u << fsize;
  if (has_flash2)
    {
      mbox->capacity <<= 1;
    }

  ret = qspi_scratch_one(0, 0x03ff0000u);
  if (ret != 0)
    {
      mbox->error = 10u + (uint32_t)ret;
      goto fail;
    }

  if (has_flash2)
    {
      ret = qspi_scratch_one(1, 0x03ff0000u);
      if (ret != 0)
        {
          mbox->error = 20u + (uint32_t)ret;
          goto fail;
        }
    }

  qspi_select_dual();
  mbox->state = VG_OTA_MBOX_OK;
  return 0;

fail:
  mbox->sr = REG32(QSPI_SR);
  mbox->cr = REG32(QSPI_CR);
  mbox->ccr = REG32(QSPI_CCR);
  mbox->dcr = REG32(QSPI_DCR);
  qspi_select_dual();
  mbox->state = VG_OTA_MBOX_FAIL;
  return -1;
}

static int valid_sram(uint32_t value)
{
  /* Cortex-M loads the initial MSP from the vector table and requires a
   * word-aligned value.  NuttX may place its early stack on a 4-byte boundary
   * before establishing the ABI-aligned task stack.
   */

  return value >= SRAM_BASE && value < SRAM_LIMIT && (value & 3u) == 0;
}

static int valid_entry(uint32_t base, uint32_t value)
{
  uint32_t addr = value & ~1u;

  return addr >= base && addr < base + APP_SLOT_SIZE && (value & 1u) != 0;
}

static int valid_vector(uint32_t base)
{
  uint32_t stack = REG32(base);
  uint32_t entry = REG32(base + 4u);

  return valid_sram(stack) && valid_entry(base, entry);
}

__attribute__((naked, noreturn))
static void jump_to_image(uint32_t, uint32_t)
{
  __asm__ volatile
    (
      "msr msp, r0\n"
      "msr psp, r0\n"
      "movs r0, #0\n"
      "msr control, r0\n"
      "isb\n"
      "bx r1\n"
    );
}

extern uint32_t _sidata[];
extern uint32_t _sdata[];
extern uint32_t _edata[];
extern uint32_t _sbss[];
extern uint32_t _ebss[];

static void init_data_bss(void)
{
  uint32_t *src = _sidata;
  uint32_t *dst;

  for (dst = _sdata; dst < _edata; )
    {
      *dst++ = *src++;
    }

  for (dst = _sbss; dst < _ebss; )
    {
      *dst++ = 0;
    }
}

static void zero_boot_decision(struct vg_ota_boot_decision *d)
{
  uint8_t *p = (uint8_t *)d;
  uint32_t i;

  for (i = 0; i < sizeof(*d); i++)
    {
      p[i] = 0;
    }

  d->slot = VG_OTA_BCB_SLOT_A;
  d->reason = VG_OTA_BOOT_REASON_A_EMMC_ERROR;
  d->bcb_which = 0xffu;
}

static void publish_boot(const struct vg_ota_boot_decision *d,
                         uint32_t slot, uint32_t reason)
{
  volatile struct vg_ota_mbox *mbox = VG_OTA_MBOX;

  mbox->magic = VG_OTA_MBOX_MAGIC;
  mbox->version = VG_OTA_MBOX_VERSION;
  mbox->boot_slot = slot;
  mbox->boot_reason = reason;
  mbox->boot_bcb_which = d->bcb_which;
  mbox->boot_bcb_state = d->bcb_state;
  mbox->boot_bcb_seq = d->bcb_seq;
  mbox->boot_bcb_slot = d->bcb_slot;
  mbox->boot_valid0 = d->bcb_valid0;
  mbox->boot_valid1 = d->bcb_valid1;
  mbox->boot_bcb_error = d->bcb_error;
  mbox->boot_fat_type = d->fat_type;
  mbox->boot_fat_error = d->fat_error;
  mbox->boot_emmc_error = d->emmc_error;
  mbox->install_stage = d->install_stage;
  mbox->install_error = d->install_error;
  mbox->install_bytes = d->install_bytes;
  mbox->boot_bcb_trials = d->bcb_trial_count;

  __asm__ volatile ("dsb 0xf" ::: "memory");
}

static void trial_watchdog_enable(void)
{
  /* STM32H7 IWDG1 at 0x58004800:
   * 1. Write 0x5555 to KR to enable access to PR/RLR
   * 2. Set PR = 6 (/256)
   * 3. Set RLR = 0xFFF (4095) -> ~32 seconds timeout
   * 4. Write 0xAAAA to KR to reload
   * 5. Write 0xCCCC to KR to start counter and enable LSI
   */
  REG32(0x58004800u) = 0x5555u;
  REG32(0x58004804u) = 6u;
  REG32(0x58004808u) = 0xfffu;
  REG32(0x58004800u) = 0xaaaau;
  REG32(0x58004800u) = 0xccccu;
}

static void boot_select(void)
{
  struct vg_ota_boot_decision d;
  uint32_t target = APP_A_BASE;
  uint32_t slot = VG_OTA_BCB_SLOT_A;
  uint32_t reason;
  char device_id[32];

  if (vg_emmc_init() == 0)
    {
      vg_ota_boot_decide(vg_emmc_read_blocks, 0, vg_emmc_sector_count(), &d);
      if (d.reason==VG_OTA_BOOT_REASON_A_PENDING)
        {
          struct vg_ota_install_ports p={0};
          hardware_device_id(device_id);
          p.read=vg_emmc_read_blocks; p.sectors=vg_emmc_sector_count();
          p.write_sector=vg_emmc_write_sector; p.sync=vg_emmc_sync;
          p.erase=install_erase; p.program=install_program;
          p.read_flash=install_read_flash; p.device_id=device_id;
          p.allow_fixture=VG_OTA_STUB_FIXTURES;
          p.progress=install_progress;
          vg_ota_install_pending(&p,&d);
          qspi_abort();
          qspi_select_dual();
          install_ready();
          qspi_enter_memory_mapped();
        }
      else if (d.reason == VG_OTA_BOOT_REASON_B_TRIAL)
        {
          struct vg_ota_install_ports p = {0};
          p.read = vg_emmc_read_blocks;
          p.sectors = vg_emmc_sector_count();
          p.write_sector = vg_emmc_write_sector;
          p.sync = vg_emmc_sync;
          vg_ota_trial_advance(&p, &d);
        }
      if (d.emmc_error == 0 && vg_emmc_error() != 0)
        {
          d.emmc_error = vg_emmc_error();
        }
    }
  else
    {
      zero_boot_decision(&d);
      d.emmc_error = vg_emmc_error();
    }

  reason = d.reason;

  if (d.slot == VG_OTA_BCB_SLOT_B)
    {
      if (valid_vector(APP_B_BASE))
        {
          target = APP_B_BASE;
          slot = VG_OTA_BCB_SLOT_B;
        }
      else
        {
          target = APP_A_BASE;
          slot = VG_OTA_BCB_SLOT_A;
          reason = VG_OTA_BOOT_REASON_A_IMAGE_INVALID;
        }
    }

  if (!valid_vector(target))
    {
      publish_boot(&d, VG_OTA_BCB_SLOT_A, VG_OTA_BOOT_REASON_A_SAFE_STOP);
      Default_Handler();
    }

  publish_boot(&d, slot, reason);

  if (slot == VG_OTA_BCB_SLOT_B &&
      (reason == VG_OTA_BOOT_REASON_B_TRIAL ||
       reason == VG_OTA_BOOT_REASON_B_INSTALLED))
    {
      trial_watchdog_enable();
    }

  REG32(SCB_VTOR) = target;
  __asm__ volatile ("dsb 0xf" ::: "memory");
  __asm__ volatile ("isb 0xf" ::: "memory");
  jump_to_image(REG32(target), REG32(target + 4u));
}

void Reset_Handler(void)
{
  volatile struct vg_ota_mbox *mbox = VG_OTA_MBOX;

  __asm__ volatile ("cpsid i");

  init_data_bss();
  clock_init();
  qspi_enter_memory_mapped();

  if (mbox->magic == VG_OTA_MBOX_MAGIC &&
      mbox->version == VG_OTA_MBOX_VERSION &&
      mbox->request == VG_OTA_MBOX_REQ_QSPI_PROBE &&
      mbox->state == VG_OTA_MBOX_REQUESTED)
    {
      qspi_abort();
      qspi_probe_run();
      mbox->request = VG_OTA_MBOX_REQ_NONE;
      qspi_enter_memory_mapped();
    }

  boot_select();
}

/* GCC may emit these for structure copies even with -ffreestanding. */
void *memcpy(void *dst, const void *src, size_t len)
{
  uint8_t *d = dst;
  const uint8_t *s = src;

  while (len-- > 0)
    {
      *d++ = *s++;
    }

  return dst;
}

void *memset(void *dst, int c, size_t len)
{
  uint8_t *d = dst;

  while (len-- > 0)
    {
      *d++ = (uint8_t)c;
    }

  return dst;
}

int memcmp(const void *a, const void *b, size_t n)
{
  const uint8_t *x=a, *y=b;
  while (n--) { if (*x!=*y) return (int)*x-(int)*y; x++; y++; }
  return 0;
}

int strcmp(const char *a, const char *b)
{
  while (*a && *a==*b) { a++; b++; }
  return (int)(uint8_t)*a-(int)(uint8_t)*b;
}

void Default_Handler(void)
{
  for (; ; )
    {
      __asm__ volatile ("wfi");
    }
}
