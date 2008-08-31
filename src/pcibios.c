// PCI BIOS (int 1a/b1) calls
//
// Copyright (C) 2008  Kevin O'Connor <kevin@koconnor.net>
// Copyright (C) 2002  MandrakeSoft S.A.
//
// This file may be distributed under the terms of the GNU GPLv3 license.

#include "types.h" // u32
#include "util.h" // handle_1ab1
#include "pci.h" // pci_config_readl
#include "bregs.h" // struct bregs
#include "biosvar.h" // GET_EBDA

#define RET_FUNC_NOT_SUPPORTED 0x81
#define RET_BAD_VENDOR_ID      0x83
#define RET_DEVICE_NOT_FOUND   0x86
#define RET_BUFFER_TOO_SMALL   0x89

// installation check
static void
handle_1ab101(struct bregs *regs)
{
    regs->al = 0x01; // Flags - "Config Mechanism #1" supported.
    regs->bx = 0x0210; // PCI version 2.10
    regs->cl = CONFIG_PCI_BUS_COUNT - 1;
    regs->edx = 0x20494350; // "PCI "
    // XXX - bochs bios code sets edi to point to 32bit code - but no
    // reference to this in spec.
    set_code_success(regs);
}

// find pci device
static void
handle_1ab102(struct bregs *regs)
{
    PCIDevice d;
    int ret = pci_find_device(regs->dx, regs->cx, regs->si, &d);
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
    int ret = pci_find_classprog(regs->ecx, regs->si, &d);
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
    struct pir_header *pirtable_far = (struct pir_header*)GET_EBDA(pir_loc);
    if (! pirtable_far) {
        set_code_fail(regs, RET_FUNC_NOT_SUPPORTED);
        return;
    }

    // Validate and update size.
    u16 size = GET_FARVAR(regs->es, *(u16*)(regs->di+0));
    u16 pirsize = (GET_FARPTR(pirtable_far->size)
                   - sizeof(struct pir_header));
    SET_FARVAR(regs->es, *(u16*)(regs->di+0), pirsize);
    if (size < pirsize) {
        set_code_fail(regs, RET_BUFFER_TOO_SMALL);
        return;
    }

    // Get dest buffer.
    u16 d = (GET_FARVAR(regs->es, *(u16*)(regs->di+2)) + 0);
    u16 destseg = GET_FARVAR(regs->es, *(u16*)(regs->di+4));

    // Memcpy pir table slots to dest buffer.
    memcpy_far(MAKE_FARPTR(destseg, d), pirtable_far, pirsize);

    // XXX - bochs bios sets bx to (1 << 9) | (1 << 11)
    regs->bx = GET_FARPTR(pirtable_far->exclusive_irqs);
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
