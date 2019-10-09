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
#include <drivers/common/memory.h>
#include <asm-generic/dma-mapping.h>

#include <linux/byteorder.h>

#include <drivers/mmc/mmc_core.h>

#include "dw_mmc.h"

#include <embox/unit.h>

EMBOX_UNIT_INIT(dw_mmc_sockfpga_init);

#define BASE_ADDR OPTION_GET(NUMBER, base_addr)

/* Common flag combinations */
#define DW_MCI_DATA_ERROR_FLAGS (SDMMC_INT_DRTO | SDMMC_INT_DCRC | \
				SDMMC_INT_HTO | SDMMC_INT_SBE  | \
				SDMMC_INT_EBE | SDMMC_INT_HLE)

#define DW_MCI_CMD_ERROR_FLAGS (SDMMC_INT_RTO | SDMMC_INT_RCRC | \
				SDMMC_INT_RESP_ERR | SDMMC_INT_HLE)

#define DW_MCI_ERROR_FLAGS (DW_MCI_DATA_ERROR_FLAGS | DW_MCI_CMD_ERROR_FLAGS)

#define IDMAC_INT_CLR (SDMMC_IDMAC_INT_AI | SDMMC_IDMAC_INT_NI | \
				SDMMC_IDMAC_INT_CES | SDMMC_IDMAC_INT_DU | \
				SDMMC_IDMAC_INT_FBE | SDMMC_IDMAC_INT_RI | \
				SDMMC_IDMAC_INT_TI)

#define DESC_RING_BUF_SZ PAGE_SIZE()

struct idmac_desc_64addr {
	uint32_t des0; /* Control Descriptor */

	uint32_t des1;	/* Reserved */

	uint32_t des2;	/*Buffer sizes */
#define IDMAC_64ADDR_SET_BUFFER1_SIZE(d, s) \
	((d)->des2 = ((d)->des2 & cpu_to_le32(0x03ffe000)) | \
	 ((cpu_to_le32(s)) & cpu_to_le32(0x1fff)))

	uint32_t des3;	/* Reserved */

	uint32_t des4;	/* Lower 32-bits of Buffer Address Pointer 1*/
	uint32_t des5;	/* Upper 32-bits of Buffer Address Pointer 1*/

	uint32_t des6;	/* Lower 32-bits of Next Descriptor Address */
	uint32_t des7;	/* Upper 32-bits of Next Descriptor Address */
};

struct idmac_desc {
	uint32_t des0;	/* Control Descriptor */
#define IDMAC_DES0_DIC	BIT(1)
#define IDMAC_DES0_LD	BIT(2)
#define IDMAC_DES0_FD	BIT(3)
#define IDMAC_DES0_CH	BIT(4)
#define IDMAC_DES0_ER	BIT(5)
#define IDMAC_DES0_CES	BIT(30)
#define IDMAC_DES0_OWN	BIT(31)

	uint32_t des1;	/* Buffer sizes */
#define IDMAC_SET_BUFFER1_SIZE(d, s) \
	((d)->des1 = ((d)->des1 & cpu_to_le32(0x03ffe000)) | (cpu_to_le32((s) & 0x1fff)))

	uint32_t des2;	/* buffer 1 physical address */

	uint32_t des3;	/* buffer 2 physical address */
};


