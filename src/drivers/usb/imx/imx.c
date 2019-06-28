/**
 * @file imx.c
 * @brief
 * @author Denis Deryugin <deryugin.denis@gmail.com>
 * @version
 * @date 26.06.2019
 */

#include <errno.h>

#include <drivers/clk/ccm_imx6.h>
#include <drivers/common/memory.h>
#include <drivers/usb/usb.h>
#include <embox/unit.h>
#include <hal/reg.h>
#include <util/log.h>

#include "imx_usb_regs.h"

EMBOX_UNIT_INIT(imx_usb_init);

#define USB_PORT 0

#if 0
static struct ehci_state {
	struct queue_head qh_list[128];
} echi_state;
#endif

/* Isochronous Transaction Descriptor */
struct itd {
	uint32_t trn[8];
	uint32_t buf[7];
};

int itd_endpt_get(struct itd *i) {
	return (i->buf[0] >> 8) & 0xF;
}

void itd_endpt_set(struct itd *i, int val) {
	i->buf[0] = i->buf[0] & ~(0xF << 8);
	i->buf[0] |= val << 8;
}

int itd_devaddr_get(struct itd *i) {
	return (i->buf[0] >> 0) & 0x3F;
}

void itd_devaddr_set(struct itd *i, int val) {
	i->buf[0] = i->buf[0] & ~(0x3F << 0);
	i->buf[0] |= val;
}

int itd_dir_get(struct itd *i) {
	return (i->buf[1] >> 11) & 0x1;
}

void itd_dir_set(struct itd *i, int val) {
	i->buf[1] = i->buf[1] & ~(1 << 11);
	i->buf[1] |= val << 11;
}

int itd_maxpacketsz_get(struct itd *i) {
	return (i->buf[1] >> 0) & 0x7FF;
}

void itd_maxpacketsz_set(struct itd *i, int val) {
	i->buf[1] = i->buf[1] & ~(0x7FF);
	i->buf[1] |= val;
}

static void imx_usb_powerup(int port) {
	REG32_STORE(USBPHY_CTRL_SET(port), USBPHY_CTRL_CLKGATE);

	REG32_STORE(USB_ANALOG_CHRG_DETECT(port),
			USB_ANALOG_EN_B | USB_ANALOG_CHK_CHRG_B);

	ccm_analog_usb_init(port);
}

static void ehci_reset(void) {
	uint32_t cmd = REG32_LOAD(USB_UOG_USBCMD);

	log_debug("Start reset");

	cmd = (cmd & ~USB_USBCMD_RS) | USB_USBCMD_RST;
	REG32_STORE(USB_UOG_USBCMD, cmd);

	while (REG32_LOAD(USB_UOG_USBCMD) & USB_USBCMD_RST);

	log_debug("Done");
}

static void imx_usb_regdump(void) {
	log_debug("USB_UOG_ID         %08x", REG32_LOAD(USB_UOG_ID));
	log_debug("USB_UOG_HWGENERAL  %08x", REG32_LOAD(USB_UOG_HWGENERAL));
	log_debug("USB_UOG_HWHOST     %08x", REG32_LOAD(USB_UOG_HWHOST));
	log_debug("USB_UOG_HWDEVICE   %08x", REG32_LOAD(USB_UOG_HWDEVICE));
	log_debug("USB_UOG_HWTXBUF    %08x", REG32_LOAD(USB_UOG_HWTXBUF));
	log_debug("USB_UOG_HWRXBUF    %08x", REG32_LOAD(USB_UOG_HWRXBUF));
	log_debug("USB_UOG_USBCMD     %08x", REG32_LOAD(USB_UOG_USBCMD));
	log_debug("USB_UOG_USBSTS     %08x", REG32_LOAD(USB_UOG_USBSTS));
	log_debug("USB_UOG_USBINTR    %08x", REG32_LOAD(USB_UOG_USBINTR));
	log_debug("========= PHY REGISTERS =========");
	for (int i = 0; i < 2; i++) {
		log_debug("USBPHY%d_PWD     %08x", i + 1, REG32_LOAD(USBPHY_PWD(i)));
		log_debug("USBPHY%d_TX      %08x", i + 1, REG32_LOAD(USBPHY_TX(i)));
		log_debug("USBPHY%d_RX      %08x", i + 1, REG32_LOAD(USBPHY_RX(i)));
		log_debug("USBPHY%d_CTRL    %08x", i + 1, REG32_LOAD(USBPHY_CTRL(i)));
		log_debug("USBPHY%d_STATUS  %08x", i + 1, REG32_LOAD(USBPHY_STATUS(i)));
	}
}

