// Initialize PCI devices (on emulators)
//
// Copyright (C) 2008  Kevin O'Connor <kevin@koconnor.net>
// Copyright (C) 2006 Fabrice Bellard
//
// This file may be distributed under the terms of the GNU LGPLv3 license.

#include "util.h" // dprintf
#include "pci.h" // pci_config_readl
#include "biosvar.h" // GET_EBDA
#include "pci_ids.h" // PCI_VENDOR_ID_INTEL
#include "pci_regs.h" // PCI_COMMAND

#define PCI_ROM_SLOT 6
#define PCI_NUM_REGIONS 7

static u32 pci_bios_io_addr;
static u32 pci_bios_mem_addr;
/* host irqs corresponding to PCI irqs A-D */
static u8 pci_irqs[4] = {
    10, 10, 11, 11
};

static void pci_set_io_region_addr(u16 bdf, int region_num, u32 addr)
{
    u32 ofs, old_addr;

    if (region_num == PCI_ROM_SLOT) {
        ofs = PCI_ROM_ADDRESS;
    } else {
        ofs = PCI_BASE_ADDRESS_0 + region_num * 4;
    }

    old_addr = pci_config_readl(bdf, ofs);

    pci_config_writel(bdf, ofs, addr);
    dprintf(1, "region %d: 0x%08x\n", region_num, addr);
}

/* return the global irq number corresponding to a given device irq
   pin. We could also use the bus number to have a more precise
   mapping. */
static int pci_slot_get_pirq(u16 bdf, int irq_num)
{
    int slot_addend = pci_bdf_to_dev(bdf) - 1;
    return (irq_num + slot_addend) & 3;
}

static void pci_bios_init_bridges(u16 bdf)
{
    u16 vendor_id = pci_config_readw(bdf, PCI_VENDOR_ID);
    u16 device_id = pci_config_readw(bdf, PCI_DEVICE_ID);

    if (vendor_id == PCI_VENDOR_ID_INTEL
        && (device_id == PCI_DEVICE_ID_INTEL_82371SB_0
            || device_id == PCI_DEVICE_ID_INTEL_82371AB_0)) {
        int i, irq;
        u8 elcr[2];

        /* PIIX3/PIIX4 PCI to ISA bridge */

        elcr[0] = 0x00;
        elcr[1] = 0x00;
        for (i = 0; i < 4; i++) {
            irq = pci_irqs[i];
            /* set to trigger level */
            elcr[irq >> 3] |= (1 << (irq & 7));
            /* activate irq remapping in PIIX */
            pci_config_writeb(bdf, 0x60 + i, irq);
        }
        outb(elcr[0], 0x4d0);
        outb(elcr[1], 0x4d1);
        dprintf(1, "PIIX3/PIIX4 init: elcr=%02x %02x\n",
                elcr[0], elcr[1]);
    }
}

