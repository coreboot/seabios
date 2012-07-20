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
#include "usb-ehci.h" // ehci_init
#include "usb-hid.h" // usb_keyboard_setup
#include "usb-hub.h" // usb_hub_init
#include "usb-msc.h" // usb_msc_init
#include "usb-uas.h" // usb_uas_init
#include "usb.h" // struct usb_s
#include "biosvar.h" // GET_GLOBAL


/****************************************************************
 * Controller function wrappers
 ****************************************************************/

// Allocate an async pipe (control or bulk).
struct usb_pipe *
usb_alloc_pipe(struct usbdevice_s *usbdev
               , struct usb_endpoint_descriptor *epdesc)
{
    switch (usbdev->hub->cntl->type) {
    default:
    case USB_TYPE_UHCI:
        return uhci_alloc_pipe(usbdev, epdesc);
    case USB_TYPE_OHCI:
        return ohci_alloc_pipe(usbdev, epdesc);
    case USB_TYPE_EHCI:
        return ehci_alloc_pipe(usbdev, epdesc);
    }
}

// Send a message on a control pipe using the default control descriptor.
static int
send_control(struct usb_pipe *pipe, int dir, const void *cmd, int cmdsize
             , void *data, int datasize)
{
    ASSERT32FLAT();
    switch (pipe->type) {
    default:
    case USB_TYPE_UHCI:
        return uhci_control(pipe, dir, cmd, cmdsize, data, datasize);
    case USB_TYPE_OHCI:
        return ohci_control(pipe, dir, cmd, cmdsize, data, datasize);
    case USB_TYPE_EHCI:
        return ehci_control(pipe, dir, cmd, cmdsize, data, datasize);
    }
}

int
usb_send_bulk(struct usb_pipe *pipe_fl, int dir, void *data, int datasize)
{
    switch (GET_LOWFLAT(pipe_fl->type)) {
    default:
    case USB_TYPE_UHCI:
        return uhci_send_bulk(pipe_fl, dir, data, datasize);
    case USB_TYPE_OHCI:
        return ohci_send_bulk(pipe_fl, dir, data, datasize);
    case USB_TYPE_EHCI:
        return ehci_send_bulk(pipe_fl, dir, data, datasize);
    }
}

int
usb_poll_intr(struct usb_pipe *pipe_fl, void *data)
{
    switch (GET_LOWFLAT(pipe_fl->type)) {
    default:
    case USB_TYPE_UHCI:
        return uhci_poll_intr(pipe_fl, data);
    case USB_TYPE_OHCI:
        return ohci_poll_intr(pipe_fl, data);
    case USB_TYPE_EHCI:
        return ehci_poll_intr(pipe_fl, data);
    }
}


/****************************************************************
 * Helper functions
 ****************************************************************/

// Send a message to the default control pipe of a device.
int
send_default_control(struct usb_pipe *pipe, const struct usb_ctrlrequest *req
                     , void *data)
{
    return send_control(pipe, req->bRequestType & USB_DIR_IN
                        , req, sizeof(*req), data, req->wLength);
}

// Free an allocated control or bulk pipe.
void
free_pipe(struct usb_pipe *pipe)
{
    ASSERT32FLAT();
    if (!pipe)
        return;
    // Add to controller's free list.
    struct usb_s *cntl = pipe->cntl;
    pipe->freenext = cntl->freelist;
    cntl->freelist = pipe;
}

// Check for an available pipe on the freelist.
struct usb_pipe *
usb_getFreePipe(struct usb_s *cntl, u8 eptype)
{
    struct usb_pipe **pfree = &cntl->freelist;
    for (;;) {
        struct usb_pipe *pipe = *pfree;
        if (!pipe)
            return NULL;
        if (pipe->eptype == eptype) {
            *pfree = pipe->freenext;
            return pipe;
        }
        pfree = &pipe->freenext;
    }
}

// Fill "pipe" endpoint info from an endpoint descriptor.
void
usb_desc2pipe(struct usb_pipe *pipe, struct usbdevice_s *usbdev
              , struct usb_endpoint_descriptor *epdesc)
{
    pipe->cntl = usbdev->hub->cntl;
    pipe->type = usbdev->hub->cntl->type;
    pipe->ep = epdesc->bEndpointAddress & USB_ENDPOINT_NUMBER_MASK;
    pipe->devaddr = usbdev->devaddr;
    pipe->speed = usbdev->speed;
    pipe->maxpacket = epdesc->wMaxPacketSize;
    pipe->eptype = epdesc->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK;
}

// Find the exponential period of the requested interrupt end point.
int
usb_getFrameExp(struct usbdevice_s *usbdev
                , struct usb_endpoint_descriptor *epdesc)
{
    int period = epdesc->bInterval;
    if (usbdev->speed != USB_HIGHSPEED)
        return (period <= 0) ? 0 : __fls(period);
    return (period <= 4) ? 0 : period - 4;
}

