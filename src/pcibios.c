// Low level ATA disk access
//
// Copyright (C) 2008  Kevin O'Connor <kevin@koconnor.net>
// Copyright (C) 2002  MandrakeSoft S.A.
//
// This file may be distributed under the terms of the GNU GPLv3 license.

#include "types.h" // u32
#include "util.h" // handle_1ab1
#include "pci.h" // pci_config_readl


/****************************************************************
 * PIR table
 ****************************************************************/

struct pir_table {
    struct pir_header pir;
    struct pir_slot slots[6];
} PACKED PIR_TABLE VISIBLE16 __attribute__((aligned(16))) = {
#if CONFIG_PIRTABLE
    .pir = {
        .signature = PIR_SIGNATURE,
        .version = 0x0100,
        .size = sizeof(struct pir_table),
        .router_devfunc = 0x08,
        .compatible_devid = 0x122e8086,
        .checksum = 0x37, // XXX - should auto calculate
    },
    .slots = {
        {
            // first slot entry PCI-to-ISA (embedded)
            .dev = 1<<3,
            .links = {
                {.link = 0x60, .bitmap = 0xdef8}, // INTA#
                {.link = 0x61, .bitmap = 0xdef8}, // INTB#
                {.link = 0x62, .bitmap = 0xdef8}, // INTC#
                {.link = 0x63, .bitmap = 0xdef8}, // INTD#
            },
            .slot_nr = 0, // embedded
        }, {
            // second slot entry: 1st PCI slot
            .dev = 2<<3,
            .links = {
                {.link = 0x61, .bitmap = 0xdef8}, // INTA#
                {.link = 0x62, .bitmap = 0xdef8}, // INTB#
                {.link = 0x63, .bitmap = 0xdef8}, // INTC#
                {.link = 0x60, .bitmap = 0xdef8}, // INTD#
            },
            .slot_nr = 1,
        }, {
            // third slot entry: 2nd PCI slot
            .dev = 3<<3,
            .links = {
                {.link = 0x62, .bitmap = 0xdef8}, // INTA#
                {.link = 0x63, .bitmap = 0xdef8}, // INTB#
                {.link = 0x60, .bitmap = 0xdef8}, // INTC#
                {.link = 0x61, .bitmap = 0xdef8}, // INTD#
            },
            .slot_nr = 2,
        }, {
            // 4th slot entry: 3rd PCI slot
            .dev = 4<<3,
            .links = {
                {.link = 0x63, .bitmap = 0xdef8}, // INTA#
                {.link = 0x60, .bitmap = 0xdef8}, // INTB#
                {.link = 0x61, .bitmap = 0xdef8}, // INTC#
                {.link = 0x62, .bitmap = 0xdef8}, // INTD#
            },
            .slot_nr = 3,
        }, {
            // 5th slot entry: 4rd PCI slot
            .dev = 5<<3,
            .links = {
                {.link = 0x60, .bitmap = 0xdef8}, // INTA#
                {.link = 0x61, .bitmap = 0xdef8}, // INTB#
                {.link = 0x62, .bitmap = 0xdef8}, // INTC#
                {.link = 0x63, .bitmap = 0xdef8}, // INTD#
            },
            .slot_nr = 4,
        }, {
            // 6th slot entry: 5rd PCI slot
            .dev = 6<<3,
            .links = {
                {.link = 0x61, .bitmap = 0xdef8}, // INTA#
                {.link = 0x62, .bitmap = 0xdef8}, // INTB#
                {.link = 0x63, .bitmap = 0xdef8}, // INTC#
                {.link = 0x60, .bitmap = 0xdef8}, // INTD#
            },
            .slot_nr = 5,
        },
    }
#endif // CONFIG_PIRTABLE
};


/****************************************************************
 * Helper functions
 ****************************************************************/

#define RET_FUNC_NOT_SUPPORTED 0x81
#define RET_BAD_VENDOR_ID      0x83
#define RET_DEVICE_NOT_FOUND   0x86
#define RET_BUFFER_TOO_SMALL   0x89

// installation check
static void
handle_1ab101(struct bregs *regs)
{
    regs->ax = 0x0001;
    regs->bx = 0x0210;
    regs->cx = 0;
    regs->edx = 0x20494350; // "PCI "
    // XXX - bochs bios code sets edi to point to 32bit code - but no
    // reference to this in spec.
    set_success(regs);
}

