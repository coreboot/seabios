// USB functions and data.
#ifndef __USB_H
#define __USB_H

// Local information for a usb controller.
struct usb_s {
    u8 type;
    u8 maxaddr;
    u16 bdf;

    union {
        struct {
            u16 iobase;
            void *qh;
        } uhci;
        struct {
            struct ohci_regs *regs;
            void *control_ed;
        } ohci;
    };
};

#define USB_TYPE_UHCI 1
#define USB_TYPE_OHCI 2

extern struct usb_s USBControllers[];

struct usb_pipe {
    u32 endp;
};

#define USB_MAXADDR 127

// usb.c
void usb_setup(void);
int configure_usb_device(struct usb_s *cntl, int lowspeed);
struct usb_ctrlrequest;
int send_default_control(u32 endp, const struct usb_ctrlrequest *req
                         , void *data);
struct usb_pipe *alloc_intr_pipe(u32 endp, int period);
int usb_poll_intr(struct usb_pipe *pipe, void *data);


/****************************************************************
 * endpoint definition
 ****************************************************************/

static inline u32
mkendp(struct usb_s *cntl, u8 devaddr, u8 ep, u8 lowspeed, u8 maxsize)
{
    u8 bus = cntl-USBControllers;
    u8 size = __ffs(maxsize);
    return (size<<25) | (lowspeed<<24) | (bus<<16) | (devaddr<<8) | ep;
}

static inline u8 endp2ep(u32 endp) {
    return endp;
}
static inline u8 endp2devaddr(u32 endp) {
    return endp>>8;
}
static inline struct usb_s *endp2cntl(u32 endp) {
    u8 bus = endp>>16;
    return &USBControllers[bus];
}
static inline u8 endp2speed(u32 endp) {
    return (endp>>24) & 1;
}
static inline u8 endp2maxsize(u32 endp) {
    return 1 << (endp>>25);
}


/****************************************************************
 * usb structs and flags
 ****************************************************************/

#define USB_PID_OUT                     0xe1
#define USB_PID_IN                      0x69
#define USB_PID_SETUP                   0x2d

#define USB_DIR_OUT                     0               /* to device */
#define USB_DIR_IN                      0x80            /* to host */

#define USB_TYPE_MASK                   (0x03 << 5)
#define USB_TYPE_STANDARD               (0x00 << 5)
#define USB_TYPE_CLASS                  (0x01 << 5)
#define USB_TYPE_VENDOR                 (0x02 << 5)
#define USB_TYPE_RESERVED               (0x03 << 5)

#define USB_RECIP_MASK                  0x1f
#define USB_RECIP_DEVICE                0x00
#define USB_RECIP_INTERFACE             0x01
#define USB_RECIP_ENDPOINT              0x02
#define USB_RECIP_OTHER                 0x03

#define USB_REQ_GET_STATUS              0x00
#define USB_REQ_CLEAR_FEATURE           0x01
#define USB_REQ_SET_FEATURE             0x03
#define USB_REQ_SET_ADDRESS             0x05
#define USB_REQ_GET_DESCRIPTOR          0x06
#define USB_REQ_SET_DESCRIPTOR          0x07
#define USB_REQ_GET_CONFIGURATION       0x08
#define USB_REQ_SET_CONFIGURATION       0x09
#define USB_REQ_GET_INTERFACE           0x0A
#define USB_REQ_SET_INTERFACE           0x0B
#define USB_REQ_SYNCH_FRAME             0x0C

struct usb_ctrlrequest {
    u8 bRequestType;
    u8 bRequest;
    u16 wValue;
    u16 wIndex;
    u16 wLength;
} PACKED;

#define USB_DT_DEVICE                   0x01
#define USB_DT_CONFIG                   0x02
#define USB_DT_STRING                   0x03
#define USB_DT_INTERFACE                0x04
#define USB_DT_ENDPOINT                 0x05
#define USB_DT_DEVICE_QUALIFIER         0x06
#define USB_DT_OTHER_SPEED_CONFIG       0x07

struct usb_device_descriptor {
    u8  bLength;
    u8  bDescriptorType;

    u16 bcdUSB;
    u8  bDeviceClass;
    u8  bDeviceSubClass;
    u8  bDeviceProtocol;
    u8  bMaxPacketSize0;
    u16 idVendor;
    u16 idProduct;
    u16 bcdDevice;
    u8  iManufacturer;
    u8  iProduct;
    u8  iSerialNumber;
    u8  bNumConfigurations;
} PACKED;

#define USB_CLASS_PER_INTERFACE         0       /* for DeviceClass */
#define USB_CLASS_AUDIO                 1
#define USB_CLASS_COMM                  2
#define USB_CLASS_HID                   3
#define USB_CLASS_PHYSICAL              5
#define USB_CLASS_STILL_IMAGE           6
#define USB_CLASS_PRINTER               7
#define USB_CLASS_MASS_STORAGE          8
#define USB_CLASS_HUB                   9

struct usb_config_descriptor {
    u8  bLength;
    u8  bDescriptorType;

    u16 wTotalLength;
    u8  bNumInterfaces;
    u8  bConfigurationValue;
    u8  iConfiguration;
    u8  bmAttributes;
    u8  bMaxPower;
} PACKED;

struct usb_interface_descriptor {
    u8  bLength;
    u8  bDescriptorType;

    u8  bInterfaceNumber;
    u8  bAlternateSetting;
    u8  bNumEndpoints;
    u8  bInterfaceClass;
    u8  bInterfaceSubClass;
    u8  bInterfaceProtocol;
    u8  iInterface;
} PACKED;

struct usb_endpoint_descriptor {
    u8  bLength;
    u8  bDescriptorType;

    u8  bEndpointAddress;
    u8  bmAttributes;
    u16 wMaxPacketSize;
    u8  bInterval;
} PACKED;

#define USB_ENDPOINT_NUMBER_MASK        0x0f    /* in bEndpointAddress */
#define USB_ENDPOINT_DIR_MASK           0x80

#define USB_ENDPOINT_XFERTYPE_MASK      0x03    /* in bmAttributes */
#define USB_ENDPOINT_XFER_CONTROL       0
#define USB_ENDPOINT_XFER_ISOC          1
#define USB_ENDPOINT_XFER_BULK          2
#define USB_ENDPOINT_XFER_INT           3
#define USB_ENDPOINT_MAX_ADJUSTABLE     0x80

#endif // usb.h
