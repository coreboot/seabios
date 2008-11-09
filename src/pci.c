// PCI config space access functions.
//
// Copyright (C) 2008  Kevin O'Connor <kevin@koconnor.net>
// Copyright (C) 2002  MandrakeSoft S.A.
//
// This file may be distributed under the terms of the GNU GPLv3 license.

#include "pci.h" // MaxBDF
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

#if MODE16
int MaxBDF VISIBLE16;
#endif

// Find the maximum bus number.
void
pci_bus_setup()
{
    dprintf(3, "Scan for max PCI bus\n");

    int max = 0x0100;
    int bdf;
    for (bdf=0; bdf < max; bdf++) {
        u32 v = pci_config_readl(bdf, PCI_VENDOR_ID);
        if (v == 0xffffffff || v == 0x00000000
            || v == 0x0000ffff || v == 0xffff0000)
            // No device present.
            continue;
        v = pci_config_readb(bdf, PCI_HEADER_TYPE);
        v &= 0x7f;
        if (v != PCI_HEADER_TYPE_BRIDGE && v != PCI_HEADER_TYPE_CARDBUS)
            // Not a bridge
            continue;
        v = pci_config_readl(bdf, PCI_PRIMARY_BUS);
        int newmax = (v & 0xff00) + 0x0100;
        if (newmax > max)
            max = newmax;
    }
    SET_VAR(CS, MaxBDF, max);

    dprintf(1, "Found %d PCI buses\n", pci_bdf_to_bus(max));
}

// Search for a device with the specified vendor and device ids.
int
pci_find_device(u16 vendid, u16 devid, int start_bdf)
{
    u32 id = (devid << 16) | vendid;
    int max = GET_VAR(CS, MaxBDF);
    int bdf;
    for (bdf=start_bdf; bdf < max; bdf++) {
        u32 v = pci_config_readl(bdf, PCI_VENDOR_ID);
        if (v != id)
            continue;
        // Found it.
        return bdf;
    }
    return -1;
}

// Search for a device with the specified class id and prog-if.
int
pci_find_classprog(u32 classprog, int start_bdf)
{
    int max = GET_VAR(CS, MaxBDF);
    int bdf;
    for (bdf=start_bdf; bdf < max; bdf++) {
        u32 v = pci_config_readl(bdf, PCI_CLASS_REVISION);
        if ((v>>8) != classprog)
            continue;
        // Found it.
        return bdf;
    }
    return -1;
}

// Search for a device with the specified class id.
int
pci_find_class(u16 classid, int start_bdf)
{
    int max = GET_VAR(CS, MaxBDF);
    int bdf;
    for (bdf=start_bdf; bdf < max; bdf++) {
        u16 v = pci_config_readw(bdf, PCI_CLASS_DEVICE);
        if (v != classid)
            continue;
        // Found it.
        return bdf;
    }
    return -1;
}
