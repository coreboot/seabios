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
#include "dev-i440fx.h"

#define PCI_ROM_SLOT 6
#define PCI_NUM_REGIONS 7

static void pci_bios_init_device_in_bus(int bus);

static struct pci_region pci_bios_io_region;
static struct pci_region pci_bios_mem_region;
static struct pci_region pci_bios_prefmem_region;

/* host irqs corresponding to PCI irqs A-D */
const u8 pci_irqs[4] = {
    10, 10, 11, 11
};

static u32 pci_bar(u16 bdf, int region_num)
{
    if (region_num != PCI_ROM_SLOT) {
        return PCI_BASE_ADDRESS_0 + region_num * 4;
    }

#define PCI_HEADER_TYPE_MULTI_FUNCTION 0x80
    u8 type = pci_config_readb(bdf, PCI_HEADER_TYPE);
    type &= ~PCI_HEADER_TYPE_MULTI_FUNCTION;
    return type == PCI_HEADER_TYPE_BRIDGE ? PCI_ROM_ADDRESS1 : PCI_ROM_ADDRESS;
}

static void pci_set_io_region_addr(u16 bdf, int region_num, u32 addr)
{
    u32 ofs;

    ofs = pci_bar(bdf, region_num);

    pci_config_writel(bdf, ofs, addr);
    dprintf(1, "region %d: 0x%08x\n", region_num, addr);
}

/*
 * return value
 *      0:     32bit BAR
 *      non 0: 64bit BAR
 */
