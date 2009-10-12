// Code for handling OHCI USB controllers.
//
// Copyright (C) 2009  Kevin O'Connor <kevin@koconnor.net>
//
// This file may be distributed under the terms of the GNU LGPLv3 license.

#include "util.h" // dprintf
#include "pci.h" // pci_bdf_to_bus
#include "config.h" // CONFIG_*
#include "ioport.h" // outw
#include "usb-ohci.h" // USBLEGSUP
#include "pci_regs.h" // PCI_BASE_ADDRESS_4
#include "usb.h" // struct usb_s
#include "farptr.h" // GET_FLATPTR
#include "biosvar.h" // GET_GLOBAL

static void
reset_ohci(struct usb_s *cntl)
{
}

static void
configure_ohci(struct usb_s *cntl)
{
    // XXX - check for SMM control?

    writel(&cntl->ohci.regs->intrdisable, OHCI_INTR_MIE);

    struct ohci_hcca *hcca = memalign_low(256, sizeof(*hcca));
    if (!hcca) {
        dprintf(1, "No ram for ohci init\n");
        return;
    }

    
}

static void
start_ohci(struct usb_s *cntl)
{
}

// Find any devices connected to the root hub.
static int
check_ohci_ports(struct usb_s *cntl)
{
    return 0;
}

int
ohci_init(struct usb_s *cntl)
{
    if (! CONFIG_USB_OHCI)
        return 0;

    cntl->type = USB_TYPE_OHCI;
    u32 baseaddr = pci_config_readl(cntl->bdf, PCI_BASE_ADDRESS_0);
    cntl->ohci.regs = (void*)(baseaddr & PCI_BASE_ADDRESS_MEM_MASK);

    dprintf(3, "OHCI init on dev %02x:%02x.%x (regs=%p)\n"
            , pci_bdf_to_bus(cntl->bdf), pci_bdf_to_dev(cntl->bdf)
            , pci_bdf_to_fn(cntl->bdf), cntl->ohci.regs);

    // Enable bus mastering and memory access.
    pci_config_maskw(cntl->bdf, PCI_COMMAND
                     , 0, PCI_COMMAND_MASTER|PCI_COMMAND_MEMORY);

    reset_ohci(cntl);
    configure_ohci(cntl);
    start_ohci(cntl);

    int count = check_ohci_ports(cntl);
    if (! count) {
        // XXX - no devices; free data structures.
        return 0;
    }

    return 0;
}

int
ohci_control(u32 endp, int dir, const void *cmd, int cmdsize
             , void *data, int datasize)
{
    if (! CONFIG_USB_OHCI)
        return 0;

    dprintf(5, "ohci_control %x\n", endp);
    return 0;
}

struct usb_pipe *
ohci_alloc_intr_pipe(u32 endp, int period)
{
    if (! CONFIG_USB_OHCI)
        return NULL;

    dprintf(7, "ohci_alloc_intr_pipe %x %d\n", endp, period);
    return NULL;
}

int
ohci_poll_intr(void *pipe, void *data)
{
    ASSERT16();
    if (! CONFIG_USB_OHCI)
        return -1;
    return -1;
}
