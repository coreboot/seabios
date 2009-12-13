// Code for handling UHCI USB controllers.
//
// Copyright (C) 2009  Kevin O'Connor <kevin@koconnor.net>
//
// This file may be distributed under the terms of the GNU LGPLv3 license.

#include "util.h" // dprintf
#include "pci.h" // pci_bdf_to_bus
#include "config.h" // CONFIG_*
#include "ioport.h" // outw
#include "usb-uhci.h" // USBLEGSUP
#include "pci_regs.h" // PCI_BASE_ADDRESS_4
#include "usb.h" // struct usb_s
#include "farptr.h" // GET_FLATPTR

static void
reset_uhci(struct usb_s *cntl)
{
    // XXX - don't reset if not needed.

    // Reset PIRQ and SMI
    pci_config_writew(cntl->bdf, USBLEGSUP, USBLEGSUP_RWC);

    // Reset the HC
    outw(USBCMD_HCRESET, cntl->uhci.iobase + USBCMD);
    udelay(5);

    // Disable interrupts and commands (just to be safe).
    outw(0, cntl->uhci.iobase + USBINTR);
    outw(0, cntl->uhci.iobase + USBCMD);
}

static void
configure_uhci(struct usb_s *cntl)
{
    // Allocate ram for schedule storage
    struct uhci_td *term_td = malloc_high(sizeof(*term_td));
    struct uhci_framelist *fl = memalign_high(sizeof(*fl), sizeof(*fl));
    struct uhci_qh *data_qh = malloc_low(sizeof(*data_qh));
    struct uhci_qh *term_qh = malloc_high(sizeof(*term_qh));
    if (!term_td || !fl || !data_qh || !term_qh) {
        dprintf(1, "No ram for uhci init\n");
        return;
    }

    // Work around for PIIX errata
    memset(term_td, 0, sizeof(*term_td));
    term_td->link = UHCI_PTR_TERM;
    term_td->token = (uhci_explen(0) | (0x7f << TD_TOKEN_DEVADDR_SHIFT)
                      | USB_PID_IN);
    memset(term_qh, 0, sizeof(*term_qh));
    term_qh->element = (u32)term_td;
    term_qh->link = UHCI_PTR_TERM;

    // Setup primary queue head.
    memset(data_qh, 0, sizeof(*data_qh));
    data_qh->element = UHCI_PTR_TERM;
    data_qh->link = (u32)term_qh | UHCI_PTR_QH;
    cntl->uhci.qh = data_qh;

    // Set schedule to point to primary queue head
    int i;
    for (i=0; i<ARRAY_SIZE(fl->links); i++) {
        fl->links[i] = (u32)data_qh | UHCI_PTR_QH;
    }

    // Set the frame length to the default: 1 ms exactly
    outb(USBSOF_DEFAULT, cntl->uhci.iobase + USBSOF);

    // Store the frame list base address
    outl((u32)fl->links, cntl->uhci.iobase + USBFLBASEADD);

    // Set the current frame number
    outw(0, cntl->uhci.iobase + USBFRNUM);
}

static void
start_uhci(struct usb_s *cntl)
{
    // Mark as configured and running with a 64-byte max packet.
    outw(USBCMD_RS | USBCMD_CF | USBCMD_MAXP, cntl->uhci.iobase + USBCMD);
}

