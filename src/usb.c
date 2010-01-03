// Main code for handling USB controllers and devices.
//
// Copyright (C) 2009  Kevin O'Connor <kevin@koconnor.net>
//
// This file may be distributed under the terms of the GNU LGPLv3 license.

#include "util.h" // dprintf
#include "pci.h" // foreachpci
#include "config.h" // CONFIG_*
#include "pci_regs.h" // PCI_CLASS_REVISION
#include "pci_ids.h" // PCI_CLASS_SERIAL_USB_UHCI
#include "usb-uhci.h" // uhci_init
#include "usb-ohci.h" // ohci_init
#include "usb-hid.h" // usb_keyboard_setup
#include "usb.h" // struct usb_s
#include "biosvar.h" // GET_GLOBAL

struct usb_s USBControllers[16] VAR16VISIBLE;

static int
send_control(u32 endp, int dir, const void *cmd, int cmdsize
             , void *data, int datasize)
{
    struct usb_s *cntl = endp2cntl(endp);
    switch (cntl->type) {
    default:
    case USB_TYPE_UHCI:
        return uhci_control(endp, dir, cmd, cmdsize, data, datasize);
    case USB_TYPE_OHCI:
        return ohci_control(endp, dir, cmd, cmdsize, data, datasize);
    }
}

struct usb_pipe *
alloc_intr_pipe(u32 endp, int period)
{
    struct usb_s *cntl = endp2cntl(endp);
    switch (cntl->type) {
    default:
    case USB_TYPE_UHCI:
        return uhci_alloc_intr_pipe(endp, period);
    case USB_TYPE_OHCI:
        return ohci_alloc_intr_pipe(endp, period);
    }
}

int
usb_poll_intr(struct usb_pipe *pipe, void *data)
{
    u32 endp = GET_FLATPTR(pipe->endp);
    struct usb_s *cntl = endp2cntl(endp);
    switch (GET_GLOBAL(cntl->type)) {
    default:
    case USB_TYPE_UHCI:
        return uhci_poll_intr(pipe, data);
    case USB_TYPE_OHCI:
        return ohci_poll_intr(pipe, data);
    }
}

int
send_default_control(u32 endp, const struct usb_ctrlrequest *req, void *data)
{
    return send_control(endp, req->bRequestType & USB_DIR_IN
                        , req, sizeof(*req), data, req->wLength);
}

// Get the first 8 bytes of the device descriptor.
static int
get_device_info8(struct usb_device_descriptor *dinfo, u32 endp)
{
    struct usb_ctrlrequest req;
    req.bRequestType = USB_DIR_IN | USB_TYPE_STANDARD | USB_RECIP_DEVICE;
    req.bRequest = USB_REQ_GET_DESCRIPTOR;
    req.wValue = USB_DT_DEVICE<<8;
    req.wIndex = 0;
    req.wLength = 8;
    return send_default_control(endp, &req, dinfo);
}

static struct usb_config_descriptor *
get_device_config(u32 endp)
{
    struct usb_config_descriptor cfg;

    struct usb_ctrlrequest req;
    req.bRequestType = USB_DIR_IN | USB_TYPE_STANDARD | USB_RECIP_DEVICE;
    req.bRequest = USB_REQ_GET_DESCRIPTOR;
    req.wValue = USB_DT_CONFIG<<8;
    req.wIndex = 0;
    req.wLength = sizeof(cfg);
    int ret = send_default_control(endp, &req, &cfg);
    if (ret)
        return NULL;

    void *config = malloc_tmphigh(cfg.wTotalLength);
    if (!config)
        return NULL;
    req.wLength = cfg.wTotalLength;
    ret = send_default_control(endp, &req, config);
    if (ret)
        return NULL;
    //hexdump(config, cfg.wTotalLength);
    return config;
}

