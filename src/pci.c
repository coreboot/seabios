// PCI config space access functions.
//
// Copyright (C) 2008  Kevin O'Connor <kevin@koconnor.net>
// Copyright (C) 2002  MandrakeSoft S.A.
//
// This file may be distributed under the terms of the GNU GPLv3 license.

#include "pci.h" // pci_config_writel
#include "ioport.h" // outl
#include "util.h" // dprintf
#include "config.h" // CONFIG_*
#include "pci_regs.h" // PCI_VENDOR_ID
#include "farptr.h" // SET_VAR

void pci_config_writel(u16 bdf, u32 addr, u32 val)
{
    outl(0x80000000 | (bdf << 8) | (addr & 0xfc), PORT_PCI_CMD);
    outl(val, PORT_PCI_DATA);
}

void pci_config_writew(u16 bdf, u32 addr, u16 val)
{
    outl(0x80000000 | (bdf << 8) | (addr & 0xfc), PORT_PCI_CMD);
    outw(val, PORT_PCI_DATA + (addr & 2));
}

void pci_config_writeb(u16 bdf, u32 addr, u8 val)
{
    outl(0x80000000 | (bdf << 8) | (addr & 0xfc), PORT_PCI_CMD);
    outb(val, PORT_PCI_DATA + (addr & 3));
}

u32 pci_config_readl(u16 bdf, u32 addr)
{
    outl(0x80000000 | (bdf << 8) | (addr & 0xfc), PORT_PCI_CMD);
    return inl(PORT_PCI_DATA);
}

u16 pci_config_readw(u16 bdf, u32 addr)
{
    outl(0x80000000 | (bdf << 8) | (addr & 0xfc), PORT_PCI_CMD);
    return inw(PORT_PCI_DATA + (addr & 2));
}

u8 pci_config_readb(u16 bdf, u32 addr)
{
    outl(0x80000000 | (bdf << 8) | (addr & 0xfc), PORT_PCI_CMD);
    return inb(PORT_PCI_DATA + (addr & 3));
}

int
pci_next(int bdf, int *pmax)
{
    if (pci_bdf_to_fn(bdf) == 1
        && (pci_config_readb(bdf-1, PCI_HEADER_TYPE) & 0x80) == 0)
        // Last found device wasn't a multi-function device - skip to
        // the next device.
        bdf += 7;

    int max = *pmax;
    for (;;) {
        if (bdf >= max)
            return -1;

        u16 v = pci_config_readw(bdf, PCI_VENDOR_ID);
        if (v != 0x0000 && v != 0xffff)
            // Device is present.
            break;

        if (pci_bdf_to_fn(bdf) == 0)
            bdf += 8;
        else
            bdf += 1;
    }

    // Check if found device is a bridge.
    u32 v = pci_config_readb(bdf, PCI_HEADER_TYPE);
    v &= 0x7f;
    if (v == PCI_HEADER_TYPE_BRIDGE || v == PCI_HEADER_TYPE_CARDBUS) {
        v = pci_config_readl(bdf, PCI_PRIMARY_BUS);
        int newmax = (v & 0xff00) + 0x0100;
        if (newmax > max)
            *pmax = newmax;
    }

    return bdf;
}

// Search for a device with the specified vendor and device ids.
int
pci_find_device(u16 vendid, u16 devid)
{
    u32 id = (devid << 16) | vendid;
    int bdf, max;
    foreachpci(bdf, max) {
        u32 v = pci_config_readl(bdf, PCI_VENDOR_ID);
        if (v != id)
            continue;
        // Found it.
        return bdf;
    }
    return -1;
}

// Search for a device with the specified class id.
int
pci_find_class(u16 classid)
{
    int bdf, max;
    foreachpci(bdf, max) {
        u16 v = pci_config_readw(bdf, PCI_CLASS_DEVICE);
        if (v != classid)
            continue;
        // Found it.
        return bdf;
    }
    return -1;
}
