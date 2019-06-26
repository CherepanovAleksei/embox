/**
 * @file imx.c
 * @brief
 * @author Denis Deryugin <deryugin.denis@gmail.com>
 * @version
 * @date 26.06.2019
 */

#include <drivers/common/memory.h>
#include <embox/unit.h>
#include <hal/reg.h>
#include <util/log.h>

#include "imx_usb_regs.h"

EMBOX_UNIT_INIT(imx_usb_init);

static void imx_usb_regdump(void) {
	log_debug("USB_UOG_ID         %08x", REG32_LOAD(USB_UOG_ID));
	log_debug("USB_UOG_HWGENERAL  %08x", REG32_LOAD(USB_UOG_HWGENERAL));
	log_debug("USB_UOG_HWHOST     %08x", REG32_LOAD(USB_UOG_HWHOST));
	log_debug("USB_UOG_HWDEVICE   %08x", REG32_LOAD(USB_UOG_HWDEVICE));
	log_debug("USB_UOG_HWTXBUF    %08x", REG32_LOAD(USB_UOG_HWTXBUF));
	log_debug("USB_UOG_HWRXBUF    %08x", REG32_LOAD(USB_UOG_HWRXBUF));
}

static int imx_usb_init(void) {
	uint32_t uog_id = REG32_LOAD(USB_UOG_ID);

	log_boot_start();
	log_boot("USB 2.0 tigh-Speed code REV 0x%02x NID 0x%02x ID 0x%02x\n",
		(uog_id >> USB_UOG_ID_REV_OFFT) & USB_UOG_ID_REV_MASK,
		(uog_id >> USB_UOG_ID_NID_OFFT) & USB_UOG_ID_NID_MASK,
		(uog_id >> USB_UOG_ID_ID_OFFT) & USB_UOG_ID_ID_MASK);
	log_boot_stop();

	imx_usb_regdump();

	return 0;
}

static struct periph_memory_desc imx_usb_mem = {
	.start = IMX_USB_CORE_BASE,
	.len   = 0x7B0,
};

PERIPH_MEMORY_DEFINE(imx_usb_mem);
