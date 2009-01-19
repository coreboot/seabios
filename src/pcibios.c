// PCI BIOS (int 1a/b1) calls
//
// Copyright (C) 2008  Kevin O'Connor <kevin@koconnor.net>
// Copyright (C) 2002  MandrakeSoft S.A.
//
// This file may be distributed under the terms of the GNU LGPLv3 license.

#include "types.h" // u32
#include "util.h" // handle_1ab1
#include "pci.h" // pci_config_readl
#include "bregs.h" // struct bregs
#include "biosvar.h" // GET_EBDA
#include "pci_regs.h" // PCI_VENDOR_ID

#define RET_FUNC_NOT_SUPPORTED 0x81
#define RET_BAD_VENDOR_ID      0x83
#define RET_DEVICE_NOT_FOUND   0x86
#define RET_BUFFER_TOO_SMALL   0x89

// installation check
static void
handle_1ab101(struct bregs *regs)
{
    // Find max bus.
    int bdf, max;
    foreachpci(bdf, max) {
    }

    regs->al = 0x01; // Flags - "Config Mechanism #1" supported.
    regs->bx = 0x0210; // PCI version 2.10
    regs->cl = pci_bdf_to_bus(max - 1);
    regs->edx = 0x20494350; // "PCI "
    // XXX - bochs bios code sets edi to point to 32bit code - but no
    // reference to this in spec.
    set_code_success(regs);
}

// find pci device
static void
handle_1ab102(struct bregs *regs)
{
    u32 id = (regs->cx << 16) | regs->dx;
    int count = regs->si;
    int bdf, max;
    foreachpci(bdf, max) {
        u32 v = pci_config_readl(bdf, PCI_VENDOR_ID);
        if (v != id)
            continue;
        if (count--)
            continue;
        regs->bx = bdf;
        set_code_success(regs);
        return;
    }
    set_code_fail(regs, RET_DEVICE_NOT_FOUND);
}

// find class code
static void
handle_1ab103(struct bregs *regs)
{
    int count = regs->si;
    u32 classprog = regs->ecx;
    int bdf, max;
    foreachpci(bdf, max) {
        u32 v = pci_config_readl(bdf, PCI_CLASS_REVISION);
        if ((v>>8) != classprog)
            continue;
        if (count--)
            continue;
        regs->bx = bdf;
        set_code_success(regs);
        return;
    }
    set_code_fail(regs, RET_DEVICE_NOT_FOUND);
}

// read configuration byte
static void
handle_1ab108(struct bregs *regs)
{
    regs->cl = pci_config_readb(regs->bx, regs->di);
    set_code_success(regs);
}

// read configuration word
static void
handle_1ab109(struct bregs *regs)
{
    regs->cx = pci_config_readw(regs->bx, regs->di);
    set_code_success(regs);
}

// read configuration dword
static void
handle_1ab10a(struct bregs *regs)
{
    regs->ecx = pci_config_readl(regs->bx, regs->di);
    set_code_success(regs);
}

// write configuration byte
static void
handle_1ab10b(struct bregs *regs)
{
    pci_config_writeb(regs->bx, regs->di, regs->cl);
    set_code_success(regs);
}

// write configuration word
static void
handle_1ab10c(struct bregs *regs)
{
    pci_config_writew(regs->bx, regs->di, regs->cx);
    set_code_success(regs);
}

// write configuration dword
static void
handle_1ab10d(struct bregs *regs)
{
    pci_config_writel(regs->bx, regs->di, regs->ecx);
    set_code_success(regs);
}

// get irq routing options
static void
handle_1ab10e(struct bregs *regs)
{
    struct pir_header *pirtable_g = (void*)(GET_GLOBAL(PirOffset) + 0);
    if (! pirtable_g) {
        set_code_fail(regs, RET_FUNC_NOT_SUPPORTED);
        return;
    }

    // Validate and update size.
    u16 size = GET_FARVAR(regs->es, *(u16*)(regs->di+0));
    u16 pirsize = (GET_GLOBAL(pirtable_g->size)
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
    memcpy_fl(MAKE_FLATPTR(destseg, d)
              , MAKE_FLATPTR(SEG_BIOS, pirtable_g->slots)
              , pirsize);

    // XXX - bochs bios sets bx to (1 << 9) | (1 << 11)
    regs->bx = GET_GLOBAL(pirtable_g->exclusive_irqs);
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