// Find the first endpoing of a given type in an interface description.
struct usb_endpoint_descriptor *
findEndPointDesc(struct usbdevice_s *usbdev, int type, int dir)
{
    struct usb_endpoint_descriptor *epdesc = (void*)&usbdev->iface[1];
    for (;;) {
        if ((void*)epdesc >= (void*)usbdev->iface + usbdev->imax
            || epdesc->bDescriptorType == USB_DT_INTERFACE) {
            return NULL;
        }
        if (epdesc->bDescriptorType == USB_DT_ENDPOINT
            && (epdesc->bEndpointAddress & USB_ENDPOINT_DIR_MASK) == dir
            && (epdesc->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK) == type)
            return epdesc;
        epdesc = (void*)epdesc + epdesc->bLength;
    }
}

// Get the first 8 bytes of the device descriptor.
static int
get_device_info8(struct usb_pipe *pipe, struct usb_device_descriptor *dinfo)
{
    struct usb_ctrlrequest req;
    req.bRequestType = USB_DIR_IN | USB_TYPE_STANDARD | USB_RECIP_DEVICE;
    req.bRequest = USB_REQ_GET_DESCRIPTOR;
    req.wValue = USB_DT_DEVICE<<8;
    req.wIndex = 0;
    req.wLength = 8;
    return send_default_control(pipe, &req, dinfo);
}

static struct usb_config_descriptor *
get_device_config(struct usb_pipe *pipe)
{
    struct usb_config_descriptor cfg;

    struct usb_ctrlrequest req;
    req.bRequestType = USB_DIR_IN | USB_TYPE_STANDARD | USB_RECIP_DEVICE;
    req.bRequest = USB_REQ_GET_DESCRIPTOR;
    req.wValue = USB_DT_CONFIG<<8;
    req.wIndex = 0;
    req.wLength = sizeof(cfg);
    int ret = send_default_control(pipe, &req, &cfg);
    if (ret)
        return NULL;

    void *config = malloc_tmphigh(cfg.wTotalLength);
    if (!config)
        return NULL;
    req.wLength = cfg.wTotalLength;
    ret = send_default_control(pipe, &req, config);
    if (ret)
        return NULL;
    //hexdump(config, cfg.wTotalLength);
    return config;
}

static int
set_configuration(struct usb_pipe *pipe, u16 val)
{
    struct usb_ctrlrequest req;
    req.bRequestType = USB_DIR_OUT | USB_TYPE_STANDARD | USB_RECIP_DEVICE;
    req.bRequest = USB_REQ_SET_CONFIGURATION;
    req.wValue = val;
    req.wIndex = 0;
    req.wLength = 0;
    return send_default_control(pipe, &req, NULL);
}


/****************************************************************
 * Initialization and enumeration
 ****************************************************************/

// Assign an address to a device in the default state on the given
// controller.
static int
usb_set_address(struct usbdevice_s *usbdev)
{
    ASSERT32FLAT();
    struct usb_s *cntl = usbdev->hub->cntl;
    dprintf(3, "set_address %p\n", cntl);
    if (cntl->maxaddr >= USB_MAXADDR)
        return -1;

    // Create a pipe for the default address.
    struct usb_endpoint_descriptor epdesc = {
        .wMaxPacketSize = 8,
        .bmAttributes = USB_ENDPOINT_XFER_CONTROL,
    };
    usbdev->defpipe = usb_alloc_pipe(usbdev, &epdesc);
    if (!usbdev->defpipe)
        return -1;

    msleep(USB_TIME_RSTRCY);

    // Send set_address command.
    struct usb_ctrlrequest req;
    req.bRequestType = USB_DIR_OUT | USB_TYPE_STANDARD | USB_RECIP_DEVICE;
    req.bRequest = USB_REQ_SET_ADDRESS;
    req.wValue = cntl->maxaddr + 1;
    req.wIndex = 0;
    req.wLength = 0;
    int ret = send_default_control(usbdev->defpipe, &req, NULL);
    free_pipe(usbdev->defpipe);
    if (ret)
        return -1;

    msleep(USB_TIME_SETADDR_RECOVERY);

    cntl->maxaddr++;
    usbdev->devaddr = cntl->maxaddr;
    usbdev->defpipe = usb_alloc_pipe(usbdev, &epdesc);
    if (!usbdev->defpipe)
        return -1;
    return 0;
}

