/*
 * Minimal polling SDMMC1 driver for the soldered eMMC on STM32H750B-DK.
 *
 * Only 1-bit, default-speed, single/multi-block reads are implemented.
 * The boot stub runs with interrupts disabled, so every wait is a bounded
 * poll.  A failure is reported to the caller, which falls back to slot A;
 * this driver never writes to the card.
 */

#include <stdint.h>

#include "stm32h750b_emmc.h"

#define REG32(a) (*(volatile uint32_t *)(uintptr_t)(a))
#define REG16(a) (*(volatile uint16_t *)(uintptr_t)(a))

#define RCC_CR            0x58024400u
#define RCC_PLLCKSELR     0x58024428u
#define RCC_PLL1DIVR      0x58024430u
#define RCC_D1CCIPR       0x5802444cu
#define RCC_AHB3ENR       0x580244d4u
#define RCC_AHB4ENR       0x580244e0u

#define RCC_CR_PLL1ON     (1u << 24)
#define RCC_AHB3ENR_SDMMC1EN (1u << 16)
#define RCC_AHB4ENR_GPIOCEN  (1u << 2)
#define RCC_AHB4ENR_GPIODEN  (1u << 3)

#define GPIOC_BASE        0x58020800u
#define GPIOD_BASE        0x58020c00u

#define GPIO_MODER(base)  ((base) + 0x00u)
#define GPIO_OSPEEDR(base) ((base) + 0x08u)
#define GPIO_PUPDR(base)  ((base) + 0x0cu)
#define GPIO_AFRL(base)   ((base) + 0x20u)
#define GPIO_AFRH(base)   ((base) + 0x24u)

#define SDMMC_BASE        0x52007000u
#define SDMMC_POWER       (SDMMC_BASE + 0x00u)
#define SDMMC_CLKCR       (SDMMC_BASE + 0x04u)
#define SDMMC_ARG         (SDMMC_BASE + 0x08u)
#define SDMMC_CMD         (SDMMC_BASE + 0x0cu)
#define SDMMC_RESP1       (SDMMC_BASE + 0x14u)
#define SDMMC_DTIMER      (SDMMC_BASE + 0x24u)
#define SDMMC_DLEN        (SDMMC_BASE + 0x28u)
#define SDMMC_DCTRL       (SDMMC_BASE + 0x2cu)
#define SDMMC_STA         (SDMMC_BASE + 0x34u)
#define SDMMC_ICR         (SDMMC_BASE + 0x38u)
#define SDMMC_MASK        (SDMMC_BASE + 0x3cu)
#define SDMMC_FIFO        (SDMMC_BASE + 0x80u)

#define SDMMC_POWER_ON    3u

#define SDMMC_CLKCR_CLKDIV_MASK 0x3ffu

#define SDMMC_CMD_WAITRESP_NONE    (0u << 8)
#define SDMMC_CMD_WAITRESP_SHORT   (1u << 8)
#define SDMMC_CMD_WAITRESP_SHORT_NOCRC (2u << 8)
#define SDMMC_CMD_WAITRESP_LONG    (3u << 8)
#define SDMMC_CMD_CMDTRANS         (1u << 6)
#define SDMMC_CMD_CMDSTOP          (1u << 7)
#define SDMMC_CMD_CPSMEN           (1u << 12)

#define SDMMC_DCTRL_DTEN           (1u << 0)
#define SDMMC_DCTRL_DTDIR          (1u << 1)
#define SDMMC_DCTRL_DBLOCKSIZE_512 (9u << 4)

#define SDMMC_STA_CCRCFAIL         (1u << 0)
#define SDMMC_STA_DCRCFAIL         (1u << 1)
#define SDMMC_STA_CTIMEOUT         (1u << 2)
#define SDMMC_STA_DTIMEOUT         (1u << 3)
#define SDMMC_STA_TXUNDERR         (1u << 4)
#define SDMMC_STA_RXOVERR          (1u << 5)
#define SDMMC_STA_CMDREND          (1u << 6)
#define SDMMC_STA_CMDSENT          (1u << 7)
#define SDMMC_STA_DATAEND          (1u << 8)
#define SDMMC_STA_DPSMACT          (1u << 12)
#define SDMMC_STA_CPSMACT          (1u << 13)
#define SDMMC_STA_RXFIFOE          (1u << 19)
#define SDMMC_STA_IDMATE           (1u << 27)

#define SDMMC_ICR_CLEAR_ALL        0x1fe01fffu