// Find any devices connected to the root hub.
static int
check_ports(struct usb_s *cntl)
{
    u16 port1 = inw(cntl->uhci.iobase + USBPORTSC1);
    u16 port2 = inw(cntl->uhci.iobase + USBPORTSC2);

    if (!((port1 & USBPORTSC_CCS) || (port2 & USBPORTSC_CCS)))
        // No devices
        return 0;

    // reset ports
    if (port1 & USBPORTSC_CCS)
        outw(USBPORTSC_PR, cntl->uhci.iobase + USBPORTSC1);
    if (port2 & USBPORTSC_CCS)
        outw(USBPORTSC_PR, cntl->uhci.iobase + USBPORTSC2);
    msleep(50);
    outw(0, cntl->uhci.iobase + USBPORTSC1);
    outw(0, cntl->uhci.iobase + USBPORTSC2);
    msleep(10);

    // Configure ports
    int totalcount = 0;
    port1 = inw(cntl->uhci.iobase + USBPORTSC1);
    if (port1 & USBPORTSC_CCS) {
        outw(USBPORTSC_PE, cntl->uhci.iobase + USBPORTSC1);
        int count = configure_usb_device(cntl, !!(port1 & USBPORTSC_LSDA));
        if (! count)
            outw(0, cntl->uhci.iobase + USBPORTSC1);
        totalcount += count;
    }
    port2 = inw(cntl->uhci.iobase + USBPORTSC2);
    if (port2 & USBPORTSC_CCS) {
        outw(USBPORTSC_PE, cntl->uhci.iobase + USBPORTSC2);
        int count = configure_usb_device(cntl, !!(port2 & USBPORTSC_LSDA));
        if (! count)
            outw(0, cntl->uhci.iobase + USBPORTSC2);
        totalcount += count;
    }
    return totalcount;
}

void
uhci_init(void *data)
{
    if (! CONFIG_USB_UHCI)
        return;
    struct usb_s *cntl = data;

    // XXX - don't call pci_config_XXX from a thread
    cntl->type = USB_TYPE_UHCI;
    cntl->uhci.iobase = (pci_config_readl(cntl->bdf, PCI_BASE_ADDRESS_4)
                         & PCI_BASE_ADDRESS_IO_MASK);

    dprintf(3, "UHCI init on dev %02x:%02x.%x (io=%x)\n"
            , pci_bdf_to_bus(cntl->bdf), pci_bdf_to_dev(cntl->bdf)
            , pci_bdf_to_fn(cntl->bdf), cntl->uhci.iobase);

    pci_config_maskw(cntl->bdf, PCI_COMMAND, 0, PCI_COMMAND_MASTER);

    reset_uhci(cntl);
    configure_uhci(cntl);
    start_uhci(cntl);

    int count = check_ports(cntl);
    if (! count) {
        // XXX - no devices; free data structures.
    }
}

static int
wait_qh(struct usb_s *cntl, struct uhci_qh *qh)
{
    // XXX - 500ms just a guess
    u64 end = calc_future_tsc(500);
    for (;;) {
        if (qh->element & UHCI_PTR_TERM)
            return 0;
        if (check_time(end)) {
            struct uhci_td *td = (void*)(qh->element & ~UHCI_PTR_BITS);
            dprintf(1, "Timeout on wait_qh %p (td=%p s=%x c=%x/%x)\n"
                    , qh, td, td->status
                    , inw(cntl->uhci.iobase + USBCMD)
                    , inw(cntl->uhci.iobase + USBSTS));
            return -1;
        }
        yield();
    }
}

int
uhci_control(u32 endp, int dir, const void *cmd, int cmdsize
             , void *data, int datasize)
{
    if (! CONFIG_USB_UHCI)
        return -1;

    dprintf(5, "uhci_control %x\n", endp);
    struct usb_s *cntl = endp2cntl(endp);
    int maxpacket = endp2maxsize(endp);
    int lowspeed = endp2speed(endp);
    int devaddr = endp2devaddr(endp) | (endp2ep(endp) << 7);

    // Setup transfer descriptors
    int count = 2 + DIV_ROUND_UP(datasize, maxpacket);
    struct uhci_td *tds = malloc_tmphigh(sizeof(*tds) * count);

    tds[0].link = (u32)&tds[1] | UHCI_PTR_DEPTH;
    tds[0].status = (uhci_maxerr(3) | (lowspeed ? TD_CTRL_LS : 0)
                     | TD_CTRL_ACTIVE);
    tds[0].token = (uhci_explen(cmdsize) | (devaddr << TD_TOKEN_DEVADDR_SHIFT)
                    | USB_PID_SETUP);
    tds[0].buffer = (void*)cmd;
    int toggle = TD_TOKEN_TOGGLE;
    int i;
    for (i=1; i<count-1; i++) {
        tds[i].link = (u32)&tds[i+1] | UHCI_PTR_DEPTH;
        tds[i].status = (uhci_maxerr(3) | (lowspeed ? TD_CTRL_LS : 0)
                         | TD_CTRL_ACTIVE);
        int len = (i == count-2 ? (datasize - (i-1)*maxpacket) : maxpacket);
        tds[i].token = (uhci_explen(len) | toggle
                        | (devaddr << TD_TOKEN_DEVADDR_SHIFT)
                        | (dir ? USB_PID_IN : USB_PID_OUT));
        tds[i].buffer = data + (i-1) * maxpacket;
        toggle ^= TD_TOKEN_TOGGLE;
    }
    tds[i].link = UHCI_PTR_TERM;
    tds[i].status = (uhci_maxerr(0) | (lowspeed ? TD_CTRL_LS : 0)
                     | TD_CTRL_ACTIVE);
    tds[i].token = (uhci_explen(0) | TD_TOKEN_TOGGLE
                    | (devaddr << TD_TOKEN_DEVADDR_SHIFT)
                    | (dir ? USB_PID_OUT : USB_PID_IN));
    tds[i].buffer = 0;

    // Transfer data
    struct uhci_qh *data_qh = cntl->uhci.qh;
    data_qh->element = (u32)&tds[0];
    int ret = wait_qh(cntl, data_qh);
    if (ret) {
        data_qh->element = UHCI_PTR_TERM;
        // XXX - leak tds
        return ret;
    }
    free(tds);
    return 0;
}