static int pci_bios_allocate_region(u16 bdf, int region_num)
{
    struct pci_region *r;
    u32 ofs = pci_bar(bdf, region_num);

    u32 old = pci_config_readl(bdf, ofs);
    u32 mask;
    if (region_num == PCI_ROM_SLOT) {
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

    u32 size = (~(val & mask)) + 1;
    if (val != 0) {
        const char *type;
        const char *msg;
        if (val & PCI_BASE_ADDRESS_SPACE_IO) {
            r = &pci_bios_io_region;
            type = "io";
            msg = "";
        } else if ((val & PCI_BASE_ADDRESS_MEM_PREFETCH) &&
                   /* keep behaviour on bus = 0 */
                   pci_bdf_to_bus(bdf) != 0 &&
                   /* If pci_bios_prefmem_addr == 0, keep old behaviour */
                   pci_region_addr(&pci_bios_prefmem_region) != 0) {
            r = &pci_bios_prefmem_region;
            type = "prefmem";
            msg = "decrease BUILD_PCIMEM_SIZE and recompile. size %x";
        } else {
            r = &pci_bios_mem_region;
            type = "mem";
            msg = "increase BUILD_PCIMEM_SIZE and recompile.";
        }
        u32 addr = pci_region_alloc(r, size);
        if (addr > 0) {
            pci_set_io_region_addr(bdf, region_num, addr);
        } else {
            size = 0;
            dprintf(1,
                    "%s region of (bdf 0x%x bar %d) can't be mapped. "
                    "%s size %x\n",
                    type, bdf, region_num, msg, pci_region_size(r));
        }
    }

    int is_64bit = !(val & PCI_BASE_ADDRESS_SPACE_IO) &&
        (val & PCI_BASE_ADDRESS_MEM_TYPE_MASK) == PCI_BASE_ADDRESS_MEM_TYPE_64;
    if (is_64bit && size > 0) {
        pci_config_writel(bdf, ofs + 4, 0);
    }
    return is_64bit;
}

void pci_bios_allocate_regions(u16 bdf, void *arg)
{
    int i;
    for (i = 0; i < PCI_NUM_REGIONS; i++) {
        int is_64bit = pci_bios_allocate_region(bdf, i);
        if (is_64bit){
            i++;
        }
    }
}

/* return the global irq number corresponding to a given device irq
   pin. We could also use the bus number to have a more precise
   mapping. */
static int pci_slot_get_pirq(u16 bdf, int irq_num)
{
    int slot_addend = pci_bdf_to_dev(bdf) - 1;
    return (irq_num + slot_addend) & 3;
}

static const struct pci_device_id pci_isa_bridge_tbl[] = {
    /* PIIX3/PIIX4 PCI to ISA bridge */
    PCI_DEVICE(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_82371SB_0,
               piix_isa_bridge_init),
    PCI_DEVICE(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_82371AB_0,
               piix_isa_bridge_init),

    PCI_DEVICE_END
};

#define PCI_IO_ALIGN            4096
#define PCI_IO_SHIFT            8
#define PCI_MEMORY_ALIGN        (1UL << 20)
#define PCI_MEMORY_SHIFT        16
#define PCI_PREF_MEMORY_ALIGN   (1UL << 20)
#define PCI_PREF_MEMORY_SHIFT   16

static void pci_bios_init_device_bridge(u16 bdf, void *arg)
{
    pci_bios_allocate_region(bdf, 0);
    pci_bios_allocate_region(bdf, 1);
    pci_bios_allocate_region(bdf, PCI_ROM_SLOT);

    u32 io_old = pci_region_addr(&pci_bios_io_region);
    u32 mem_old = pci_region_addr(&pci_bios_mem_region);
    u32 prefmem_old = pci_region_addr(&pci_bios_prefmem_region);

    /* IO BASE is assumed to be 16 bit */
    if (pci_region_align(&pci_bios_io_region, PCI_IO_ALIGN) == 0) {
        pci_region_disable(&pci_bios_io_region);
    }
    if (pci_region_align(&pci_bios_mem_region, PCI_MEMORY_ALIGN) == 0) {
        pci_region_disable(&pci_bios_mem_region);
    }
    if (pci_region_align(&pci_bios_prefmem_region,
                         PCI_PREF_MEMORY_ALIGN) == 0) {
        pci_region_disable(&pci_bios_prefmem_region);
    }

    u32 io_base = pci_region_addr(&pci_bios_io_region);
    u32 mem_base = pci_region_addr(&pci_bios_mem_region);
    u32 prefmem_base = pci_region_addr(&pci_bios_prefmem_region);

    u8 secbus = pci_config_readb(bdf, PCI_SECONDARY_BUS);
    if (secbus > 0) {
        pci_bios_init_device_in_bus(secbus);
    }

    u32 io_end = pci_region_align(&pci_bios_io_region, PCI_IO_ALIGN);
    if (io_end == 0) {
        pci_region_revert(&pci_bios_io_region, io_old);
        io_base = 0xffff;
        io_end = 1;
    }
    pci_config_writeb(bdf, PCI_IO_BASE, io_base >> PCI_IO_SHIFT);
    pci_config_writew(bdf, PCI_IO_BASE_UPPER16, 0);
    pci_config_writeb(bdf, PCI_IO_LIMIT, (io_end - 1) >> PCI_IO_SHIFT);
    pci_config_writew(bdf, PCI_IO_LIMIT_UPPER16, 0);

    u32 mem_end = pci_region_align(&pci_bios_mem_region, PCI_MEMORY_ALIGN);
    if (mem_end == 0) {
        pci_region_revert(&pci_bios_mem_region, mem_old);
        mem_base = 0xffffffff;
        mem_end = 1;
    }
    pci_config_writew(bdf, PCI_MEMORY_BASE, mem_base >> PCI_MEMORY_SHIFT);
    pci_config_writew(bdf, PCI_MEMORY_LIMIT, (mem_end -1) >> PCI_MEMORY_SHIFT);

    u32 prefmem_end = pci_region_align(&pci_bios_prefmem_region,
                                       PCI_PREF_MEMORY_ALIGN);
    if (prefmem_end == 0) {
        pci_region_revert(&pci_bios_prefmem_region, prefmem_old);
        prefmem_base = 0xffffffff;
        prefmem_end = 1;
    }
    pci_config_writew(bdf, PCI_PREF_MEMORY_BASE,
                      prefmem_base >> PCI_PREF_MEMORY_SHIFT);
    pci_config_writew(bdf, PCI_PREF_MEMORY_LIMIT,
                      (prefmem_end - 1) >> PCI_PREF_MEMORY_SHIFT);
    pci_config_writel(bdf, PCI_PREF_BASE_UPPER32, 0);
    pci_config_writel(bdf, PCI_PREF_LIMIT_UPPER32, 0);

    dprintf(1, "PCI: br io   = [0x%x, 0x%x)\n", io_base, io_end);
    dprintf(1, "PCI: br mem  = [0x%x, 0x%x)\n", mem_base, mem_end);
    dprintf(1, "PCI: br pref = [0x%x, 0x%x)\n", prefmem_base, prefmem_end);

    u16 cmd = pci_config_readw(bdf, PCI_COMMAND);
    cmd &= ~PCI_COMMAND_IO;
    if (io_end > io_base) {
        cmd |= PCI_COMMAND_IO;
    }
    cmd &= ~PCI_COMMAND_MEMORY;
    if (mem_end > mem_base || prefmem_end > prefmem_base) {
        cmd |= PCI_COMMAND_MEMORY;
    }
    cmd |= PCI_COMMAND_MASTER;
    pci_config_writew(bdf, PCI_COMMAND, cmd);

    pci_config_maskw(bdf, PCI_BRIDGE_CONTROL, 0, PCI_BRIDGE_CTL_SERR);
}

static void storage_ide_init(u16 bdf, void *arg)
{
    /* IDE: we map it as in ISA mode */
    pci_set_io_region_addr(bdf, 0, PORT_ATA1_CMD_BASE);
    pci_set_io_region_addr(bdf, 1, PORT_ATA1_CTRL_BASE);
    pci_set_io_region_addr(bdf, 2, PORT_ATA2_CMD_BASE);
    pci_set_io_region_addr(bdf, 3, PORT_ATA2_CTRL_BASE);
}

static void pic_ibm_init(u16 bdf, void *arg)
{
    /* PIC, IBM, MPIC & MPIC2 */
    pci_set_io_region_addr(bdf, 0, 0x80800000 + 0x00040000);
}

static void apple_macio_init(u16 bdf, void *arg)
{
    /* macio bridge */
    pci_set_io_region_addr(bdf, 0, 0x80800000);
}

static const struct pci_device_id pci_class_tbl[] = {
    /* STORAGE IDE */
    PCI_DEVICE_CLASS(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_82371SB_1,
                     PCI_CLASS_STORAGE_IDE, piix_ide_init),
    PCI_DEVICE_CLASS(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_82371AB,
                     PCI_CLASS_STORAGE_IDE, piix_ide_init),
    PCI_DEVICE_CLASS(PCI_ANY_ID, PCI_ANY_ID, PCI_CLASS_STORAGE_IDE,
                     storage_ide_init),

    /* PIC, IBM, MIPC & MPIC2 */
    PCI_DEVICE_CLASS(PCI_VENDOR_ID_IBM, 0x0046, PCI_CLASS_SYSTEM_PIC,
                     pic_ibm_init),
    PCI_DEVICE_CLASS(PCI_VENDOR_ID_IBM, 0xFFFF, PCI_CLASS_SYSTEM_PIC,
                     pic_ibm_init),

    /* 0xff00 */
    PCI_DEVICE_CLASS(PCI_VENDOR_ID_APPLE, 0x0017, 0xff00, apple_macio_init),
    PCI_DEVICE_CLASS(PCI_VENDOR_ID_APPLE, 0x0022, 0xff00, apple_macio_init),

    /* PCI bridge */
    PCI_DEVICE_CLASS(PCI_ANY_ID, PCI_ANY_ID, PCI_CLASS_BRIDGE_PCI,
                     pci_bios_init_device_bridge),

    /* default */
    PCI_DEVICE(PCI_ANY_ID, PCI_ANY_ID, pci_bios_allocate_regions),

    PCI_DEVICE_END,
};

static const struct pci_device_id pci_device_tbl[] = {
    /* PIIX4 Power Management device (for ACPI) */
    PCI_DEVICE(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_82371AB_3,
               piix4_pm_init),

    PCI_DEVICE_END,
};

static void pci_bios_init_device(u16 bdf)
{
    int pin, pic_irq, vendor_id, device_id;

    vendor_id = pci_config_readw(bdf, PCI_VENDOR_ID);
    device_id = pci_config_readw(bdf, PCI_DEVICE_ID);
    dprintf(1, "PCI: bus=%d devfn=0x%02x: vendor_id=0x%04x device_id=0x%04x\n"
            , pci_bdf_to_bus(bdf), pci_bdf_to_devfn(bdf), vendor_id, device_id);
    pci_init_device(pci_class_tbl, bdf, NULL);

    /* enable memory mappings */
    pci_config_maskw(bdf, PCI_COMMAND, 0, PCI_COMMAND_IO | PCI_COMMAND_MEMORY);

    /* map the interrupt */
    pin = pci_config_readb(bdf, PCI_INTERRUPT_PIN);
    if (pin != 0) {
        pin = pci_slot_get_pirq(bdf, pin - 1);
        pic_irq = pci_irqs[pin];
        pci_config_writeb(bdf, PCI_INTERRUPT_LINE, pic_irq);
    }

    pci_init_device(pci_device_tbl, bdf, NULL);
}

static void pci_bios_init_device_in_bus(int bus)
{
    int bdf, max;
    foreachpci_in_bus(bdf, max, bus) {
        pci_bios_init_device(bdf);
    }
}

static void
pci_bios_init_bus_rec(int bus, u8 *pci_bus)
{
    int bdf, max;
    u16 class;

    dprintf(1, "PCI: %s bus = 0x%x\n", __func__, bus);

    /* prevent accidental access to unintended devices */
    foreachpci_in_bus(bdf, max, bus) {
        class = pci_config_readw(bdf, PCI_CLASS_DEVICE);
        if (class == PCI_CLASS_BRIDGE_PCI) {
            pci_config_writeb(bdf, PCI_SECONDARY_BUS, 255);
            pci_config_writeb(bdf, PCI_SUBORDINATE_BUS, 0);
        }
    }

    foreachpci_in_bus(bdf, max, bus) {
        class = pci_config_readw(bdf, PCI_CLASS_DEVICE);
        if (class != PCI_CLASS_BRIDGE_PCI) {
            continue;
        }
        dprintf(1, "PCI: %s bdf = 0x%x\n", __func__, bdf);

        u8 pribus = pci_config_readb(bdf, PCI_PRIMARY_BUS);
        if (pribus != bus) {
            dprintf(1, "PCI: primary bus = 0x%x -> 0x%x\n", pribus, bus);
            pci_config_writeb(bdf, PCI_PRIMARY_BUS, bus);
        } else {
            dprintf(1, "PCI: primary bus = 0x%x\n", pribus);
        }

        u8 secbus = pci_config_readb(bdf, PCI_SECONDARY_BUS);
        (*pci_bus)++;
        if (*pci_bus != secbus) {
            dprintf(1, "PCI: secondary bus = 0x%x -> 0x%x\n",
                    secbus, *pci_bus);
            secbus = *pci_bus;
            pci_config_writeb(bdf, PCI_SECONDARY_BUS, secbus);
        } else {
            dprintf(1, "PCI: secondary bus = 0x%x\n", secbus);
        }

        /* set to max for access to all subordinate buses.
           later set it to accurate value */
        u8 subbus = pci_config_readb(bdf, PCI_SUBORDINATE_BUS);
        pci_config_writeb(bdf, PCI_SUBORDINATE_BUS, 255);

        pci_bios_init_bus_rec(secbus, pci_bus);

        if (subbus != *pci_bus) {
            dprintf(1, "PCI: subordinate bus = 0x%x -> 0x%x\n",
                    subbus, *pci_bus);
            subbus = *pci_bus;
        } else {
            dprintf(1, "PCI: subordinate bus = 0x%x\n", subbus);
        }
        pci_config_writeb(bdf, PCI_SUBORDINATE_BUS, subbus);
    }
}

static void
pci_bios_init_bus(void)
{
    u8 pci_bus = 0;
    pci_bios_init_bus_rec(0 /* host bus */, &pci_bus);
}

void
pci_setup(void)
{
    if (CONFIG_COREBOOT)
        // Already done by coreboot.
        return;

    dprintf(3, "pci setup\n");

    pci_region_init(&pci_bios_io_region, 0xc000, 64 * 1024 - 1);
    pci_region_init(&pci_bios_mem_region,
                    BUILD_PCIMEM_START, BUILD_PCIMEM_END - 1);
    pci_region_init(&pci_bios_prefmem_region,
                    BUILD_PCIPREFMEM_START, BUILD_PCIPREFMEM_END - 1);

    pci_bios_init_bus();

    int bdf, max;
    foreachpci(bdf, max) {
        pci_init_device(pci_isa_bridge_tbl, bdf, NULL);
    }
    pci_bios_init_device_in_bus(0 /* host bus */);
}