static void pci_bios_init_device(u16 bdf)
{
    int class;
    u32 *paddr;
    int i, pin, pic_irq, vendor_id, device_id;

    class = pci_config_readw(bdf, PCI_CLASS_DEVICE);
    vendor_id = pci_config_readw(bdf, PCI_VENDOR_ID);
    device_id = pci_config_readw(bdf, PCI_DEVICE_ID);
    dprintf(1, "PCI: bus=%d devfn=0x%02x: vendor_id=0x%04x device_id=0x%04x\n"
            , pci_bdf_to_bus(bdf), pci_bdf_to_devfn(bdf), vendor_id, device_id);
    switch (class) {
    case PCI_CLASS_STORAGE_IDE:
        if (vendor_id == PCI_VENDOR_ID_INTEL
            && (device_id == PCI_DEVICE_ID_INTEL_82371SB_1
                || device_id == PCI_DEVICE_ID_INTEL_82371AB)) {
            /* PIIX3/PIIX4 IDE */
            pci_config_writew(bdf, 0x40, 0x8000); // enable IDE0
            pci_config_writew(bdf, 0x42, 0x8000); // enable IDE1
            goto default_map;
        } else {
            /* IDE: we map it as in ISA mode */
            pci_set_io_region_addr(bdf, 0, PORT_ATA1_CMD_BASE);
            pci_set_io_region_addr(bdf, 1, PORT_ATA1_CTRL_BASE);
            pci_set_io_region_addr(bdf, 2, PORT_ATA2_CMD_BASE);
            pci_set_io_region_addr(bdf, 3, PORT_ATA2_CTRL_BASE);
        }
        break;
    case PCI_CLASS_SYSTEM_PIC:
        /* PIC */
        if (vendor_id == PCI_VENDOR_ID_IBM) {
            /* IBM */
            if (device_id == 0x0046 || device_id == 0xFFFF) {
                /* MPIC & MPIC2 */
                pci_set_io_region_addr(bdf, 0, 0x80800000 + 0x00040000);
            }
        }
        break;
    case 0xff00:
        if (vendor_id == PCI_VENDOR_ID_APPLE &&
            (device_id == 0x0017 || device_id == 0x0022)) {
            /* macio bridge */
            pci_set_io_region_addr(bdf, 0, 0x80800000);
        }
        break;
    default:
    default_map:
        /* default memory mappings */
        for (i = 0; i < PCI_NUM_REGIONS; i++) {
            int ofs;
            if (i == PCI_ROM_SLOT)
                ofs = PCI_ROM_ADDRESS;
            else
                ofs = PCI_BASE_ADDRESS_0 + i * 4;

            u32 old = pci_config_readl(bdf, ofs);
            u32 mask;
            if (i == PCI_ROM_SLOT) {
                mask = PCI_ROM_ADDRESS_MASK;
                pci_config_writel(bdf, ofs, mask);
            } else {
                if (old & PCI_BASE_ADDRESS_SPACE_IO)
                    mask = PCI_BASE_ADDRESS_IO_MASK;
                else
                    mask = PCI_BASE_ADDRESS_MEM_MASK;
                pci_config_writel(bdf, ofs, ~0);
            }
            u32 val = pci_config_readl(bdf, ofs);
            pci_config_writel(bdf, ofs, old);

            if (val != 0) {
                u32 size = (~(val & mask)) + 1;
                if (val & PCI_BASE_ADDRESS_SPACE_IO)
                    paddr = &pci_bios_io_addr;
                else
                    paddr = &pci_bios_mem_addr;
                *paddr = ALIGN(*paddr, size);
                pci_set_io_region_addr(bdf, i, *paddr);
                *paddr += size;
            }
        }
        break;
    }

    /* enable memory mappings */
    pci_config_maskw(bdf, PCI_COMMAND, 0, PCI_COMMAND_IO | PCI_COMMAND_MEMORY);

    /* map the interrupt */
    pin = pci_config_readb(bdf, PCI_INTERRUPT_PIN);
    if (pin != 0) {
        pin = pci_slot_get_pirq(bdf, pin - 1);
        pic_irq = pci_irqs[pin];
        pci_config_writeb(bdf, PCI_INTERRUPT_LINE, pic_irq);
    }

    if (vendor_id == PCI_VENDOR_ID_INTEL
        && device_id == PCI_DEVICE_ID_INTEL_82371AB_3) {
        /* PIIX4 Power Management device (for ACPI) */

        // acpi sci is hardwired to 9
        pci_config_writeb(bdf, PCI_INTERRUPT_LINE, 9);

        pci_config_writel(bdf, 0x40, PORT_ACPI_PM_BASE | 1);
        pci_config_writeb(bdf, 0x80, 0x01); /* enable PM io space */
        pci_config_writel(bdf, 0x90, PORT_SMB_BASE | 1);
        pci_config_writeb(bdf, 0xd2, 0x09); /* enable SMBus io space */
    }
}

void
pci_setup(void)
{
    if (CONFIG_COREBOOT)
        // Already done by coreboot.
        return;

    dprintf(3, "pci setup\n");

    pci_bios_io_addr = 0xc000;
    pci_bios_mem_addr = BUILD_PCIMEM_START;

    int bdf, max;
    foreachpci(bdf, max) {
        pci_bios_init_bridges(bdf);
    }
    foreachpci(bdf, max) {
        pci_bios_init_device(bdf);
    }
}
