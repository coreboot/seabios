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
#include "biosvar.h" // GET_GLOBAL


/****************************************************************
 * Setup
 ****************************************************************/

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
    struct uhci_qh *intr_qh = malloc_high(sizeof(*intr_qh));
    struct uhci_qh *term_qh = malloc_high(sizeof(*term_qh));
    if (!term_td || !fl || !intr_qh || !term_qh) {
        warn_noalloc();
        free(term_td);
        free(fl);
        free(intr_qh);
        free(term_qh);
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

    // Set schedule to point to primary intr queue head
    memset(intr_qh, 0, sizeof(*intr_qh));
    intr_qh->element = UHCI_PTR_TERM;
    intr_qh->link = (u32)term_qh | UHCI_PTR_QH;
    int i;
    for (i=0; i<ARRAY_SIZE(fl->links); i++)
        fl->links[i] = (u32)intr_qh | UHCI_PTR_QH;
    cntl->uhci.framelist = fl;
    cntl->uhci.control_qh = cntl->uhci.bulk_qh = intr_qh;
    barrier();

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
    // XXX - if just powered up, need to wait for USB_TIME_SIGATT?
    u16 port1 = inw(cntl->uhci.iobase + USBPORTSC1);
    u16 port2 = inw(cntl->uhci.iobase + USBPORTSC2);

    if (!((port1 & USBPORTSC_CCS) || (port2 & USBPORTSC_CCS)))
        // No devices
        return 0;

    // XXX - if just powered up, need to wait for USB_TIME_ATTDB?

    // reset ports
    if (port1 & USBPORTSC_CCS)
        outw(USBPORTSC_PR, cntl->uhci.iobase + USBPORTSC1);
    if (port2 & USBPORTSC_CCS)
        outw(USBPORTSC_PR, cntl->uhci.iobase + USBPORTSC2);
    msleep(USB_TIME_DRSTR);

    // Configure ports
    int totalcount = 0;
    outw(0, cntl->uhci.iobase + USBPORTSC1);
    udelay(6); // 64 high-speed bit times
    port1 = inw(cntl->uhci.iobase + USBPORTSC1);
    if (port1 & USBPORTSC_CCS) {
        outw(USBPORTSC_PE, cntl->uhci.iobase + USBPORTSC1);
        msleep(USB_TIME_RSTRCY);
        int count = configure_usb_device(cntl, !!(port1 & USBPORTSC_LSDA));
        if (! count)
            outw(0, cntl->uhci.iobase + USBPORTSC1);
        totalcount += count;
    }
    outw(0, cntl->uhci.iobase + USBPORTSC2);
    udelay(6);
    port2 = inw(cntl->uhci.iobase + USBPORTSC2);
    if (port2 & USBPORTSC_CCS) {
        outw(USBPORTSC_PE, cntl->uhci.iobase + USBPORTSC2);
        msleep(USB_TIME_RSTRCY);
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
    free_pipe(cntl->defaultpipe);
    if (! count) {
        // XXX - no devices; free data structures.
    }
}


/****************************************************************
 * End point communication
 ****************************************************************/

static int
wait_qh(struct usb_s *cntl, struct uhci_qh *qh)
{
    // XXX - 500ms just a guess
    u64 end = calc_future_tsc(500);
    for (;;) {
        if (qh->element & UHCI_PTR_TERM)
            return 0;
        if (check_time(end)) {
            warn_timeout();
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

// Wait for next USB frame to start - for ensuring safe memory release.
static void
uhci_waittick(struct usb_s *cntl)
{
    barrier();
    u16 iobase = GET_GLOBAL(cntl->uhci.iobase);
    u16 startframe = inw(iobase + USBFRNUM);
    u64 end = calc_future_tsc(1000 * 5);
    for (;;) {
        if (inw(iobase + USBFRNUM) != startframe)
            break;
        if (check_time(end)) {
            warn_timeout();
            return;
        }
        yield();
    }
}

struct uhci_pipe {
    struct uhci_qh qh;
    struct uhci_td *next_td;
    struct usb_pipe pipe;
};

void
uhci_free_pipe(struct usb_pipe *p)
{
    if (! CONFIG_USB_UHCI)
        return;
    struct uhci_pipe *pipe = container_of(p, struct uhci_pipe, pipe);
    u32 endp = pipe->pipe.endp;
    dprintf(7, "uhci_free_pipe %x\n", endp);
    struct usb_s *cntl = endp2cntl(endp);

    struct uhci_framelist *fl = cntl->uhci.framelist;
    struct uhci_qh *pos = (void*)(fl->links[0] & ~UHCI_PTR_BITS);
    for (;;) {
        u32 link = pos->link;
        if (link == UHCI_PTR_TERM) {
            // Not found?!  Exit without freeing.
            warn_internalerror();
            return;
        }
        struct uhci_qh *next = (void*)(link & ~UHCI_PTR_BITS);
        if (next == &pipe->qh) {
            pos->link = next->link;
            if (cntl->uhci.control_qh == next)
                cntl->uhci.control_qh = pos;
            if (cntl->uhci.bulk_qh == next)
                cntl->uhci.bulk_qh = pos;
            uhci_waittick(cntl);
            free(pipe);
            return;
        }
        pos = next;
    }
}

struct usb_pipe *
uhci_alloc_control_pipe(u32 endp)
{
    if (! CONFIG_USB_UHCI)
        return NULL;
    struct usb_s *cntl = endp2cntl(endp);
    dprintf(7, "uhci_alloc_control_pipe %x\n", endp);

    // Allocate a queue head.
    struct uhci_pipe *pipe = malloc_tmphigh(sizeof(*pipe));
    if (!pipe) {
        warn_noalloc();
        return NULL;
    }
    pipe->qh.element = UHCI_PTR_TERM;
    pipe->next_td = 0;
    pipe->pipe.endp = endp;

    // Add queue head to controller list.
    struct uhci_qh *control_qh = cntl->uhci.control_qh;
    pipe->qh.link = control_qh->link;
    barrier();
    control_qh->link = (u32)&pipe->qh | UHCI_PTR_QH;
    if (cntl->uhci.bulk_qh == control_qh)
        cntl->uhci.bulk_qh = &pipe->qh;
    return &pipe->pipe;
}

int
uhci_control(struct usb_pipe *p, int dir, const void *cmd, int cmdsize
             , void *data, int datasize)
{
    ASSERT32FLAT();
    if (! CONFIG_USB_UHCI)
        return -1;
    struct uhci_pipe *pipe = container_of(p, struct uhci_pipe, pipe);
    u32 endp = pipe->pipe.endp;

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
    barrier();
    pipe->qh.element = (u32)&tds[0];
    int ret = wait_qh(cntl, &pipe->qh);
    if (ret) {
        pipe->qh.element = UHCI_PTR_TERM;
        uhci_waittick(cntl);
    }
    free(tds);
    return ret;
}

struct usb_pipe *
uhci_alloc_bulk_pipe(u32 endp)
{
    if (! CONFIG_USB_UHCI)
        return NULL;
    struct usb_s *cntl = endp2cntl(endp);
    dprintf(7, "uhci_alloc_bulk_pipe %x\n", endp);

    // Allocate a queue head.
    struct uhci_pipe *pipe = malloc_low(sizeof(*pipe));
    if (!pipe) {
        warn_noalloc();
        return NULL;
    }
    pipe->qh.element = UHCI_PTR_TERM;
    pipe->next_td = 0;
    pipe->pipe.endp = endp;

    // Add queue head to controller list.
    struct uhci_qh *bulk_qh = cntl->uhci.bulk_qh;
    pipe->qh.link = bulk_qh->link;
    barrier();
    bulk_qh->link = (u32)&pipe->qh | UHCI_PTR_QH;

    return &pipe->pipe;
}

static int
wait_td(struct uhci_td *td)
{
    u64 end = calc_future_tsc(5000); // XXX - lookup real time.
    u32 status;
    for (;;) {
        status = td->status;
        if (!(status & TD_CTRL_ACTIVE))
            break;
        if (check_time(end)) {
            warn_timeout();
            return -1;
        }
        yield();
    }
    if (status & TD_CTRL_ANY_ERROR) {
        dprintf(1, "wait_td error - status=%x\n", status);
        return -2;
    }
    return 0;
}

#define STACKTDS 4
#define TDALIGN 16

int
uhci_send_bulk(struct usb_pipe *p, int dir, void *data, int datasize)
{
    struct uhci_pipe *pipe = container_of(p, struct uhci_pipe, pipe);
    u32 endp = GET_FLATPTR(pipe->pipe.endp);
    dprintf(7, "uhci_send_bulk qh=%p endp=%x dir=%d data=%p size=%d\n"
            , &pipe->qh, endp, dir, data, datasize);
    int maxpacket = endp2maxsize(endp);
    int lowspeed = endp2speed(endp);
    int devaddr = endp2devaddr(endp) | (endp2ep(endp) << 7);
    int toggle = (u32)GET_FLATPTR(pipe->next_td); // XXX

    // Allocate 4 tds on stack (16byte aligned)
    u8 tdsbuf[sizeof(struct uhci_td) * STACKTDS + TDALIGN - 1];
    struct uhci_td *tds = (void*)ALIGN((u32)tdsbuf, TDALIGN);
    memset(tds, 0, sizeof(*tds) * STACKTDS);

    // Enable tds
    SET_FLATPTR(pipe->qh.element, (u32)MAKE_FLATPTR(GET_SEG(SS), tds));

    int tdpos = 0;
    while (datasize) {
        struct uhci_td *td = &tds[tdpos++ % STACKTDS];
        int ret = wait_td(td);
        if (ret)
            goto fail;

        int transfer = datasize;
        if (transfer > maxpacket)
            transfer = maxpacket;
        struct uhci_td *nexttd_fl = MAKE_FLATPTR(GET_SEG(SS)
                                                 , &tds[tdpos % STACKTDS]);
        td->link = (transfer==datasize ? UHCI_PTR_TERM : (u32)nexttd_fl);
        td->token = (uhci_explen(transfer) | toggle
                     | (devaddr << TD_TOKEN_DEVADDR_SHIFT)
                     | (dir ? USB_PID_IN : USB_PID_OUT));
        td->buffer = data;
        barrier();
        td->status = (uhci_maxerr(3) | (lowspeed ? TD_CTRL_LS : 0)
                      | TD_CTRL_ACTIVE);
        toggle ^= TD_TOKEN_TOGGLE;

        data += transfer;
        datasize -= transfer;
    }
    int i;
    for (i=0; i<STACKTDS; i++) {
        struct uhci_td *td = &tds[tdpos++ % STACKTDS];
        int ret = wait_td(td);
        if (ret)
            goto fail;
    }

    SET_FLATPTR(pipe->next_td, (void*)toggle); // XXX
    return 0;
fail:
    dprintf(1, "uhci_send_bulk failed\n");
    SET_FLATPTR(pipe->qh.element, UHCI_PTR_TERM);
    uhci_waittick(endp2cntl(endp));
    return -1;
}

struct usb_pipe *
uhci_alloc_intr_pipe(u32 endp, int frameexp)
{
    if (! CONFIG_USB_UHCI)
        return NULL;

    dprintf(7, "uhci_alloc_intr_pipe %x %d\n", endp, frameexp);
    if (frameexp > 10)
        frameexp = 10;
    struct usb_s *cntl = endp2cntl(endp);
    int maxpacket = endp2maxsize(endp);
    int lowspeed = endp2speed(endp);
    int devaddr = endp2devaddr(endp) | (endp2ep(endp) << 7);
    // Determine number of entries needed for 2 timer ticks.
    int ms = 1<<frameexp;
    int count = DIV_ROUND_UP(PIT_TICK_INTERVAL * 1000 * 2, PIT_TICK_RATE * ms);
    struct uhci_pipe *pipe = malloc_low(sizeof(*pipe));
    struct uhci_td *tds = malloc_low(sizeof(*tds) * count);
    if (!pipe || !tds) {
        warn_noalloc();
        goto fail;
    }
    if (maxpacket > sizeof(tds[0].data))
        goto fail;
    pipe->qh.element = (u32)tds;
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

    pipe->next_td = &tds[0];
    pipe->pipe.endp = endp;

    // Add to interrupt schedule.
    struct uhci_framelist *fl = cntl->uhci.framelist;
    if (frameexp == 0) {
        // Add to existing interrupt entry.
        struct uhci_qh *intr_qh = (void*)(fl->links[0] & ~UHCI_PTR_BITS);
        pipe->qh.link = intr_qh->link;
        barrier();
        intr_qh->link = (u32)&pipe->qh | UHCI_PTR_QH;
        if (cntl->uhci.control_qh == intr_qh)
            cntl->uhci.control_qh = &pipe->qh;
        if (cntl->uhci.bulk_qh == intr_qh)
            cntl->uhci.bulk_qh = &pipe->qh;
    } else {
        int startpos = 1<<(frameexp-1);
        pipe->qh.link = fl->links[startpos];
        barrier();
        for (i=startpos; i<ARRAY_SIZE(fl->links); i+=ms)
            fl->links[i] = (u32)&pipe->qh | UHCI_PTR_QH;
    }

    return &pipe->pipe;
fail:
    free(pipe);
    free(tds);
    return NULL;
}

int
uhci_poll_intr(struct usb_pipe *p, void *data)
{
    ASSERT16();
    if (! CONFIG_USB_UHCI)
        return -1;

    struct uhci_pipe *pipe = container_of(p, struct uhci_pipe, pipe);
    struct uhci_td *td = GET_FLATPTR(pipe->next_td);
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
    barrier();
    SET_FLATPTR(td->status, (uhci_maxerr(0) | (status & TD_CTRL_LS)
                             | TD_CTRL_ACTIVE));
    SET_FLATPTR(pipe->next_td, (void*)(next & ~UHCI_PTR_BITS));

    return 0;
}
