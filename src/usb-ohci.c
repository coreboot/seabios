// Code for handling OHCI USB controllers.
//
// Copyright (C) 2009  Kevin O'Connor <kevin@koconnor.net>
//
// This file may be distributed under the terms of the GNU LGPLv3 license.

#include "util.h" // dprintf
#include "pci.h" // pci_bdf_to_bus
#include "config.h" // CONFIG_*
#include "usb-ohci.h" // struct ohci_hcca
#include "pci_regs.h" // PCI_BASE_ADDRESS_0
#include "usb.h" // struct usb_s
#include "farptr.h" // GET_FLATPTR

#define FIT                     (1 << 31)

static int
start_ohci(struct usb_s *cntl, struct ohci_hcca *hcca)
{
    u32 oldfminterval = readl(&cntl->ohci.regs->fminterval);
    u32 oldrwc = readl(&cntl->ohci.regs->control) & OHCI_CTRL_RWC;

    // XXX - check if already running?

    // Do reset
    writel(&cntl->ohci.regs->control, OHCI_USB_RESET | oldrwc);
    readl(&cntl->ohci.regs->control); // flush writes
    msleep(50);

    // Do software init (min 10us, max 2ms)
    u64 end = calc_future_tsc_usec(10);
    writel(&cntl->ohci.regs->cmdstatus, OHCI_HCR);
    for (;;) {
        u32 status = readl(&cntl->ohci.regs->cmdstatus);
        if (! status & OHCI_HCR)
            break;
        if (check_time(end)) {
            dprintf(1, "Timeout on ohci software reset\n");
            return -1;
        }
    }

    // Init memory
    writel(&cntl->ohci.regs->ed_controlhead, (u32)cntl->ohci.control_ed);
    writel(&cntl->ohci.regs->ed_bulkhead, 0);
    writel(&cntl->ohci.regs->hcca, (u32)hcca);

    // Init fminterval
    u32 fi = oldfminterval & 0x3fff;
    writel(&cntl->ohci.regs->fminterval
           , (((oldfminterval & FIT) ^ FIT)
              | fi | (((6 * (fi - 210)) / 7) << 16)));
    writel(&cntl->ohci.regs->periodicstart, ((9 * fi) / 10) & 0x3fff);
    readl(&cntl->ohci.regs->control); // flush writes

    // XXX - verify that fminterval was setup correctly.

    // Go into operational state
    writel(&cntl->ohci.regs->control
           , (OHCI_CTRL_CBSR | OHCI_CTRL_CLE | OHCI_CTRL_PLE
              | OHCI_USB_OPER | oldrwc));
    readl(&cntl->ohci.regs->control); // flush writes

    return 0;
}

static void
stop_ohci(struct usb_s *cntl)
{
    u32 oldrwc = readl(&cntl->ohci.regs->control) & OHCI_CTRL_RWC;
    writel(&cntl->ohci.regs->control, oldrwc);
    readl(&cntl->ohci.regs->control); // flush writes
}

// Find any devices connected to the root hub.
static int
check_ohci_ports(struct usb_s *cntl)
{
    // Turn on power for all devices on roothub.
    u32 rha = readl(&cntl->ohci.regs->roothub_a);
    rha &= ~(RH_A_PSM | RH_A_OCPM);
    writel(&cntl->ohci.regs->roothub_status, RH_HS_LPSC);
    writel(&cntl->ohci.regs->roothub_b, RH_B_PPCM);
    msleep((rha >> 24) * 2);

    // Count and reset connected devices
    int ports = rha & RH_A_NDP;
    int totalcount = 0;
    int i;
    for (i=0; i<ports; i++)
        if (readl(&cntl->ohci.regs->roothub_portstatus[i]) & RH_PS_CCS) {
            writel(&cntl->ohci.regs->roothub_portstatus[i], RH_PS_PRS);
            totalcount++;
        }
    if (!totalcount)
        // No devices connected
        goto shutdown;

    msleep(60);    // XXX - should poll instead of using timer.

    totalcount = 0;
    for (i=0; i<ports; i++) {
        u32 sts = readl(&cntl->ohci.regs->roothub_portstatus[i]);
        if ((sts & (RH_PS_CCS|RH_PS_PES)) == (RH_PS_CCS|RH_PS_PES)) {
            int count = configure_usb_device(cntl, !!(sts & RH_PS_LSDA));
            if (! count)
                // Shutdown port
                writel(&cntl->ohci.regs->roothub_portstatus[i]
                       , RH_PS_CCS|RH_PS_LSDA);
            totalcount += count;
        }
    }
    if (!totalcount)
        goto shutdown;

    return totalcount;

shutdown:
    // Turn off power to all ports
    writel(&cntl->ohci.regs->roothub_status, RH_HS_LPS);
    return 0;
}