#define SDMMC_ERR_MASK             (SDMMC_STA_CCRCFAIL | \
                                    SDMMC_STA_DCRCFAIL | \
                                    SDMMC_STA_CTIMEOUT | \
                                    SDMMC_STA_DTIMEOUT | \
                                    SDMMC_STA_TXUNDERR | \
                                    SDMMC_STA_RXOVERR | \
                                    SDMMC_STA_IDMATE)

#define SDMMC_INIT_KHZ             400u
#define SDMMC_DATA_KHZ             20000u

static uint32_t g_sectors;
static uint32_t g_error;
static uint32_t g_sector_mode;
static uint32_t g_data_stage;

static void delay_cycles(uint32_t count)
{
  while (count-- > 0)
    {
      __asm__ volatile ("nop");
    }
}

static void mmw(uint32_t addr, uint32_t setbits, uint32_t clearbits)
{
  REG32(addr) = (REG32(addr) & ~clearbits) | setbits;
}

static uint32_t pll1q_khz(void)
{
  uint32_t sel;
  uint32_t src_khz;
  uint32_t divm;
  uint32_t divn;
  uint32_t divq;
  uint32_t vco_khz;

  if ((REG32(RCC_CR) & RCC_CR_PLL1ON) == 0)
    {
      return 0;
    }

  /* SDMMC uses PLL1Q unless D1CCIPR selects PLL2, which this stub does
   * not configure.
   */

  if ((REG32(RCC_D1CCIPR) & (1u << 16)) != 0)
    {
      return 0;
    }

  sel = REG32(RCC_PLLCKSELR);
  switch (sel & 3u)
    {
      case 0:
        src_khz = 64000u; /* HSI */
        break;
      case 2:
        src_khz = 25000u; /* HSE, 25 MHz on this board */
        break;
      default:
        return 0;
    }

  divm = (sel >> 4) & 0x3fu;

  /* PLL DIVN/DIVP/DIVQ/DIVR fields store divider - 1. */
  divn = (REG32(RCC_PLL1DIVR) & 0x1ffu) + 1u;
  divq = ((REG32(RCC_PLL1DIVR) >> 16) & 0x7fu) + 1u;

  if (divm == 0 || divn == 0 || divq == 0)
    {
      return 0;
    }

  vco_khz = (src_khz / divm) * divn;
  return vco_khz / divq;
}

static uint32_t clock_divider(uint32_t kernel_khz, uint32_t target_khz)
{
  uint32_t div;

  if (kernel_khz == 0)
    {
      return SDMMC_CLKCR_CLKDIV_MASK;
    }

  div = (kernel_khz + 2u * target_khz - 1u) / (2u * target_khz);
  if (div < 1)
    {
      div = 1;
    }

  if (div > SDMMC_CLKCR_CLKDIV_MASK)
    {
      div = SDMMC_CLKCR_CLKDIV_MASK;
    }

  return div;
}

static void gpio_init(void)
{
  mmw(RCC_AHB3ENR, RCC_AHB3ENR_SDMMC1EN, 0);
  mmw(RCC_AHB4ENR, RCC_AHB4ENR_GPIOCEN | RCC_AHB4ENR_GPIODEN, 0);
  delay_cycles(1000);

  /* PC8 = SDMMC1_D0, AF12, pull-up. */
  mmw(GPIO_MODER(GPIOC_BASE), 0x00020000u, 0x00030000u);
  mmw(GPIO_OSPEEDR(GPIOC_BASE), 0x00030000u, 0);
  mmw(GPIO_PUPDR(GPIOC_BASE), 0x00010000u, 0x00030000u);
  mmw(GPIO_AFRH(GPIOC_BASE), 0x0000000cu, 0x0000000fu);

  /* PC12 = SDMMC1_CK, AF12, no pull. */
  mmw(GPIO_MODER(GPIOC_BASE), 0x02000000u, 0x03000000u);
  mmw(GPIO_OSPEEDR(GPIOC_BASE), 0x03000000u, 0);
  mmw(GPIO_PUPDR(GPIOC_BASE), 0x00000000u, 0x03000000u);
  mmw(GPIO_AFRH(GPIOC_BASE), 0x000c0000u, 0x000f0000u);

  /* PD2 = SDMMC1_CMD, AF12, pull-up. */
  mmw(GPIO_MODER(GPIOD_BASE), 0x00000020u, 0x00000030u);
  mmw(GPIO_OSPEEDR(GPIOD_BASE), 0x00000030u, 0);
  mmw(GPIO_PUPDR(GPIOD_BASE), 0x00000010u, 0x00000030u);
  mmw(GPIO_AFRL(GPIOD_BASE), 0x00000c00u, 0x00000f00u);
}

