#ifndef __USB_MSC_H
#define __USB_MSC_H

// usb-msc.c
struct disk_op_s;
int usb_cmd_data(struct disk_op_s *op, void *cdbcmd, u16 blocksize);
struct usbdevice_s;
int usb_msc_init(struct usbdevice_s *usbdev);


/****************************************************************
 * MSC flags
 ****************************************************************/

#define US_SC_ATAPI_8020   0x02
#define US_SC_ATAPI_8070   0x05
#define US_SC_SCSI         0x06

#define US_PR_BULK         0x50

#endif // ush-msc.h
