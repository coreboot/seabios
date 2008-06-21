// PCI config space access functions.
//
// Copyright (C) 2008  Kevin O'Connor <kevin@koconnor.net>
// Copyright (C) 2002  MandrakeSoft S.A.
//
// This file may be distributed under the terms of the GNU GPLv3 license.

#include "pci.h" // PCIDevice
#include "ioport.h" // outl
#include "util.h" // dprintf

#define MAX_BUS 1

void pci_config_writel(PCIDevice d, u32 addr, u32 val)
{
    outl(0x80000000 | (d.bus << 16) | (d.devfn << 8) | (addr & 0xfc)
         , PORT_PCI_CMD);
    outl(val, PORT_PCI_DATA);
}

void pci_config_writew(PCIDevice d, u32 addr, u16 val)
{
    outl(0x80000000 | (d.bus << 16) | (d.devfn << 8) | (addr & 0xfc)
         , PORT_PCI_CMD);
    outw(val, PORT_PCI_DATA + (addr & 2));
}

void pci_config_writeb(PCIDevice d, u32 addr, u8 val)
{
    outl(0x80000000 | (d.bus << 16) | (d.devfn << 8) | (addr & 0xfc)
         , PORT_PCI_CMD);
    outb(val, PORT_PCI_DATA + (addr & 3));
}

u32 pci_config_readl(PCIDevice d, u32 addr)
{
    outl(0x80000000 | (d.bus << 16) | (d.devfn << 8) | (addr & 0xfc)
         , PORT_PCI_CMD);
    return inl(PORT_PCI_DATA);
}

u16 pci_config_readw(PCIDevice d, u32 addr)
{
    outl(0x80000000 | (d.bus << 16) | (d.devfn << 8) | (addr & 0xfc)
         , PORT_PCI_CMD);
    return inw(PORT_PCI_DATA + (addr & 2));
}

u8 pci_config_readb(PCIDevice d, u32 addr)
{
    outl(0x80000000 | (d.bus << 16) | (d.devfn << 8) | (addr & 0xfc)
         , PORT_PCI_CMD);
    return inb(PORT_PCI_DATA + (addr & 3));
}

int
pci_find_device(u16 vendid, u16 devid, int index, PCIDevice *dev)
{
    int devfn, bus;
    u32 id = (devid << 16) | vendid;
    for (bus=0; bus < MAX_BUS; bus++) {
        for (devfn=0; devfn<0x100; devfn++) {
            PCIDevice d = pci_bd(bus, devfn);
            u32 v = pci_config_readl(d, 0x00);
            if (v != id)
                continue;
            if (index) {
                index--;
                continue;
            }
            // Found it.
            *dev = d;
            return 0;
        }
    }
    return -1;
}

int
pci_find_class(u32 classid, int index, PCIDevice *dev)
{
    int devfn, bus;
    u32 id = classid << 8;
    for (bus=0; bus < MAX_BUS; bus++) {
        for (devfn=0; devfn<0x100; devfn++) {
            PCIDevice d = pci_bd(bus, devfn);
            u32 v = pci_config_readl(d, 0x08);
            if (v != id)
                continue;
            if (index) {
                index--;
                continue;
            }
            // Found it.
            *dev = d;
            return 0;
        }
    }
    return -1;
}
