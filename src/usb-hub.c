// Code for handling standard USB hubs.
//
// Copyright (C) 2010  Kevin O'Connor <kevin@koconnor.net>
//
// This file may be distributed under the terms of the GNU LGPLv3 license.

#include "util.h" // dprintf
#include "config.h" // CONFIG_USB_HUB
#include "usb-hub.h" // struct usb_hub_descriptor
#include "usb.h" // struct usb_s

static int
get_hub_desc(struct usb_pipe *pipe, struct usb_hub_descriptor *desc)
{
    struct usb_ctrlrequest req;
    req.bRequestType = USB_DIR_IN | USB_TYPE_CLASS | USB_RECIP_DEVICE;
    req.bRequest = USB_REQ_GET_DESCRIPTOR;
    req.wValue = USB_DT_HUB<<8;
    req.wIndex = 0;
    req.wLength = sizeof(*desc);
    return send_default_control(pipe, &req, desc);
}

static int
set_port_feature(struct usbhub_s *hub, int port, int feature)
{
    struct usb_ctrlrequest req;
    req.bRequestType = USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_OTHER;
    req.bRequest = USB_REQ_SET_FEATURE;
    req.wValue = feature;
    req.wIndex = port;
    req.wLength = 0;
    mutex_lock(&hub->lock);
    int ret = send_default_control(hub->pipe, &req, NULL);
    mutex_unlock(&hub->lock);
    return ret;
}

static int
clear_port_feature(struct usbhub_s *hub, int port, int feature)
{
    struct usb_ctrlrequest req;
    req.bRequestType = USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_OTHER;
    req.bRequest = USB_REQ_CLEAR_FEATURE;
    req.wValue = feature;
    req.wIndex = port;
    req.wLength = 0;
    mutex_lock(&hub->lock);
    int ret = send_default_control(hub->pipe, &req, NULL);
    mutex_unlock(&hub->lock);
    return ret;
}

static int
get_port_status(struct usbhub_s *hub, int port, struct usb_port_status *sts)
{
    struct usb_ctrlrequest req;
    req.bRequestType = USB_DIR_IN | USB_TYPE_CLASS | USB_RECIP_OTHER;
    req.bRequest = USB_REQ_GET_STATUS;
    req.wValue = 0;
    req.wIndex = port;
    req.wLength = sizeof(*sts);
    mutex_lock(&hub->lock);
    int ret = send_default_control(hub->pipe, &req, sts);
    mutex_unlock(&hub->lock);
    return ret;
}

static void
init_hub_port(void *data)
{
    struct usbhub_s *hub = data;
    u32 port = hub->port; // XXX - find better way to pass port

    // Turn on power to port.
    int ret = set_port_feature(hub, port, USB_PORT_FEAT_POWER);
    if (ret)
        goto fail;

    // Wait for port power to stabilize.
    msleep(hub->powerwait);

    // Check periodically for a device connect.
    struct usb_port_status sts;
    u64 end = calc_future_tsc(USB_TIME_SIGATT);
    for (;;) {
        ret = get_port_status(hub, port, &sts);
        if (ret)
            goto fail;
        if (sts.wPortStatus & USB_PORT_STAT_CONNECTION)
            // Device connected.
            break;
        if (check_time(end))
            // No device found.
            goto done;
        msleep(5);
    }

    // XXX - wait USB_TIME_ATTDB time?

    // Reset port.
    mutex_lock(&hub->cntl->resetlock);
    ret = set_port_feature(hub, port, USB_PORT_FEAT_RESET);
    if (ret)
        goto resetfail;

    // Wait for reset to complete.
    end = calc_future_tsc(USB_TIME_DRST * 2);
    for (;;) {
        ret = get_port_status(hub, port, &sts);
        if (ret)
            goto resetfail;
        if (!(sts.wPortStatus & USB_PORT_STAT_RESET))
            break;
        if (check_time(end)) {
            warn_timeout();
            goto resetfail;
        }
        msleep(5);
    }

    // Reset complete.
    if (!(sts.wPortStatus & USB_PORT_STAT_CONNECTION))
        // Device no longer present
        goto resetfail;

    // Set address of port
    struct usb_pipe *pipe = usb_set_address(
        hub->cntl, !!(sts.wPortStatus & USB_PORT_STAT_LOW_SPEED));
    if (!pipe)
        goto resetfail;
    mutex_unlock(&hub->cntl->resetlock);

    // Configure the device
    int count = configure_usb_device(pipe);
    free_pipe(pipe);
    if (!count) {
        ret = clear_port_feature(hub, port, USB_PORT_FEAT_ENABLE);
        if (ret)
            goto fail;
    }
    hub->devcount += count;
done:
    hub->threads--;
    return;

resetfail:
    clear_port_feature(hub, port, USB_PORT_FEAT_ENABLE);
    mutex_unlock(&hub->cntl->resetlock);
fail:
    dprintf(1, "Failure on hub port %d setup\n", port);
    goto done;
}

// Configure a usb hub and then find devices connected to it.
int
usb_hub_init(struct usb_pipe *pipe)
{
    ASSERT32FLAT();
    if (!CONFIG_USB_HUB)
        return -1;

    struct usb_hub_descriptor desc;
    int ret = get_hub_desc(pipe, &desc);
    if (ret)
        return ret;

    struct usbhub_s hub;
    memset(&hub, 0, sizeof(hub));
    hub.pipe = pipe;
    hub.cntl = endp2cntl(pipe->endp);
    hub.powerwait = desc.bPwrOn2PwrGood * 2;

    // Launch a thread for every port.
    int i;
    for (i=1; i<=desc.bNbrPorts; i++) {
        hub.port = i;
        hub.threads++;
        run_thread(init_hub_port, &hub);
    }

    // Wait for threads to complete.
    while (hub.threads)
        yield();

    dprintf(1, "Initialized USB HUB (%d ports used)\n", hub.devcount);
    if (hub.devcount)
        return 0;
    return -1;
}
