// Initialize PCI devices (on emulators)
//
// Copyright (C) 2008  Kevin O'Connor <kevin@koconnor.net>
// Copyright (C) 2006 Fabrice Bellard
//
// This file may be distributed under the terms of the GNU GPLv3 license.

#include "util.h" // dprintf
#include "pci.h" // PCIDevice
#include "biosvar.h" // GET_EBDA

#define PCI_ADDRESS_SPACE_MEM		0x00
#define PCI_ADDRESS_SPACE_IO		0x01
#define PCI_ADDRESS_SPACE_MEM_PREFETCH	0x08

#define PCI_ROM_SLOT 6
#define PCI_NUM_REGIONS 7

#define PCI_DEVICES_MAX 64

static u32 pci_bios_io_addr;
static u32 pci_bios_mem_addr;
static u32 pci_bios_bigmem_addr;
/* host irqs corresponding to PCI irqs A-D */
static u8 pci_irqs[4] = { 11, 9, 11, 9 };

static void pci_set_io_region_addr(PCIDevice d, int region_num, u32 addr)
{
    u16 cmd;
    u32 ofs, old_addr;

    if ( region_num == PCI_ROM_SLOT ) {
        ofs = 0x30;
    }else{
        ofs = 0x10 + region_num * 4;
    }

    old_addr = pci_config_readl(d, ofs);

    pci_config_writel(d, ofs, addr);
    dprintf(1, "region %d: 0x%08x\n", region_num, addr);

    /* enable memory mappings */
    cmd = pci_config_readw(d, PCI_COMMAND);
    if ( region_num == PCI_ROM_SLOT )
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
static int pci_slot_get_pirq(PCIDevice pci_dev, int irq_num)
{
    int slot_addend;
    slot_addend = (pci_dev.devfn >> 3) - 1;
    return (irq_num + slot_addend) & 3;
}

static void pci_bios_init_bridges(PCIDevice d)
{
    u16 vendor_id, device_id;

    vendor_id = pci_config_readw(d, PCI_VENDOR_ID);
    device_id = pci_config_readw(d, PCI_DEVICE_ID);

    if (vendor_id == 0x8086 && device_id == 0x7000) {
        int i, irq;
        u8 elcr[2];

        /* PIIX3 bridge */

        elcr[0] = 0x00;
        elcr[1] = 0x00;
        for(i = 0; i < 4; i++) {
            irq = pci_irqs[i];
            /* set to trigger level */
            elcr[irq >> 3] |= (1 << (irq & 7));
            /* activate irq remapping in PIIX */
            pci_config_writeb(d, 0x60 + i, irq);
        }
        outb(elcr[0], 0x4d0);
        outb(elcr[1], 0x4d1);
        dprintf(1, "PIIX3 init: elcr=%02x %02x\n",
                elcr[0], elcr[1]);
    }
}

static void pci_bios_init_device(PCIDevice d)
{
    int class;
    u32 *paddr;
    int i, pin, pic_irq, vendor_id, device_id;

    class = pci_config_readw(d, PCI_CLASS_DEVICE);
    vendor_id = pci_config_readw(d, PCI_VENDOR_ID);
    device_id = pci_config_readw(d, PCI_DEVICE_ID);
    dprintf(1, "PCI: bus=%d devfn=0x%02x: vendor_id=0x%04x device_id=0x%04x\n",
            d.bus, d.devfn, vendor_id, device_id);
    switch(class) {
    case 0x0101:
        if (vendor_id == 0x8086 && device_id == 0x7010) {
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
        if (vendor_id == 0x1014) {
            /* IBM */
            if (device_id == 0x0046 || device_id == 0xFFFF) {
                /* MPIC & MPIC2 */
                pci_set_io_region_addr(d, 0, 0x80800000 + 0x00040000);
            }
        }
        break;
    case 0xff00:
        if (vendor_id == 0x0106b &&
            (device_id == 0x0017 || device_id == 0x0022)) {
            /* macio bridge */
            pci_set_io_region_addr(d, 0, 0x80800000);
        }
        break;
    default:
    default_map:
        /* default memory mappings */
        for(i = 0; i < PCI_NUM_REGIONS; i++) {
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

    if (vendor_id == 0x8086 && device_id == 0x7113) {
        /* PIIX4 Power Management device (for ACPI) */
        u32 pm_io_base = BUILD_PM_IO_BASE;
        pci_config_writel(d, 0x40, pm_io_base | 1);
        pci_config_writeb(d, 0x80, 0x01); /* enable PM io space */
        u32 smb_io_base = BUILD_SMB_IO_BASE;
        pci_config_writel(d, 0x90, smb_io_base | 1);
        pci_config_writeb(d, 0xd2, 0x09); /* enable SMBus io space */
    }
}

static void pci_for_each_device(void (*init_func)(PCIDevice d))
{
    int bus, devfn;
    u16 vendor_id, device_id;

    for(bus = 0; bus < 1; bus++) {
        for(devfn = 0; devfn < 256; devfn++) {
            PCIDevice d = pci_bd(bus, devfn);
            vendor_id = pci_config_readw(d, PCI_VENDOR_ID);
            device_id = pci_config_readw(d, PCI_DEVICE_ID);
            if (vendor_id != 0xffff || device_id != 0xffff) {
                init_func(d);
            }
        }
    }
}

void
pci_bios_setup(void)
{
    if (CONFIG_COREBOOT)
        // Already done by coreboot.
        return;

    pci_bios_io_addr = 0xc000;
    pci_bios_mem_addr = 0xf0000000;
    pci_bios_bigmem_addr = GET_EBDA(ram_size);
    if (pci_bios_bigmem_addr < 0x90000000)
        pci_bios_bigmem_addr = 0x90000000;

    pci_for_each_device(pci_bios_init_bridges);

    pci_for_each_device(pci_bios_init_device);
}
