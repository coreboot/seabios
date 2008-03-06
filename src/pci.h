/*
 * pci.h
 * 
 * Copyright (C) 2008  Nguyen Anh Quynh <aquynh@gmail.com>
 * Copyright (C) 2002  MandrakeSoft S.A.
 * 
 * This file may be distributed under the terms of the GNU GPLv3 license.
 */

#ifndef __PCI_H
#define __PCI_H

#include "ioport.h" // outl

/* PCI init */
#define PCI_ADDRESS_SPACE_IO		0x01

#define PCI_ROM_SLOT    6
#define PCI_NUM_REGIONS 7

#define PCI_VENDOR_ID		0x00	/* 16 bits */
#define PCI_DEVICE_ID		0x02	/* 16 bits */
#define PCI_COMMAND			0x04	/* 16 bits */
#define PCI_COMMAND_IO		0x1		/* Enable response in I/O space */
#define PCI_COMMAND_MEMORY	0x2		/* Enable response in Memory space */
#define PCI_CLASS_DEVICE	0x0a    /* Device class */
#define PCI_INTERRUPT_LINE	0x3c	/* 8 bits */
#define PCI_INTERRUPT_PIN	0x3d	/* 8 bits */

#define PCI_VENDOR_ID_INTEL             0x8086
#define PCI_DEVICE_ID_INTEL_82441       0x1237
#define PCI_DEVICE_ID_INTEL_82371SB_0   0x7000
#define PCI_DEVICE_ID_INTEL_82371SB_1   0x7010
#define PCI_DEVICE_ID_INTEL_82371AB_3   0x7113

#define PCI_VENDOR_ID_IBM               0x1014
#define PCI_VENDOR_ID_APPLE             0x106b

/* 64 KB used to copy the BIOS to shadow RAM */
#define BIOS_TMP_STORAGE  0x00030000

#define SMB_IO_BASE         0xb100
#define PM_IO_BASE          0xb000

#define wbinvd() asm volatile("wbinvd")

typedef struct PCIDevice {
    int bus;
    int devfn;
} PCIDevice;

static inline void
pci_config_writel(PCIDevice *d, u32 addr, u32 val)
{
    outl(0x80000000 | (d->bus << 16) | (d->devfn << 8) | (addr & 0xfc), 0xcf8);
    outl(val, 0xcfc);
}

static inline void
pci_config_writew(PCIDevice *d, u32 addr, u32 val)
{
    outl(0x80000000 | (d->bus << 16) | (d->devfn << 8) | (addr & 0xfc), 0xcf8);
    outw(val, 0xcfc + (addr & 2));
}

static inline void
pci_config_writeb(PCIDevice *d, u32 addr, u32 val)
{
    outl(0x80000000 | (d->bus << 16) | (d->devfn << 8) | (addr & 0xfc), 0xcf8);
    outb(val, 0xcfc + (addr & 3));
}

static inline u32
pci_config_readl(PCIDevice *d, u32 addr)
{
    outl(0x80000000 | (d->bus << 16) | (d->devfn << 8) | (addr & 0xfc), 0xcf8);

    return inl(0xcfc);
}

static inline u32
pci_config_readw(PCIDevice *d, u32 addr)
{
    outl(0x80000000 | (d->bus << 16) | (d->devfn << 8) | (addr & 0xfc), 0xcf8);
    return inw(0xcfc + (addr & 2));
}

static inline u32
pci_config_readb(PCIDevice *d, u32 addr)
{
    outl(0x80000000 | (d->bus << 16) | (d->devfn << 8) | (addr & 0xfc), 0xcf8);

    return inb(0xcfc + (addr & 3));
}

void pci_bios_init(void);

void bios_lock_shadow_ram(void);

extern PCIDevice i440_pcidev;

#endif
