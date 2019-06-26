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

#endif /* IMX_USB_REGS_H_ */