static u32
set_address(u32 endp)
{
    dprintf(3, "set_address %x\n", endp);
    struct usb_s *cntl = endp2cntl(endp);
    if (cntl->maxaddr >= USB_MAXADDR)
        return 0;

    struct usb_ctrlrequest req;
    req.bRequestType = USB_DIR_OUT | USB_TYPE_STANDARD | USB_RECIP_DEVICE;
    req.bRequest = USB_REQ_SET_ADDRESS;
    req.wValue = cntl->maxaddr + 1;
    req.wIndex = 0;
    req.wLength = 0;
    int ret = send_default_control(endp, &req, NULL);
    if (ret)
        return 0;
    msleep(2);

    cntl->maxaddr++;
    return mkendp(cntl, cntl->maxaddr, 0, endp2speed(endp), endp2maxsize(endp));
}

static int
set_configuration(u32 endp, u16 val)
{
    struct usb_ctrlrequest req;
    req.bRequestType = USB_DIR_OUT | USB_TYPE_STANDARD | USB_RECIP_DEVICE;
    req.bRequest = USB_REQ_SET_CONFIGURATION;
    req.wValue = val;
    req.wIndex = 0;
    req.wLength = 0;
    return send_default_control(endp, &req, NULL);
}

// Called for every found device - see if a driver is available for
// this device and do setup if so.
int
configure_usb_device(struct usb_s *cntl, int lowspeed)
{
    dprintf(1, "config_usb: %p %d\n", cntl, lowspeed);

    // Get device info
    u32 endp = mkendp(cntl, 0, 0, lowspeed, 8);
    struct usb_device_descriptor dinfo;
    int ret = get_device_info8(&dinfo, endp);
    if (ret)
        return 0;
    dprintf(3, "device rev=%04x cls=%02x sub=%02x proto=%02x size=%02x\n"
            , dinfo.bcdUSB, dinfo.bDeviceClass, dinfo.bDeviceSubClass
            , dinfo.bDeviceProtocol, dinfo.bMaxPacketSize0);
    if (dinfo.bMaxPacketSize0 < 8 || dinfo.bMaxPacketSize0 > 64)
        return 0;
    endp = mkendp(cntl, 0, 0, lowspeed, dinfo.bMaxPacketSize0);

    // Get configuration
    struct usb_config_descriptor *config = get_device_config(endp);
    if (!config)
        return 0;

    // Determine if a driver exists for this device - only look at the
    // first interface of the first configuration.
    struct usb_interface_descriptor *iface = (void*)(&config[1]);
    if (iface->bInterfaceClass != USB_CLASS_HID
        || iface->bInterfaceSubClass != USB_INTERFACE_SUBCLASS_BOOT
        || iface->bInterfaceProtocol != USB_INTERFACE_PROTOCOL_KEYBOARD)
        // Not a "boot" keyboard
        goto fail;

    // Set the address and configure device.
    endp = set_address(endp);
    if (!endp)
        goto fail;
    ret = set_configuration(endp, config->bConfigurationValue);
    if (ret)
        goto fail;

    // Configure driver.
    ret = usb_keyboard_init(endp, iface, ((void*)config + config->wTotalLength
                                          - (void*)iface));
    if (ret)
        goto fail;

    free(config);
    return 1;
fail:
    free(config);
    return 0;
}

void
usb_setup(void)
{
    if (! CONFIG_USB)
        return;

    dprintf(3, "init usb\n");

    usb_keyboard_setup();

    // Look for USB controllers
    int count = 0;
    int bdf, max;
    foreachpci(bdf, max) {
        u32 code = pci_config_readl(bdf, PCI_CLASS_REVISION) >> 8;

        if (code >> 8 != PCI_CLASS_SERIAL_USB)
            continue;

        struct usb_s *cntl = &USBControllers[count];
        cntl->bdf = bdf;

        if (code == PCI_CLASS_SERIAL_USB_UHCI)
            run_thread(uhci_init, cntl);
        else if (code == PCI_CLASS_SERIAL_USB_OHCI)
            run_thread(ohci_init, cntl);
        else
            continue;

        count++;
        if (count >= ARRAY_SIZE(USBControllers))
            break;
    }
}
