/**
 * @file
 *
 * @date Oct 7, 2019
 * @author Anton Bondarev
 */

#include <util/log.h>

#include <util/math.h>

#include <stdint.h>
#include <errno.h>
#include <string.h>

#include <hal/clock.h>
#include <kernel/time/time.h>

#include "dw_mmc.h"

#include <embox/unit.h>

EMBOX_UNIT_INIT(dw_mmc_sockfpga_init);

#define BASE_ADDR OPTION_GET(NUMBER, base_addr)

static bool dw_mci_ctrl_reset(struct dw_mci *host, uint32_t reset) {
	unsigned long timeout = clock_sys_ticks() + ms2jiffies(1000);
	uint32_t ctrl;

	ctrl = mci_readl(host, CTRL);
	ctrl |= reset;
	mci_writel(host, CTRL, ctrl);

	/* wait till resets clear */
	do {
		ctrl = mci_readl(host, CTRL);
		if (!(ctrl & reset)) {
			return true;
		}
	} while (time_before(clock_sys_ticks(), timeout));

	log_error("Timeout resetting block (ctrl reset %#x)", ctrl & reset);

	return false;
}

/* push final bytes to part_buf, only use during push */
static inline void dw_mci_set_part_bytes(struct dw_mci *host, void *buf, int cnt) {
	memcpy((void *)&host->part_buf, buf, cnt);
	host->part_buf_count = cnt;
}

/* append bytes to part_buf, only use during push */
static inline int dw_mci_push_part_bytes(struct dw_mci *host, void *buf, int cnt) {
	cnt = min(cnt, (1 << host->data_shift) - host->part_buf_count);
	memcpy((void *)&host->part_buf + host->part_buf_count, buf, cnt);
	host->part_buf_count += cnt;
	return cnt;
}

/* pull first bytes from part_buf, only use during pull */
static int dw_mci_pull_part_bytes(struct dw_mci *host, void *buf, int cnt) {
	cnt = min((int)cnt, (int)host->part_buf_count);
	if (cnt) {
		memcpy(buf, (void *)&host->part_buf + host->part_buf_start, cnt);
		host->part_buf_count -= cnt;
		host->part_buf_start += cnt;
	}
	return cnt;
}

/* pull final bytes from the part_buf, assuming it's just been filled */
static inline void dw_mci_pull_final_bytes(struct dw_mci *host, void *buf, int cnt) {
	memcpy(buf, &host->part_buf, cnt);
	host->part_buf_start = cnt;
	host->part_buf_count = (1 << host->data_shift) - cnt;
}

static void dw_mci_push_data16(struct dw_mci *host, void *buf, int cnt) {
}

static void dw_mci_pull_data16(struct dw_mci *host, void *buf, int cnt) {
}

static void dw_mci_push_data32(struct dw_mci *host, void *buf, int cnt) {
}

static void dw_mci_pull_data32(struct dw_mci *host, void *buf, int cnt) {
}

static void dw_mci_push_data64(struct dw_mci *host, void *buf, int cnt) {
}

static void dw_mci_pull_data64(struct dw_mci *host, void *buf, int cnt) {
}

static inline void dw_mci_pull_data(struct dw_mci *host, void *buf, int cnt) {
	int len;

	/* get remaining partial bytes */
	len = dw_mci_pull_part_bytes(host, buf, cnt);
	if (unlikely(len == cnt))
		return;
	buf += len;
	cnt -= len;

	/* get the rest of the data */
	host->pull_data(host, buf, cnt);
}

static inline void dw_mci_read_data_pio(struct dw_mci *host, bool dto) {
}

static inline void dw_mci_write_data_pio(struct dw_mci *host) {
}

static void dw_mci_init_dma(struct dw_mci *host) {
	int addr_config;

	/*
	* Check tansfer mode from HCON[17:16]
	* Clear the ambiguous description of dw_mmc databook:
	* 2b'00: No DMA Interface -> Actually means using Internal DMA block
	* 2b'01: DesignWare DMA Interface -> Synopsys DW-DMA block
	* 2b'10: Generic DMA Interface -> non-Synopsys generic DMA block
	* 2b'11: Non DW DMA Interface -> pio only
	* Compared to DesignWare DMA Interface, Generic DMA Interface has a
	* simpler request/acknowledge handshake mechanism and both of them
	* are regarded as external dma master for dw_mmc.
	*/
	host->use_dma = SDMMC_GET_TRANS_MODE(mci_readl(host, HCON));
	if (host->use_dma == DMA_INTERFACE_IDMA) {
		host->use_dma = TRANS_MODE_IDMAC;
	} else if (host->use_dma == DMA_INTERFACE_DWDMA ||
		   host->use_dma == DMA_INTERFACE_GDMA) {
		host->use_dma = TRANS_MODE_EDMAC;
	} else {
		goto no_dma;
	}

	/* Determine which DMA interface to use */
	if (host->use_dma == TRANS_MODE_IDMAC) {
		/*
		* Check ADDR_CONFIG bit in HCON to find
		* IDMAC address bus width
		*/
		addr_config = SDMMC_GET_ADDR_CONFIG(mci_readl(host, HCON));

		if (addr_config == 1) {
			/* host supports IDMAC in 64-bit address mode */
			host->dma_64bit_address = 1;
			log_info("IDMAC supports 64-bit address mode.");
#if 0
			if (!dma_set_mask(host->dev, DMA_BIT_MASK(64))) {
				dma_set_coherent_mask(host->dev, DMA_BIT_MASK(64));
			}
#endif
		} else {
			/* host supports IDMAC in 32-bit address mode */
			host->dma_64bit_address = 0;
			log_info("IDMAC supports 32-bit address mode.");
		}
#if 0
		/* Alloc memory for sg translation */
		host->sg_cpu = dmam_alloc_coherent(host->dev,
						   DESC_RING_BUF_SZ,
						   &host->sg_dma, GFP_KERNEL);
		if (!host->sg_cpu) {
			dev_err(host->dev,
				"%s: could not alloc DMA memory\n",
				__func__);
			goto no_dma;
		}

		host->dma_ops = &dw_mci_idmac_ops;
#endif
		log_info("Using internal DMA controller.");
	} else {
#if 0
		/* TRANS_MODE_EDMAC: check dma bindings again */
		if ((device_property_read_string_array(dev, "dma-names",
						       NULL, 0) < 0) ||
		    !device_property_present(dev, "dmas")) {
			goto no_dma;
		}
		host->dma_ops = &dw_mci_edmac_ops;
#endif
		log_info("Using external DMA controller.");
	}
#if 0
	if (host->dma_ops->init && host->dma_ops->start &&
	    host->dma_ops->stop && host->dma_ops->cleanup) {
		if (host->dma_ops->init(host)) {
			dev_err(host->dev, "%s: Unable to initialize DMA Controller.\n",
				__func__);
			goto no_dma;
		}
	} else {
		dev_err(host->dev, "DMA initialization not found.\n");
		goto no_dma;
	}
#endif
	return;

no_dma:
	log_info("Using PIO mode.");
	host->use_dma = TRANS_MODE_PIO;
}