void
ohci_init(void *data)
{
    if (! CONFIG_USB_OHCI)
        return;
    struct usb_s *cntl = data;

    // XXX - don't call pci_config_XXX from a thread
    cntl->type = USB_TYPE_OHCI;
    u32 baseaddr = pci_config_readl(cntl->bdf, PCI_BASE_ADDRESS_0);
    cntl->ohci.regs = (void*)(baseaddr & PCI_BASE_ADDRESS_MEM_MASK);

    dprintf(3, "OHCI init on dev %02x:%02x.%x (regs=%p)\n"
            , pci_bdf_to_bus(cntl->bdf), pci_bdf_to_dev(cntl->bdf)
            , pci_bdf_to_fn(cntl->bdf), cntl->ohci.regs);

    // Enable bus mastering and memory access.
    pci_config_maskw(cntl->bdf, PCI_COMMAND
                     , 0, PCI_COMMAND_MASTER|PCI_COMMAND_MEMORY);

    // XXX - check for and disable SMM control?

    // Disable interrupts
    writel(&cntl->ohci.regs->intrdisable, ~0);
    writel(&cntl->ohci.regs->intrstatus, ~0);

    // Allocate memory
    struct ohci_hcca *hcca = memalign_high(256, sizeof(*hcca));
    struct ohci_ed *control_ed = malloc_high(sizeof(*control_ed));
    if (!hcca || !control_ed) {
        dprintf(1, "No ram for ohci init\n");
        return;
    }
    memset(hcca, 0, sizeof(*hcca));
    memset(control_ed, 0, sizeof(*control_ed));
    control_ed->hwINFO = ED_SKIP;
    cntl->ohci.control_ed = control_ed;

    int ret = start_ohci(cntl, hcca);
    if (ret)
        goto err;

    int count = check_ohci_ports(cntl);
    if (! count)
        goto err;
    return;

err:
    stop_ohci(cntl);
    free(hcca);
    free(control_ed);
}

static int
wait_ed(struct ohci_ed *ed)
{
    // XXX - 500ms just a guess
    u64 end = calc_future_tsc(500);
    for (;;) {
        if (ed->hwHeadP == ed->hwTailP)
            return 0;
        if (check_time(end)) {
            dprintf(1, "Timeout on wait_ed %p\n", ed);
            return -1;
        }
        yield();
    }
}

int
ohci_control(u32 endp, int dir, const void *cmd, int cmdsize
             , void *data, int datasize)
{
    if (! CONFIG_USB_OHCI)
        return -1;

    dprintf(5, "ohci_control %x\n", endp);
    struct usb_s *cntl = endp2cntl(endp);
    int maxpacket = endp2maxsize(endp);
    int lowspeed = endp2speed(endp);
    int devaddr = endp2devaddr(endp) | (endp2ep(endp) << 7);

    // Setup transfer descriptors
    struct ohci_td *tds = malloc_tmphigh(sizeof(*tds) * 3);
    tds[0].hwINFO = TD_DP_SETUP | TD_T_DATA0 | TD_CC;
    tds[0].hwCBP = (u32)cmd;
    tds[0].hwNextTD = (u32)&tds[1];
    tds[0].hwBE = (u32)cmd + cmdsize - 1;
    tds[1].hwINFO = (dir ? TD_DP_IN : TD_DP_OUT) | TD_T_DATA1 | TD_CC;
    tds[1].hwCBP = datasize ? (u32)data : 0;
    tds[1].hwNextTD = (u32)&tds[2];
    tds[1].hwBE = (u32)data + datasize - 1;
    tds[2].hwINFO = (dir ? TD_DP_OUT : TD_DP_IN) | TD_T_DATA1 | TD_CC;
    tds[2].hwCBP = 0;
    tds[2].hwNextTD = (u32)&tds[3];
    tds[2].hwBE = 0;

    // Transfer data
    struct ohci_ed *ed = cntl->ohci.control_ed;
    ed->hwINFO = ED_SKIP;
    barrier();
    ed->hwHeadP = (u32)&tds[0];
    ed->hwTailP = (u32)&tds[3];
    barrier();
    ed->hwINFO = devaddr | (maxpacket << 16) | (lowspeed ? ED_LOWSPEED : 0);
    writel(&cntl->ohci.regs->cmdstatus, OHCI_CLF);

    int ret = wait_ed(ed);
    ed->hwINFO = ED_SKIP;
    if (ret)
        usleep(1); // XXX - in case controller still accessing tds
    free(tds);
    return ret;
}