struct usb_pipe *
uhci_alloc_intr_pipe(u32 endp, int period)
{
    if (! CONFIG_USB_UHCI)
        return NULL;

    dprintf(7, "uhci_alloc_intr_pipe %x %d\n", endp, period);
    struct usb_s *cntl = endp2cntl(endp);
    int maxpacket = endp2maxsize(endp);
    int lowspeed = endp2speed(endp);
    int devaddr = endp2devaddr(endp) | (endp2ep(endp) << 7);
    // XXX - just grab 20 for now.
    int count = 20;
    struct uhci_qh *qh = malloc_low(sizeof(*qh));
    struct uhci_td *tds = malloc_low(sizeof(*tds) * count);
    if (!qh || !tds)
        return NULL;
    if (maxpacket > sizeof(tds[0].data))
        // XXX - free qh/tds
        return NULL;

    qh->element = (u32)tds;
    int toggle = 0;
    int i;
    for (i=0; i<count; i++) {
        tds[i].link = (i==count-1 ? (u32)&tds[0] : (u32)&tds[i+1]);
        tds[i].status = (uhci_maxerr(3) | (lowspeed ? TD_CTRL_LS : 0)
                         | TD_CTRL_ACTIVE);
        tds[i].token = (uhci_explen(maxpacket) | toggle
                        | (devaddr << TD_TOKEN_DEVADDR_SHIFT)
                        | USB_PID_IN);
        tds[i].buffer = &tds[i].data;
        toggle ^= TD_TOKEN_TOGGLE;
    }

    qh->next_td = &tds[0];
    qh->pipe.endp = endp;

    // XXX - need schedule - just add to primary list for now.
    struct uhci_qh *data_qh = cntl->uhci.qh;
    qh->link = data_qh->link;
    data_qh->link = (u32)qh | UHCI_PTR_QH;

    return &qh->pipe;
}

int
uhci_poll_intr(struct usb_pipe *pipe, void *data)
{
    ASSERT16();
    if (! CONFIG_USB_UHCI)
        return -1;

    struct uhci_qh *qh = container_of(pipe, struct uhci_qh, pipe);
    struct uhci_td *td = GET_FLATPTR(qh->next_td);
    u32 status = GET_FLATPTR(td->status);
    u32 token = GET_FLATPTR(td->token);
    if (status & TD_CTRL_ACTIVE)
        // No intrs found.
        return -1;
    // XXX - check for errors.

    // Copy data.
    memcpy_far(GET_SEG(SS), data
               , FLATPTR_TO_SEG(td->data), (void*)FLATPTR_TO_OFFSET(td->data)
               , uhci_expected_length(token));

    // Reenable this td.
    u32 next = GET_FLATPTR(td->link);
    SET_FLATPTR(td->status, (uhci_maxerr(0) | (status & TD_CTRL_LS)
                             | TD_CTRL_ACTIVE));
    SET_FLATPTR(qh->next_td, (void*)(next & ~UHCI_PTR_BITS));

    return 0;
}
