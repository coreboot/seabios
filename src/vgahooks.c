// Hooks for via vgabios calls into main bios.
//
// Copyright (C) 2008  Kevin O'Connor <kevin@koconnor.net>
//
// This file may be distributed under the terms of the GNU GPLv3 license.

#include "bregs.h" // set_fail
#include "util.h" // handle_155f
#include "config.h" // CONFIG_*

static void
handle_155f01(struct bregs *regs)
{
    regs->eax = 0x5f;
    regs->cl = 2; // panel type =  2 = 1024 * 768
    set_success(regs);
}

static void
handle_155f02(struct bregs *regs)
{
    regs->eax = 0x5f;
    regs->bx = 2;
    regs->cx = 0x401;  // PAL + crt only
    regs->dx = 0;  // TV Layout - default
    set_success(regs);
}

static void
handle_155f18(struct bregs *regs)
{
    regs->eax = 0x5f;
    regs->ebx = 0x545; // MCLK = 133, 32M frame buffer, 256 M main memory
    regs->ecx = 0x060;
    set_success(regs);
}

static void
handle_155f19(struct bregs *regs)
{
    set_fail_silent(regs);
}

static void
handle_155fXX(struct bregs *regs)
{
    set_code_fail(regs, RET_EUNSUPPORTED);
}

void
handle_155f(struct bregs *regs)
{
    if (! CONFIG_VGAHOOKS) {
        handle_155fXX(regs);
        return;
    }

    switch (regs->al) {
    case 0x01: handle_155f01(regs); break;
    case 0x02: handle_155f02(regs); break;
    case 0x18: handle_155f18(regs); break;
    case 0x19: handle_155f19(regs); break;
    default:   handle_155fXX(regs); break;
    }
}