static int wait_status(uint32_t ok, uint32_t timeout)
{
  while (timeout-- > 0)
    {
      uint32_t sr = REG32(SDMMC_STA);

      if ((sr & SDMMC_ERR_MASK) != 0)
        {
          return -1;
        }

      if ((sr & ok) != 0)
        {
          return 0;
        }
    }

  return -2;
}

static void clear_flags(void)
{
  REG32(SDMMC_ICR) = SDMMC_ICR_CLEAR_ALL;
}

static int send_cmd(uint32_t index, uint32_t arg, uint32_t response)
{
  clear_flags();
  REG32(SDMMC_ARG) = arg;
  REG32(SDMMC_CMD) = (index & 0x3fu) | response | SDMMC_CMD_CPSMEN;

  if (response == SDMMC_CMD_WAITRESP_NONE)
    {
      return wait_status(SDMMC_STA_CMDSENT, 2000000u);
    }

  return wait_status(SDMMC_STA_CMDREND, 2000000u);
}

static int read_words(uint8_t *buf, uint32_t words)
{
  volatile uint32_t *fifo = (volatile uint32_t *)SDMMC_FIFO;
  uint32_t i;

  for (i = 0; i < words; i++)
    {
      uint32_t timeout = 2000000u;
      uint32_t word;

      while ((REG32(SDMMC_STA) & SDMMC_STA_RXFIFOE) != 0)
        {
          if ((REG32(SDMMC_STA) & SDMMC_ERR_MASK) != 0)
            {
              return -1;
            }

          if (timeout-- == 0)
            {
              return -2;
            }
        }

      word = fifo[0];
      buf[i * 4u + 0u] = (uint8_t)word;
      buf[i * 4u + 1u] = (uint8_t)(word >> 8);
      buf[i * 4u + 2u] = (uint8_t)(word >> 16);
      buf[i * 4u + 3u] = (uint8_t)(word >> 24);
    }

  return 0;
}

static void stop_transfer(void)
{
  clear_flags();
  REG32(SDMMC_ARG) = 0;
  REG32(SDMMC_CMD) = 12u | SDMMC_CMD_CMDSTOP |
                     SDMMC_CMD_WAITRESP_SHORT | SDMMC_CMD_CPSMEN;
  wait_status(SDMMC_STA_CMDREND, 2000000u);
  clear_flags();
}

static void abort_transfer(void)
{
  REG32(SDMMC_DCTRL) = 0;
  REG32(SDMMC_DLEN) = 0;
  stop_transfer();
}

static int read_data(uint32_t index, uint32_t arg, uint8_t *buf,
                     uint32_t len)
{
  uint32_t timeout;
  int ret;

  if (len == 0 || (len & 3u) != 0)
    {
      return -1;
    }

  g_data_stage = 0;

  clear_flags();
  REG32(SDMMC_DTIMER) = 0xffffffffu;
  REG32(SDMMC_DLEN) = len;

  /* DTDIR=1 selects card-to-controller.  The NuttX H7 driver does not use
   * DTEN; the data path is started by the command's CMDTRANS bit.
   */
  REG32(SDMMC_DCTRL) = SDMMC_DCTRL_DBLOCKSIZE_512 | SDMMC_DCTRL_DTDIR;
  REG32(SDMMC_ARG) = arg;
  REG32(SDMMC_CMD) = (index & 0x3fu) | SDMMC_CMD_WAITRESP_SHORT |
                     SDMMC_CMD_CMDTRANS | SDMMC_CMD_CPSMEN;

  if (wait_status(SDMMC_STA_CMDREND, 2000000u) != 0)
    {
      g_data_stage = 1;
      abort_transfer();
      return -1;
    }

  if (read_words(buf, len / 4u) != 0)
    {
      g_data_stage = 2;
      abort_transfer();
      return -1;
    }

  if (wait_status(SDMMC_STA_DATAEND, 20000000u) != 0)
    {
      g_data_stage = 3;
      abort_transfer();
      return -1;
    }

  timeout = 2000000u;
  while ((REG32(SDMMC_STA) &
          (SDMMC_STA_DPSMACT | SDMMC_STA_CPSMACT)) != 0)
    {
      if (timeout-- == 0)
        {
          g_data_stage = 4;
          abort_transfer();
          return -1;
        }
    }

  REG32(SDMMC_DCTRL) = 0;
  REG32(SDMMC_DLEN) = 0;
  clear_flags();

  /* CMD18 streams until CMD12.  Without the stop command the card stays in
   * data state and the next read fails even though this one completed.
   */
  if (index == 18u)
    {
      stop_transfer();
    }

  ret = 0;
  return ret;
}

