#include "pci.h" // PCIDevice
#include "ioport.h" // outl

void pci_config_writel(PCIDevice *d, u32 addr, u32 val)
{
    outl(0x80000000 | (d->bus << 16) | (d->devfn << 8) | (addr & 0xfc), 0xcf8);
    outl(val, 0xcfc);
}

void pci_config_writew(PCIDevice *d, u32 addr, u16 val)
{
    outl(0x80000000 | (d->bus << 16) | (d->devfn << 8) | (addr & 0xfc), 0xcf8);
    outw(val, 0xcfc + (addr & 2));
}

void pci_config_writeb(PCIDevice *d, u32 addr, u8 val)
{
    outl(0x80000000 | (d->bus << 16) | (d->devfn << 8) | (addr & 0xfc), 0xcf8);
    outb(val, 0xcfc + (addr & 3));
}

u32 pci_config_readl(PCIDevice *d, u32 addr)
{
    outl(0x80000000 | (d->bus << 16) | (d->devfn << 8) | (addr & 0xfc), 0xcf8);
    return inl(0xcfc);
}

u16 pci_config_readw(PCIDevice *d, u32 addr)
{
    outl(0x80000000 | (d->bus << 16) | (d->devfn << 8) | (addr & 0xfc), 0xcf8);
    return inw(0xcfc + (addr & 2));
}

u8 pci_config_readb(PCIDevice *d, u32 addr)
{
    outl(0x80000000 | (d->bus << 16) | (d->devfn << 8) | (addr & 0xfc), 0xcf8);
    return inb(0xcfc + (addr & 3));
}