struct ohci_pipe {
    struct ohci_ed ed;
    struct usb_pipe pipe;
    void *data;
    int count;
    struct ohci_td *tds;
};

struct usb_pipe *
ohci_alloc_intr_pipe(u32 endp, int period)
{
    if (! CONFIG_USB_OHCI)
        return NULL;

    dprintf(7, "ohci_alloc_intr_pipe %x %d\n", endp, period);
    struct usb_s *cntl = endp2cntl(endp);
    int maxpacket = endp2maxsize(endp);
    int lowspeed = endp2speed(endp);
    int devaddr = endp2devaddr(endp) | (endp2ep(endp) << 7);
    // XXX - just grab 20 for now.
    int count = 20;
    struct ohci_pipe *pipe = malloc_low(sizeof(*pipe));
    struct ohci_td *tds = malloc_low(sizeof(*tds) * count);
    void *data = malloc_low(maxpacket * count);
    if (!pipe || !tds || !data)
        goto err;

    struct ohci_ed *ed = &pipe->ed;
    ed->hwHeadP = (u32)&tds[0];
    ed->hwTailP = (u32)&tds[count-1];
    ed->hwINFO = devaddr | (maxpacket << 16) | (lowspeed ? ED_LOWSPEED : 0);
    ed->hwNextED = 0;

    int i;
    for (i=0; i<count-1; i++) {
        tds[i].hwINFO = TD_DP_IN | TD_T_TOGGLE | TD_CC;
        tds[i].hwCBP = (u32)data + maxpacket * i;
        tds[i].hwNextTD = (u32)&tds[i+1];
        tds[i].hwBE = tds[i].hwCBP + maxpacket - 1;
    }

    // XXX - need schedule - just add to primary list for now.
    barrier();
    struct ohci_hcca *hcca = (void*)cntl->ohci.regs->hcca;
    for (i=0; i<ARRAY_SIZE(hcca->int_table); i++)
        hcca->int_table[i] = (u32)ed;

    pipe->data = data;
    pipe->count = count;
    pipe->tds = tds;
    pipe->pipe.endp = endp;
    return &pipe->pipe;

err:
    free(pipe);
    free(tds);
    free(data);
    return NULL;
}

int
ohci_poll_intr(struct usb_pipe *pipe, void *data)
{
    ASSERT16();
    if (! CONFIG_USB_OHCI)
        return -1;

    struct ohci_pipe *p = container_of(pipe, struct ohci_pipe, pipe);
    struct ohci_td *tds = GET_FLATPTR(p->tds);
    struct ohci_td *head = (void*)GET_FLATPTR(p->ed.hwHeadP);
    struct ohci_td *tail = (void*)GET_FLATPTR(p->ed.hwTailP);
    int count = GET_FLATPTR(p->count);
    int pos = (tail - tds + 1) % count;
    struct ohci_td *next = &tds[pos];
    if (head == next)
        // No intrs found.
        return -1;
    // XXX - check for errors.

    // Copy data.
    u32 endp = GET_FLATPTR(p->pipe.endp);
    int maxpacket = endp2maxsize(endp);
    void *pipedata = GET_FLATPTR(p->data);
    void *intrdata = pipedata + maxpacket * pos;
    memcpy_far(GET_SEG(SS), data
               , FLATPTR_TO_SEG(intrdata), (void*)FLATPTR_TO_OFFSET(intrdata)
               , maxpacket);

    // Reenable this td.
    SET_FLATPTR(tail->hwINFO, TD_DP_IN | TD_T_TOGGLE | TD_CC);
    intrdata = pipedata + maxpacket * (tail-tds);
    SET_FLATPTR(tail->hwCBP, (u32)intrdata);
    SET_FLATPTR(tail->hwNextTD, (u32)next);
    SET_FLATPTR(tail->hwBE, (u32)intrdata + maxpacket - 1);

    SET_FLATPTR(p->ed.hwTailP, (u32)next);

    return 0;
}
