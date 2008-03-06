/*
 * pci.c
 * 
 * Copyright (C) 2008  Nguyen Anh Quynh <aquynh@gmail.com>
 * Copyright (C) 2002  MandrakeSoft S.A.
 * 
 * This file may be distributed under the terms of the GNU GPLv3 license.
 */

#include "acpi.h"
#include "hardware.h"
#include "ioport.h"
#include "pci.h"
#include "smm.h"
#include "types.h"
#include "util.h"

u32 pm_io_base, smb_io_base;
int pm_sci_int;
PCIDevice i440_pcidev;

static u32 pci_bios_io_addr  = 0xC000;
static u32 pci_bios_mem_addr = 0xF0000000;
static u32 pci_bios_bigmem_addr;

/* host irqs corresponding to PCI irqs A-D */
static u8 pci_irqs[4] = { 11, 9, 11, 9 };


static void
pci_set_io_region_addr(PCIDevice *d, int region_num, u32 addr)
{
    u16 cmd;
    u32 ofs, old_addr;

    if (region_num == PCI_ROM_SLOT)
        ofs = 0x30;
    else
        ofs = 0x10 + region_num * 4;

    old_addr = pci_config_readl(d, ofs);

    pci_config_writel(d, ofs, addr);
    BX_INFO("region %d: 0x%08x\n", region_num, addr);

    /* enable memory mappings */
    cmd = pci_config_readw(d, PCI_COMMAND);
    if (region_num == PCI_ROM_SLOT)
        cmd |= 2;
    else if (old_addr & PCI_ADDRESS_SPACE_IO)
        cmd |= 1;
    else
        cmd |= 2;
    pci_config_writew(d, PCI_COMMAND, cmd);
}

/* return the global irq number corresponding to a given device irq
   pin. We could also use the bus number to have a more precise
   mapping. */
static int
pci_slot_get_pirq(PCIDevice *pci_dev, int irq_num)
{
    int slot_addend;

    slot_addend = (pci_dev->devfn >> 3) - 1;

    return (irq_num + slot_addend) & 3;
}

static int
find_bios_table_area(void)
{
    unsigned long addr;

    for (addr = 0xf0000; addr < 0x100000; addr += 16) {
        if (*(u32 *)addr == 0xaafb4442) {
            bios_table_cur_addr = addr + 8;
            bios_table_end_addr = bios_table_cur_addr + *(u32 *)(addr + 4);
            BX_INFO("bios_table_addr: 0x%08lx end=0x%08lx\n",
                    bios_table_cur_addr, bios_table_end_addr);

            return 0;
        }
    }

    return -1;
}

static void
bios_shadow_init(PCIDevice *d)
{
    int v;

    if (find_bios_table_area() < 0)
        return;

    /* remap the BIOS to shadow RAM an keep it read/write while we
       are writing tables */
    v = pci_config_readb(d, 0x59);
    v &= 0xcf;
    pci_config_writeb(d, 0x59, v);
    memcpy((void *)BIOS_TMP_STORAGE, (void *)0x000f0000, 0x10000);
    v |= 0x30;
    pci_config_writeb(d, 0x59, v);
    memcpy((void *)0x000f0000, (void *)BIOS_TMP_STORAGE, 0x10000);

    i440_pcidev = *d;
}

void bios_lock_shadow_ram(void)
{
    PCIDevice *d = &i440_pcidev;
    int v;

    wbinvd();
    v = pci_config_readb(d, 0x59);
    v = (v & 0x0f) | (0x10);
    pci_config_writeb(d, 0x59, v);
}

static void pci_bios_init_bridges(PCIDevice *d)
{
    u16 vendor_id, device_id;

    vendor_id = pci_config_readw(d, PCI_VENDOR_ID);
    device_id = pci_config_readw(d, PCI_DEVICE_ID);

    if (vendor_id == PCI_VENDOR_ID_INTEL && device_id == PCI_DEVICE_ID_INTEL_82371SB_0) {
        int i, irq;
        u8 elcr[2];

        /* PIIX3 bridge */
        elcr[0] = 0x00;
        elcr[1] = 0x00;
        for (i = 0; i < 4; i++) {
            irq = pci_irqs[i];
            /* set to trigger level */
            elcr[irq >> 3] |= (1 << (irq & 7));
            /* activate irq remapping in PIIX */
            pci_config_writeb(d, 0x60 + i, irq);
        }

        outb(elcr[0], 0x4d0);
        outb(elcr[1], 0x4d1);
        BX_INFO("PIIX3 init: elcr=%02x %02x\n", elcr[0], elcr[1]);
    }
	else if (vendor_id == PCI_VENDOR_ID_INTEL && device_id == PCI_DEVICE_ID_INTEL_82441) {
        /* i440 PCI bridge */
        bios_shadow_init(d);
    }
}