// Called for every found device - see if a driver is available for
// this device and do setup if so.
static int
configure_usb_device(struct usbdevice_s *usbdev)
{
    ASSERT32FLAT();
    dprintf(3, "config_usb: %p\n", usbdev->defpipe);

    // Set the max packet size for endpoint 0 of this device.
    struct usb_device_descriptor dinfo;
    int ret = get_device_info8(usbdev->defpipe, &dinfo);
    if (ret)
        return 0;
    dprintf(3, "device rev=%04x cls=%02x sub=%02x proto=%02x size=%02x\n"
            , dinfo.bcdUSB, dinfo.bDeviceClass, dinfo.bDeviceSubClass
            , dinfo.bDeviceProtocol, dinfo.bMaxPacketSize0);
    if (dinfo.bMaxPacketSize0 < 8 || dinfo.bMaxPacketSize0 > 64)
        return 0;
    free_pipe(usbdev->defpipe);
    struct usb_endpoint_descriptor epdesc = {
        .wMaxPacketSize = dinfo.bMaxPacketSize0,
        .bmAttributes = USB_ENDPOINT_XFER_CONTROL,
    };
    usbdev->defpipe = usb_alloc_pipe(usbdev, &epdesc);
    if (!usbdev->defpipe)
        return -1;

    // Get configuration
    struct usb_config_descriptor *config = get_device_config(usbdev->defpipe);
    if (!config)
        return 0;

    // Determine if a driver exists for this device - only look at the
    // first interface of the first configuration.
    struct usb_interface_descriptor *iface = (void*)(&config[1]);
    if (iface->bInterfaceClass != USB_CLASS_HID
        && iface->bInterfaceClass != USB_CLASS_MASS_STORAGE
        && iface->bInterfaceClass != USB_CLASS_HUB)
        // Not a supported device.
        goto fail;

    // Set the configuration.
    ret = set_configuration(usbdev->defpipe, config->bConfigurationValue);
    if (ret)
        goto fail;

    // Configure driver.
    usbdev->config = config;
    usbdev->iface = iface;
    usbdev->imax = (void*)config + config->wTotalLength - (void*)iface;
    if (iface->bInterfaceClass == USB_CLASS_HUB)
        ret = usb_hub_init(usbdev);
    else if (iface->bInterfaceClass == USB_CLASS_MASS_STORAGE) {
        if (iface->bInterfaceProtocol == US_PR_BULK)
            ret = usb_msc_init(usbdev);
        if (iface->bInterfaceProtocol == US_PR_UAS)
            ret = usb_uas_init(usbdev);
    } else
        ret = usb_hid_init(usbdev);
    if (ret)
        goto fail;

    free(config);
    return 1;
fail:
    free(config);
    return 0;
}

static void
usb_init_hub_port(void *data)
{
    struct usbdevice_s *usbdev = data;
    struct usbhub_s *hub = usbdev->hub;
    u32 port = usbdev->port;

    // Detect if device present (and possibly start reset)
    int ret = hub->op->detect(hub, port);
    if (ret)
        // No device present
        goto done;

    // Reset port and determine device speed
    mutex_lock(&hub->cntl->resetlock);
    ret = hub->op->reset(hub, port);
    if (ret < 0)
        // Reset failed
        goto resetfail;
    usbdev->speed = ret;

    // Set address of port
    ret = usb_set_address(usbdev);
    if (ret) {
        hub->op->disconnect(hub, port);
        goto resetfail;
    }
    mutex_unlock(&hub->cntl->resetlock);

    // Configure the device
    int count = configure_usb_device(usbdev);
    free_pipe(usbdev->defpipe);
    if (!count)
        hub->op->disconnect(hub, port);
    hub->devcount += count;
done:
    hub->threads--;
    free(usbdev);
    return;

resetfail:
    mutex_unlock(&hub->cntl->resetlock);
    goto done;
}

void
usb_enumerate(struct usbhub_s *hub)
{
    u32 portcount = hub->portcount;
    hub->threads = portcount;

    // Launch a thread for every port.
    int i;
    for (i=0; i<portcount; i++) {
        struct usbdevice_s *usbdev = malloc_tmphigh(sizeof(*usbdev));
        if (!usbdev) {
            warn_noalloc();
            continue;
        }
        memset(usbdev, 0, sizeof(*usbdev));
        usbdev->hub = hub;
        usbdev->port = i;
        run_thread(usb_init_hub_port, usbdev);
    }

    // Wait for threads to complete.
    while (hub->threads)
        yield();
}

void
usb_setup(void)
{
    ASSERT32FLAT();
    if (! CONFIG_USB)
        return;

    dprintf(3, "init usb\n");

    // Look for USB controllers
    int count = 0;
    struct pci_device *ehcipci = PCIDevices;
    struct pci_device *pci;
    foreachpci(pci) {
        if (pci->class != PCI_CLASS_SERIAL_USB)
            continue;

        if (pci->bdf >= ehcipci->bdf) {
            // Check to see if this device has an ehci controller
            int found = 0;
            ehcipci = pci;
            for (;;) {
                if (pci_classprog(ehcipci) == PCI_CLASS_SERIAL_USB_EHCI) {
                    // Found an ehci controller.
                    int ret = ehci_init(ehcipci, count++, pci);
                    if (ret)
                        // Error
                        break;
                    count += found;
                    pci = ehcipci;
                    break;
                }
                if (ehcipci->class == PCI_CLASS_SERIAL_USB)
                    found++;
                ehcipci = ehcipci->next;
                if (!ehcipci || (pci_bdf_to_busdev(ehcipci->bdf)
                                 != pci_bdf_to_busdev(pci->bdf)))
                    // No ehci controller found.
                    break;
            }
        }

        if (pci_classprog(pci) == PCI_CLASS_SERIAL_USB_UHCI)
            uhci_init(pci, count++);
        else if (pci_classprog(pci) == PCI_CLASS_SERIAL_USB_OHCI)
            ohci_init(pci, count++);
    }
}
