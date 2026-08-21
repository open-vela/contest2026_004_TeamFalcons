/*
 * Minimal STM32H750B-DK internal-Flash boot stub for QSPI XIP.
 *
 * The register sequence intentionally mirrors xPack OpenOCD's
 * board/stm32h7x_dual_qspi.cfg, then jumps to the vector table of the main
 * image linked at 0x90000000.
 */

#include <stdint.h>

#define REG32(a) (*(volatile uint32_t *)(uintptr_t)(a))
#define REG16(a) (*(volatile uint16_t *)(uintptr_t)(a))

#define APP_BASE              0x90000000u
#define APP_LIMIT             0x98000000u

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
#define QSPI_DLR              0x52005010u
#define QSPI_CCR              0x52005014u
#define QSPI_DR               0x52005020u
#define QSPI_LPTR             0x52005030u

#define QSPI_CR_EN            (1u << 0)
#define QSPI_CR_ABORT         (1u << 1)
#define QSPI_SR_BUSY          (1u << 5)

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

static int valid_sram(uint32_t value)
{
  /* Cortex-M loads the initial MSP from the vector table and requires a
   * word-aligned value.  NuttX may place its early stack on a 4-byte boundary
   * before establishing the ABI-aligned task stack.
   */

  return value >= SRAM_BASE && value < SRAM_LIMIT && (value & 3u) == 0;
}

static int valid_entry(uint32_t value)
{
  uint32_t addr = value & ~1u;

  return addr >= APP_BASE && addr < APP_LIMIT && (value & 1u) != 0;
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

void Reset_Handler(void)
{
  uint32_t app_stack;
  uint32_t app_entry;

  __asm__ volatile ("cpsid i");

  clock_init();
  qspi_enter_memory_mapped();

  app_stack = REG32(APP_BASE);
  app_entry = REG32(APP_BASE + 4u);

  if (valid_sram(app_stack) && valid_entry(app_entry))
    {
      REG32(SCB_VTOR) = APP_BASE;
      __asm__ volatile ("dsb 0xf" ::: "memory");
      __asm__ volatile ("isb 0xf" ::: "memory");
      jump_to_image(app_stack, app_entry);
    }

  Default_Handler();
}

void Default_Handler(void)
{
  for (; ; )
    {
      __asm__ volatile ("wfi");
    }
}
