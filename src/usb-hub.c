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
set_port_feature(struct usb_pipe *pipe, int port, int feature)
{
    struct usb_ctrlrequest req;
    req.bRequestType = USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_OTHER;
    req.bRequest = USB_REQ_SET_FEATURE;
    req.wValue = feature;
    req.wIndex = port;
    req.wLength = 0;
    return send_default_control(pipe, &req, NULL);
}

static int
clear_port_feature(struct usb_pipe *pipe, int port, int feature)
{
    struct usb_ctrlrequest req;
    req.bRequestType = USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_OTHER;
    req.bRequest = USB_REQ_CLEAR_FEATURE;
    req.wValue = feature;
    req.wIndex = port;
    req.wLength = 0;
    return send_default_control(pipe, &req, NULL);
}

static int
get_port_status(struct usb_pipe *pipe, int port, struct usb_port_status *sts)
{
    struct usb_ctrlrequest req;
    req.bRequestType = USB_DIR_IN | USB_TYPE_CLASS | USB_RECIP_OTHER;
    req.bRequest = USB_REQ_GET_STATUS;
    req.wValue = 0;
    req.wIndex = port;
    req.wLength = sizeof(*sts);
    return send_default_control(pipe, &req, sts);
}

// Configure a usb hub and then find devices connected to it.
int
usb_hub_init(struct usb_pipe *pipe)
{
    if (!CONFIG_USB_HUB)
        return -1;

    struct usb_hub_descriptor desc;
    int ret = get_hub_desc(pipe, &desc);
    if (ret)
        return ret;

    // Turn on power to all ports.
    int i;
    for (i=1; i<=desc.bNbrPorts; i++) {
        ret = set_port_feature(pipe, i, USB_PORT_FEAT_POWER);
        if (ret)
            goto fail;
    }

    // Wait for port detection.
    msleep(desc.bPwrOn2PwrGood * 2 + USB_TIME_SIGATT);
    // XXX - should poll for ports becoming active sooner and then
    // possibly wait USB_TIME_ATTDB.

    // Detect down stream devices.
    struct usb_s *cntl = endp2cntl(pipe->endp);
    int totalcount = 0;
    for (i=1; i<=desc.bNbrPorts; i++) {
        struct usb_port_status sts;
        ret = get_port_status(pipe, i, &sts);
        if (ret)
            goto fail;
        if (!(sts.wPortStatus & USB_PORT_STAT_CONNECTION))
            // XXX - power down port?
            continue;

        // Reset port.
        ret = set_port_feature(pipe, i, USB_PORT_FEAT_RESET);
        if (ret)
            goto fail;

        // Wait for reset to complete.
        u64 end = calc_future_tsc(USB_TIME_DRST * 2);
        for (;;) {
            ret = get_port_status(pipe, i, &sts);
            if (ret)
                goto fail;
            if (!(sts.wPortStatus & USB_PORT_STAT_RESET))
                break;
            if (check_time(end)) {
                // Timeout.
                warn_timeout();
                goto fail;
            }
            yield();
        }
        if (!(sts.wPortStatus & USB_PORT_STAT_CONNECTION))
            // Device no longer present.  XXX - power down port?
            continue;

        // XXX - should try to parallelize configuration.
        int count = configure_usb_device(
            cntl, !!(sts.wPortStatus & USB_PORT_STAT_LOW_SPEED));
        if (! count) {
            // Shutdown port
            ret = clear_port_feature(pipe, i, USB_PORT_FEAT_ENABLE);
            if (ret)
                goto fail;
        }
        totalcount += count;
    }

    dprintf(1, "Initialized USB HUB (%d ports used)\n", totalcount);
    if (totalcount)
        return 0;
    return -1;

fail:
    dprintf(1, "Failure on hub setup\n");
    return -1;
}