int vg_emmc_init(void)
{
  uint8_t extcsd[512];
  uint32_t kernel_khz;
  uint32_t init_div;
  uint32_t data_div;
  uint32_t csd;
  uint32_t rca = 1u;
  uint32_t i;
  int ret;

  g_sectors = 0;
  g_error = 0;
  g_sector_mode = 1;

  gpio_init();

  REG32(SDMMC_POWER) = 0;
  REG32(SDMMC_CLKCR) = 0;
  REG32(SDMMC_MASK) = 0;
  REG32(SDMMC_DCTRL) = 0;
  delay_cycles(10000);

  kernel_khz = pll1q_khz();
  init_div = clock_divider(kernel_khz, SDMMC_INIT_KHZ);
  data_div = clock_divider(kernel_khz, SDMMC_DATA_KHZ);

  REG32(SDMMC_POWER) = SDMMC_POWER_ON;
  REG32(SDMMC_CLKCR) = init_div;
  delay_cycles(200000);

  ret = send_cmd(0, 0, SDMMC_CMD_WAITRESP_NONE);
  if (ret != 0)
    {
      g_error = 3;
      return ret;
    }

  /* CMD1 with sector support and the 3.3 V window. */
  for (i = 0; i < 2000u; i++)
    {
      ret = send_cmd(1, 0x40ff8080u, SDMMC_CMD_WAITRESP_SHORT_NOCRC);
      if (ret != 0)
        {
          g_error = 4;
          return ret;
        }

      if ((REG32(SDMMC_RESP1) & 0x80000000u) != 0)
        {
          break;
        }

      delay_cycles(100000);
    }

  if (i >= 2000u)
    {
      g_error = 5;
      return -1;
    }

  ret = send_cmd(2, 0, SDMMC_CMD_WAITRESP_LONG);
  if (ret != 0)
    {
      g_error = 6;
      return ret;
    }

  ret = send_cmd(3, rca << 16, SDMMC_CMD_WAITRESP_SHORT);
  if (ret != 0)
    {
      g_error = 7;
      return ret;
    }

  ret = send_cmd(9, rca << 16, SDMMC_CMD_WAITRESP_LONG);
  if (ret != 0)
    {
      g_error = 8;
      return ret;
    }

  csd = REG32(SDMMC_RESP1);
  g_sector_mode = ((csd >> 30) == 3u);

  ret = send_cmd(7, rca << 16, SDMMC_CMD_WAITRESP_SHORT);
  if (ret != 0)
    {
      g_error = 9;
      return ret;
    }

  ret = send_cmd(16, 512, SDMMC_CMD_WAITRESP_SHORT);
  if (ret != 0)
    {
      g_error = 10;
      return ret;
    }

  ret = read_data(8, 0, extcsd, sizeof(extcsd));
  if (ret != 0)
    {
      g_error = 10u + g_data_stage;
      g_data_stage = 0;
      return ret;
    }

  g_sectors = (uint32_t)extcsd[212] | ((uint32_t)extcsd[213] << 8) |
              ((uint32_t)extcsd[214] << 16) |
              ((uint32_t)extcsd[215] << 24);

  if (g_sectors < 2)
    {
      g_error = 12;
      return -1;
    }

  REG32(SDMMC_CLKCR) = data_div;
  delay_cycles(10000);
  return 0;
}

uint32_t vg_emmc_sector_count(void)
{
  return g_sectors;
}

uint32_t vg_emmc_error(void)
{
  return g_error;
}

int vg_emmc_read_blocks(void *ctx, uint32_t sector, uint8_t *buf,
                        uint32_t count)
{
  uint32_t arg;
  uint32_t len;

  (void)ctx;

  if (count == 0)
    {
      return 0;
    }

  if (buf == 0 || count > 0xffffu || sector >= g_sectors ||
      count > g_sectors - sector)
    {
      g_error = 30;
      return -1;
    }

  len = count * 512u;
  arg = g_sector_mode ? sector : sector * 512u;

  if (read_data(18, arg, buf, len) != 0)
    {
      g_error = 20u + g_data_stage;
      g_data_stage = 0;
      return -1;
    }

  return 0;
}