static bool dw_mci_ctrl_reset(struct dw_mci *host, uint32_t reset) {
	uint32_t ctrl;
	unsigned long timeout;

	timeout = clock_sys_ticks() + ms2jiffies(1000);

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


/* DMA interface functions */
static inline void dw_mci_stop_dma(struct dw_mci *host) {
	if (host->using_dma) {
		host->dma_ops->stop(host);
		host->dma_ops->cleanup(host);
	}

	/* Data transfer was stopped by the interrupt handler */
//	set_bit(EVENT_XFER_COMPLETE, &host->pending_events);
}

static inline int dw_mci_get_dma_dir(struct mmc_data *data) {
	if (data->flags & MMC_DATA_WRITE) {
		return DMA_TO_DEVICE;
	} else {
		return DMA_FROM_DEVICE;
	}
}

static void dw_mci_dma_cleanup(struct dw_mci *host) {
	struct mmc_data *data = host->data;

	if (data) {
#if 0
		if (!data->host_cookie) {
			dma_unmap_sg(host->dev, data->sg, data->sg_len,
					dw_mci_get_dma_dir(data));
		}
#endif
	}
}

static void dw_mci_idmac_reset(struct dw_mci *host) {
	uint32_t bmod = mci_readl(host, BMOD);
	/* Software reset of DMA */
	bmod |= SDMMC_IDMAC_SWRESET;
	mci_writel(host, BMOD, bmod);
}

static void dw_mci_idmac_stop_dma(struct dw_mci *host) {
	uint32_t temp;

	/* Disable and reset the IDMAC interface */
	temp = mci_readl(host, CTRL);
	temp &= ~SDMMC_CTRL_USE_IDMAC;
	temp |= SDMMC_CTRL_DMA_RESET;
	mci_writel(host, CTRL, temp);

	/* Stop the IDMAC running */
	temp = mci_readl(host, BMOD);
	temp &= ~(SDMMC_IDMAC_ENABLE | SDMMC_IDMAC_FB);
	temp |= SDMMC_IDMAC_SWRESET;
	mci_writel(host, BMOD, temp);
}

static void dw_mci_dmac_complete_dma(void *arg) {
	struct dw_mci *host = arg;
	struct mmc_data *data = host->data;

	log_debug("DMA complete");

	if ((host->use_dma == TRANS_MODE_EDMAC) &&
	    data && (data->flags & MMC_DATA_READ)) {
		log_error("TRANS_MODE_EDMAC not support");
#if 0
		/* Invalidate cache after read */
		dma_sync_sg_for_cpu(mmc_dev(host->cur_slot->mmc),
				    data->sg,
				    data->sg_len,
				    DMA_FROM_DEVICE);
#endif
	}

	host->dma_ops->cleanup(host);

	/*
	 * If the card was removed, data will be NULL. No point in trying to
	 * send the stop command or waiting for NBUSY in this case.
	 */
	if (data) {
#if 0
		set_bit(EVENT_XFER_COMPLETE, &host->pending_events);
		tasklet_schedule(&host->tasklet);
#endif
	}
}


static int dw_mci_idmac_init(struct dw_mci *host) {
	int i;

	if (host->dma_64bit_address == 1) {
		struct idmac_desc_64addr *p;
		/* Number of descriptors in the ring buffer */
		host->ring_size =
			DESC_RING_BUF_SZ / sizeof(struct idmac_desc_64addr);

		/* Forward link the descriptor list */
		for (i = 0, p = host->sg_cpu; i < host->ring_size - 1; i++, p++) {
			p->des6 = (host->sg_dma +
					(sizeof(struct idmac_desc_64addr) * (i + 1))) & 0xffffffff;

			p->des7 = (uint64_t)(host->sg_dma +
					(sizeof(struct idmac_desc_64addr) * (i + 1))) >> 32;
			/* Initialize reserved and buffer size fields to "0" */
			p->des1 = 0;
			p->des2 = 0;
			p->des3 = 0;
		}

		/* Set the last descriptor as the end-of-ring descriptor */
		p->des6 = host->sg_dma & 0xffffffff;
		p->des7 = (uint64_t)host->sg_dma >> 32;
		p->des0 = IDMAC_DES0_ER;

	} else {
		struct idmac_desc *p;
		/* Number of descriptors in the ring buffer */
		host->ring_size =
			DESC_RING_BUF_SZ / sizeof(struct idmac_desc);

		/* Forward link the descriptor list */
		for (i = 0, p = host->sg_cpu; i < host->ring_size - 1; i++, p++) {
			p->des3 = cpu_to_le32(host->sg_dma +
					(sizeof(struct idmac_desc) * (i + 1)));
			p->des1 = 0;
		}

		/* Set the last descriptor as the end-of-ring descriptor */
		p->des3 = cpu_to_le32(host->sg_dma);
		p->des0 = cpu_to_le32(IDMAC_DES0_ER);
	}

	dw_mci_idmac_reset(host);

	if (host->dma_64bit_address == 1) {
		/* Mask out interrupts - get Tx & Rx complete only */
		mci_writel(host, IDSTS64, IDMAC_INT_CLR);
		mci_writel(host, IDINTEN64, SDMMC_IDMAC_INT_NI |
				SDMMC_IDMAC_INT_RI | SDMMC_IDMAC_INT_TI);

		/* Set the descriptor base address */
		mci_writel(host, DBADDRL, host->sg_dma & 0xffffffff);
		mci_writel(host, DBADDRU, (uint64_t)host->sg_dma >> 32);

	} else {
		/* Mask out interrupts - get Tx & Rx complete only */
		mci_writel(host, IDSTS, IDMAC_INT_CLR);
		mci_writel(host, IDINTEN, SDMMC_IDMAC_INT_NI |
				SDMMC_IDMAC_INT_RI | SDMMC_IDMAC_INT_TI);

		/* Set the descriptor base address */
		mci_writel(host, DBADDR, host->sg_dma);
	}

	return 0;
}

static int dw_mci_idmac_start_dma(struct dw_mci *host, unsigned int sg_len) {
	uint32_t temp;
	int ret = 0;

#if 0
	if (host->dma_64bit_address == 1)
		ret = dw_mci_prepare_desc64(host, host->data, sg_len);
	else
		ret = dw_mci_prepare_desc32(host, host->data, sg_len);

	if (ret)
		goto out;
#endif
	/* drain writebuffer */
	//wmb();

	/* Make sure to reset DMA in case we did PIO before this */
	dw_mci_ctrl_reset(host, SDMMC_CTRL_DMA_RESET);
	dw_mci_idmac_reset(host);

	/* Select IDMAC interface */
	temp = mci_readl(host, CTRL);
	temp |= SDMMC_CTRL_USE_IDMAC;
	mci_writel(host, CTRL, temp);

	/* drain writebuffer */
	//wmb();

	/* Enable the IDMAC */
	temp = mci_readl(host, BMOD);
	temp |= SDMMC_IDMAC_ENABLE | SDMMC_IDMAC_FB;
	mci_writel(host, BMOD, temp);

	/* Start it running */
	mci_writel(host, PLDMND, 1);

//out:
	return ret;
}

static const struct dw_mci_dma_ops dw_mci_idmac_ops = {
	.init = dw_mci_idmac_init,
	.start = dw_mci_idmac_start_dma,
	.stop = dw_mci_idmac_stop_dma,
	.complete = dw_mci_dmac_complete_dma,
	.cleanup = dw_mci_dma_cleanup,
};

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

		/* Alloc memory for sg translation */
		host->sg_cpu = dma_alloc_coherent(NULL,
						   DESC_RING_BUF_SZ,
						   &host->sg_dma, 0);
		if (!host->sg_cpu) {
			log_error("could not alloc DMA memory");
			goto no_dma;
		}

		host->dma_ops = &dw_mci_idmac_ops;

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

	if (host->dma_ops->init && host->dma_ops->start &&
	    host->dma_ops->stop && host->dma_ops->cleanup) {
		if (host->dma_ops->init(host)) {
			log_error("Unable to initialize DMA Controller.");
			goto no_dma;
		}
	} else {
		log_error("DMA initialization not found.");
		goto no_dma;
	}

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
	/*
	 * Power-on value of RX_WMark is FIFO_DEPTH-1, but this may
	 * have been overwritten by the bootloader, just like we're
	 * about to do, so if you know the value for your hardware, you
	 * should put it in the platform data.
	 */
	fifo_size = mci_readl(host, FIFOTH);
	fifo_size = 1 + ((fifo_size >> 16) & 0xfff);

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
			SDMMC_INT_TXDR | SDMMC_INT_RXDR | DW_MCI_ERROR_FLAGS);
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

PERIPH_MEMORY_DEFINE(dw_mmc, BASE_ADDR, 0x4000);
