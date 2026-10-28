#ifndef __STM32H750B_EMMC_H
#define __STM32H750B_EMMC_H

#include <stdint.h>

/* Minimal polling SDMMC1 driver for the soldered eMMC.  It always uses the
 * 1-bit bus at default speed and never writes to the card.
 */

int vg_emmc_init(void);
uint32_t vg_emmc_sector_count(void);
uint32_t vg_emmc_error(void);

/* Matches vg_ota_block_read_fn.  ctx is ignored. */
int vg_emmc_read_blocks(void *ctx, uint32_t sector, uint8_t *buf,
                        uint32_t count);

#endif
