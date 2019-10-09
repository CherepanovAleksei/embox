/**
 * @file
 *
 * @date Oct 9, 2019
 * @author Anton Bondarev
 */

#ifndef SRC_DRIVERS_MMC_CORE_MMC_CORE_H_
#define SRC_DRIVERS_MMC_CORE_MMC_CORE_H_

#include <stdint.h>

struct mmc_command {
	uint32_t opcode;
	uint32_t arg;
};

struct mmc_data {
	unsigned int flags;

#define MMC_DATA_WRITE (1 << 8)
#define MMC_DATA_READ  (1 << 9)

};

struct mmc_request {
	struct mmc_command *sbc; /* SET_BLOCK_COUNT for multiblock */
	struct mmc_command *cmd;
	struct mmc_data    *data;
};

#endif /* SRC_DRIVERS_MMC_CORE_MMC_CORE_H_ */