// find pci device
static void
handle_1ab102(struct bregs *regs)
{
    PCIDevice d;
    int ret = pci_find_device(regs->cx, regs->dx, regs->si, &d);
    if (ret) {
        set_code_fail(regs, RET_DEVICE_NOT_FOUND);
        return;
    }
    regs->bh = d.bus;
    regs->bl = d.devfn;
    set_code_success(regs);
}

// find class code
static void
handle_1ab103(struct bregs *regs)
{
    PCIDevice d;
    int ret = pci_find_class(regs->ecx, regs->si, &d);
    if (ret) {
        set_code_fail(regs, RET_DEVICE_NOT_FOUND);
        return;
    }
    regs->bh = d.bus;
    regs->bl = d.devfn;
    set_code_success(regs);
}

// read configuration byte
static void
handle_1ab108(struct bregs *regs)
{
    regs->cl = pci_config_readb(pci_bd(regs->bh, regs->bl), regs->di);
    set_code_success(regs);
}

// read configuration word
static void
handle_1ab109(struct bregs *regs)
{
    regs->cx = pci_config_readw(pci_bd(regs->bh, regs->bl), regs->di);
    set_code_success(regs);
}

// read configuration dword
static void
handle_1ab10a(struct bregs *regs)
{
    regs->ecx = pci_config_readl(pci_bd(regs->bh, regs->bl), regs->di);
    set_code_success(regs);
}

// write configuration byte
static void
handle_1ab10b(struct bregs *regs)
{
    pci_config_writeb(pci_bd(regs->bh, regs->bl), regs->di, regs->cl);
    set_code_success(regs);
}

// write configuration word
static void
handle_1ab10c(struct bregs *regs)
{
    pci_config_writew(pci_bd(regs->bh, regs->bl), regs->di, regs->cx);
    set_code_success(regs);
}

// write configuration dword
static void
handle_1ab10d(struct bregs *regs)
{
    pci_config_writel(pci_bd(regs->bh, regs->bl), regs->di, regs->ecx);
    set_code_success(regs);
}

// get irq routing options
static void
handle_1ab10e(struct bregs *regs)
{
    if (! CONFIG_PIRTABLE) {
        set_code_fail(regs, RET_FUNC_NOT_SUPPORTED);
        return;
    }

    // Validate and update size.
    u16 size = GET_FARVAR(regs->es, *(u16*)(regs->di+0));
    u32 pirsize = sizeof(PIR_TABLE.slots);
    SET_FARVAR(regs->es, *(u16*)(regs->di+0), pirsize);
    if (size < pirsize) {
        set_code_fail(regs, RET_BUFFER_TOO_SMALL);
        return;
    }

    // Get dest buffer.
    u8 *d = (u8*)(GET_FARVAR(regs->es, *(u16*)(regs->di+2)) + 0);
    u16 destseg = GET_FARVAR(regs->es, *(u16*)(regs->di+4));

    // Memcpy pir table slots to dest buffer.
    u8 *p = (u8*)PIR_TABLE.slots;
    u8 *end = p + pirsize;
    for (; p<end; p++, d++) {
        u8 c = GET_VAR(CS, *p);
        SET_FARVAR(destseg, *d, c);
    }

    // XXX - bochs bios sets bx to (1 << 9) | (1 << 11)
    regs->bx = GET_VAR(CS, PIR_TABLE.pir.exclusive_irqs);
    set_code_success(regs);
}

static void
handle_1ab1XX(struct bregs *regs)
{
    set_code_fail(regs, RET_FUNC_NOT_SUPPORTED);
}

void
handle_1ab1(struct bregs *regs)
{
    //debug_stub(regs);

    if (! CONFIG_PCIBIOS) {
        set_fail(regs);
        return;
    }

    switch (regs->al) {
    case 0x01: handle_1ab101(regs); break;
    case 0x02: handle_1ab102(regs); break;
    case 0x03: handle_1ab103(regs); break;
    case 0x08: handle_1ab108(regs); break;
    case 0x09: handle_1ab109(regs); break;
    case 0x0a: handle_1ab10a(regs); break;
    case 0x0b: handle_1ab10b(regs); break;
    case 0x0c: handle_1ab10c(regs); break;
    case 0x0d: handle_1ab10d(regs); break;
    case 0x0e: handle_1ab10e(regs); break;
    default:   handle_1ab1XX(regs); break;
    }
}
