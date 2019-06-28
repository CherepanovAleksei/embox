/**
 * @file imx_usb_regs.h
 * @brief Registers definition
 * @author Denis Deryugin <deryugin.denis@gmail.com>
 * @version
 * @date 26.06.2019
 */

#ifndef IMX_USB_REGS_H_
#define IMX_USB_REGS_H_

#define IMX_USB_CORE_BASE 0x02184000

#define USB_UOG_ID           (IMX_USB_CORE_BASE + 0x000)
# define USB_UOG_ID_REV_OFFT 16
# define USB_UOG_ID_REV_MASK 0xff
# define USB_UOG_ID_NID_OFFT 8
# define USB_UOG_ID_NID_MASK 0x3f
# define USB_UOG_ID_ID_OFFT  0
# define USB_UOG_ID_ID_MASK  0x3f

#define USB_UOG_HWGENERAL    (IMX_USB_CORE_BASE + 0x004)
#define USB_UOG_HWHOST       (IMX_USB_CORE_BASE + 0x008)
#define USB_UOG_HWDEVICE     (IMX_USB_CORE_BASE + 0x00C)
#define USB_UOG_HWTXBUF      (IMX_USB_CORE_BASE + 0x010)
#define USB_UOG_HWRXBUF      (IMX_USB_CORE_BASE + 0x014)

#define USB_UOG_USBCMD       (IMX_USB_CORE_BASE + 0x140)
# define USB_USBCMD_RS       (1 << 0)
# define USB_USBCMD_RST      (1 << 1)
#define USB_UOG_USBSTS       (IMX_USB_CORE_BASE + 0x144)
#define USB_UOG_USBINTR      (IMX_USB_CORE_BASE + 0x148)

#define QTD_TERMINATE     0x1
#define QTD_POINTER_MASK ~0x1F
#define QTD_TOKEN_TOOGLE (1 << 31)
#define QTD_TOKEN_BYTES_OFFT   16
#define QTD_TOKEN_BYTES_MASK   0x7FFF
#define QTD_TOKEN_IOC          (1 << 15)
#define QTD_TOKEN_CPAGE_OFFT   12
#define QTD_TOKEN_CPAGE_MASK   0x1F

#define QTD_TOKEN_PID_OFFT   8
#define QTD_TOKEN_PID_MASK   0x3
#define QTD_TOKEN_STATUS_MASK   0xFF

#define QTD_BUFFER_CURRENT_OFFT(x) (x &  0xFFF)
#define QTD_BUFFER_POINTER(x)      (x & ~0xFFF)
/* Data structures as decribed in iMX6DQPRM.pdf */
struct qtd {
	uint32_t next;
	uint32_t altnext;
	uint32_t token;
	uint32_t buffer[5];
};

struct queue_head {
	uint32_t link;
	uint32_t endpt1;
	uint32_t endpt2;
	uint32_t curtd;
	struct qtd qtd;
	uint32_t buffer;
};

#endif /* IMX_USB_REGS_H_ */
