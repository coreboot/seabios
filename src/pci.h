#ifndef __PCI_H
#define __PCI_H

#include "types.h" // u32

typedef struct PCIDevice {
    int bus;
    int devfn;
} PCIDevice;

void pci_config_writel(PCIDevice *d, u32 addr, u32 val);
void pci_config_writew(PCIDevice *d, u32 addr, u16 val);
void pci_config_writeb(PCIDevice *d, u32 addr, u8 val);
u32 pci_config_readl(PCIDevice *d, u32 addr);
u16 pci_config_readw(PCIDevice *d, u32 addr);
u8 pci_config_readb(PCIDevice *d, u32 addr);

#endif
