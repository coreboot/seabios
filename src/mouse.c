// 16bit code to handle mouse events.
//
// Copyright (C) 2008  Kevin O'Connor <kevin@koconnor.net>
// Copyright (C) 2002  MandrakeSoft S.A.
//
// This file may be distributed under the terms of the GNU GPLv3 license.

#include "biosvar.h" // struct bregs
#include "util.h" // debug_enter

void
handle_15c2(struct bregs *regs)
{
    // XXX
}

static u8
int74_function()
{
    // XXX
    return 0;
}

// INT74h : PS/2 mouse hardware interrupt
void VISIBLE
handle_74(struct bregs *regs)
{
    debug_enter(regs);

    irq_enable();
    u8 ret = int74_function();
    if (ret) {
        // XXX - far call to ptr at ebda:0022
    }
    irq_disable();
    eoi_both_pics();
}