static int ehci_start(struct usb_hcd *hcd) {
	uint32_t cmd = REG32_LOAD(USB_UOG_USBCMD);

	log_debug("Put USB controller in running mode");

	REG32_STORE(USB_UOG_USBCMD, cmd);

	while (REG32_LOAD(USB_UOG_USBCMD) & USB_USBCMD_RST);

	return 0;
}

static int ehci_stop(struct usb_hcd *hcd) {
	return 0;
}
static void *ehci_hcd_alloc(struct usb_hcd *hcd, void *arg) {
	return NULL;
}

static void ehci_hcd_free(struct usb_hcd *hcd, void *hci) {
}

static int ehci_rh_ctrl(struct usb_hub_port *port, enum usb_hub_request req,
			unsigned short value) {
	return 0;
}

static int ehci_request(struct usb_request *req) {
	return 0;
}

static struct usb_hcd_ops ehci_hcd_ops = {
	.hcd_start     = ehci_start,
	.hcd_stop      = ehci_stop,
	.hcd_hci_alloc = ehci_hcd_alloc,
	.hcd_hci_free  = ehci_hcd_free,
	.rhub_ctrl     = ehci_rh_ctrl,
	.request       = ehci_request,
};

static int imx_usb_init(void) {
	struct usb_hcd *hcd;
	uint32_t uog_id = REG32_LOAD(USB_UOG_ID);
	uint32_t phy_version = REG32_LOAD(USBPHY_VERSION(0));
	uint32_t digprog = REG32_LOAD(USB_ANALOG_DIGPROG);

	log_boot_start();
	log_boot("USB 2.0 high-Speed code rev 0x%02x NID 0x%02x ID 0x%02x\n",
		(uog_id >> USB_UOG_ID_REV_OFFT) & USB_UOG_ID_REV_MASK,
		(uog_id >> USB_UOG_ID_NID_OFFT) & USB_UOG_ID_NID_MASK,
		(uog_id >> USB_UOG_ID_ID_OFFT) & USB_UOG_ID_ID_MASK);
	log_boot("USB PHY %d.%d\n",
			phy_version >> 24,
			((phy_version) >> 16) & 0xFF);
	log_boot("USB analog rev x.%d\n", digprog & 0xFF);

	log_boot_stop();

	imx_usb_regdump();

	clk_enable("usboh3");

	/* Setup IOMUX */

	imx_usb_powerup(USB_PORT);

	/* USB phy config as host */

	/* Configure GPIO pins */

	ehci_reset();

	/* Init qh list */

	/* Init periodic list */

	imx_usb_regdump();
	/* Write '11' to USBMODE */

	/* Make sure all structures don't cross 4kb-border! */

	hcd = usb_hcd_alloc(&ehci_hcd_ops, (void *) 0);
	if (!hcd) {
		return -ENOMEM;
	}

	return usb_hcd_register(hcd);
}

static struct periph_memory_desc imx_usb_mem = {
	.start = IMX_USB_CORE_BASE,
	.len   = 0x7B0,
};

PERIPH_MEMORY_DEFINE(imx_usb_mem);

static struct periph_memory_desc imx_usb_phy_mem = {
	.start = USBPHY_BASE(0),
	.len   = 0x2000,
};

PERIPH_MEMORY_DEFINE(imx_usb_phy_mem);