int dw_mci_probe(struct dw_mci *host) {
	int ret = 0;
	int i;
	int width;
	uint32_t fifo_size;

	/*
	 * Get the host data width - this assumes that HCON has been set with
	 * the correct values.
	 */
	i = SDMMC_GET_HDATA_WIDTH(mci_readl(host, HCON));
	if (!i) {
		host->push_data = dw_mci_push_data16;
		host->pull_data = dw_mci_pull_data16;
		width = 16;
		host->data_shift = 1;
	} else if (i == 2) {
		host->push_data = dw_mci_push_data64;
		host->pull_data = dw_mci_pull_data64;
		width = 64;
		host->data_shift = 3;
	} else {
		/* Check for a reserved value, and warn if it is */
		if (i != 1) {
			log_info("HCON reports a reserved host data width!");
			log_info("Defaulting to 32-bit access.");
		}

		host->push_data = dw_mci_push_data32;
		host->pull_data = dw_mci_pull_data32;
		width = 32;
		host->data_shift = 2;
	}

	/* Reset all blocks */
	if (!dw_mci_ctrl_reset(host, SDMMC_CTRL_ALL_RESET_FLAGS)) {
		ret = -ENODEV;
		return ret;
	}

	dw_mci_init_dma(host);

	/* Clear the interrupts for the host controller */
	mci_writel(host, RINTSTS, 0xFFFFFFFF);
	mci_writel(host, INTMASK, 0); /* disable all mmc interrupt first */

	/* Put in max timeout */
	mci_writel(host, TMOUT, 0xFFFFFFFF);

	/*
	 * FIFO threshold settings  RxMark  = fifo_size / 2 - 1,
	 *                          Tx Mark = fifo_size / 2 DMA Size = 8
	 */
//	if (!host->pdata->fifo_depth) {
		/*
		 * Power-on value of RX_WMark is FIFO_DEPTH-1, but this may
		 * have been overwritten by the bootloader, just like we're
		 * about to do, so if you know the value for your hardware, you
		 * should put it in the platform data.
		 */
		fifo_size = mci_readl(host, FIFOTH);
		fifo_size = 1 + ((fifo_size >> 16) & 0xfff);
//	} else {
//		fifo_size = host->pdata->fifo_depth;
//	}
	host->fifo_depth = fifo_size;
	host->fifoth_val = SDMMC_SET_FIFOTH(0x2, fifo_size / 2 - 1, fifo_size / 2);
	mci_writel(host, FIFOTH, host->fifoth_val);

	/* disable clock to CIU */
	mci_writel(host, CLKENA, 0);
	mci_writel(host, CLKSRC, 0);

	/*
	 * In 2.40a spec, Data offset is changed.
	 * Need to check the version-id and set data-offset for DATA register.
	 */
	host->verid = SDMMC_GET_VERID(mci_readl(host, VERID));
	log_info("Version ID is %04x", host->verid);

	if (host->verid < DW_MMC_240A) {
		host->fifo_reg = host->regs + DATA_OFFSET;
	} else{
		host->fifo_reg = host->regs + DATA_240A_OFFSET;
	}
	/*
	 * Enable interrupts for command done, data over, data empty,
	 * receive ready and error such as transmit, receive timeout, crc error
	 */
	mci_writel(host, INTMASK, SDMMC_INT_CMD_DONE | SDMMC_INT_DATA_OVER |
			SDMMC_INT_TXDR | SDMMC_INT_RXDR /*| DW_MCI_ERROR_FLAGS*/);
	/* Enable mci interrupt */
	mci_writel(host, CTRL, SDMMC_CTRL_INT_ENABLE);

	log_info("DW MMC controller at irq %d,%d bit host data width,%u deep fifo",
			host->irq, width, fifo_size);
	return 0;
}

static struct dw_mci dw_mci;
static int dw_mmc_sockfpga_init(void) {
	dw_mci.regs = (uintptr_t *)BASE_ADDR;
	dw_mci_probe(&dw_mci);
	return 0;
}