static void
pci_bios_init_device(PCIDevice *d)
{
    int class;
    u32 *paddr;
    int i, pin, pic_irq, vendor_id, device_id;

    class = pci_config_readw(d, PCI_CLASS_DEVICE);
    vendor_id = pci_config_readw(d, PCI_VENDOR_ID);
    device_id = pci_config_readw(d, PCI_DEVICE_ID);
    BX_INFO("PCI: bus=%d devfn=0x%02x: vendor_id=0x%04x device_id=0x%04x\n",
            d->bus, d->devfn, vendor_id, device_id);
    switch(class) {
    case 0x0101:
        if (vendor_id == PCI_VENDOR_ID_INTEL && device_id == PCI_DEVICE_ID_INTEL_82371SB_1) {
            /* PIIX3 IDE */
            pci_config_writew(d, 0x40, 0x8000); // enable IDE0
            pci_config_writew(d, 0x42, 0x8000); // enable IDE1
            goto default_map;
        } else {
            /* IDE: we map it as in ISA mode */
            pci_set_io_region_addr(d, 0, 0x1f0);
            pci_set_io_region_addr(d, 1, 0x3f4);
            pci_set_io_region_addr(d, 2, 0x170);
            pci_set_io_region_addr(d, 3, 0x374);
        }
        break;
    case 0x0300:
        if (vendor_id != 0x1234)
            goto default_map;
        /* VGA: map frame buffer to default Bochs VBE address */
        pci_set_io_region_addr(d, 0, 0xE0000000);
        break;
    case 0x0800:
        /* PIC */
        if (vendor_id == PCI_VENDOR_ID_IBM) {
            /* IBM */
            if (device_id == 0x0046 || device_id == 0xFFFF) {
                /* MPIC & MPIC2 */
                pci_set_io_region_addr(d, 0, 0x80800000 + 0x00040000);
            }
        }
        break;
    case 0xff00:
        if (vendor_id == PCI_VENDOR_ID_APPLE &&
            (device_id == 0x0017 || device_id == 0x0022)) {
            /* macio bridge */
            pci_set_io_region_addr(d, 0, 0x80800000);
        }
        break;
    default:
    default_map:
        /* default memory mappings */
        for (i = 0; i < PCI_NUM_REGIONS; i++) {
            int ofs;
            u32 val, size ;

            if (i == PCI_ROM_SLOT)
                ofs = 0x30;
            else
                ofs = 0x10 + i * 4;
            pci_config_writel(d, ofs, 0xffffffff);
            val = pci_config_readl(d, ofs);
            if (val != 0) {
                size = (~(val & ~0xf)) + 1;
                if (val & PCI_ADDRESS_SPACE_IO)
                    paddr = &pci_bios_io_addr;
                else if (size >= 0x04000000)
                    paddr = &pci_bios_bigmem_addr;
                else
                    paddr = &pci_bios_mem_addr;
                *paddr = (*paddr + size - 1) & ~(size - 1);
                pci_set_io_region_addr(d, i, *paddr);
                *paddr += size;
            }
        }
        break;
    }

    /* map the interrupt */
    pin = pci_config_readb(d, PCI_INTERRUPT_PIN);
    if (pin != 0) {
        pin = pci_slot_get_pirq(d, pin - 1);
        pic_irq = pci_irqs[pin];
        pci_config_writeb(d, PCI_INTERRUPT_LINE, pic_irq);
    }

    if (vendor_id == PCI_VENDOR_ID_INTEL && device_id == PCI_DEVICE_ID_INTEL_82371AB_3) {
        /* PIIX4 Power Management device (for ACPI) */
        pm_io_base = PM_IO_BASE;
        pci_config_writel(d, 0x40, pm_io_base | 1);
        pci_config_writeb(d, 0x80, 0x01); /* enable PM io space */
        smb_io_base = SMB_IO_BASE;
        pci_config_writel(d, 0x90, smb_io_base | 1);
        pci_config_writeb(d, 0xd2, 0x09); /* enable SMBus io space */
        pm_sci_int = pci_config_readb(d, PCI_INTERRUPT_LINE);
#ifdef CONFIG_SMM
        smm_init(d);
#endif
        acpi_enabled = 1;
    }
}

static void
pci_for_each_device(void (*init_func)(PCIDevice *d))
{
    PCIDevice d1, *d = &d1;
    int bus, devfn;
    u16 vendor_id, device_id;

    for (bus = 0; bus < 1; bus++) {
        for (devfn = 0; devfn < 256; devfn++) {
            d->bus = bus;
            d->devfn = devfn;
            vendor_id = pci_config_readw(d, PCI_VENDOR_ID);
            device_id = pci_config_readw(d, PCI_DEVICE_ID);
            if (vendor_id != 0xffff || device_id != 0xffff)
                init_func(d);
        }
    }
}

void
pci_bios_init(void)
{
    pci_bios_bigmem_addr = ram_size;

    if (pci_bios_bigmem_addr < 0x90000000)
        pci_bios_bigmem_addr = 0x90000000;

    pci_for_each_device(pci_bios_init_bridges);

    pci_for_each_device(pci_bios_init_device);
}
