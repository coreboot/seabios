// 16bit code to handle keyboard requests.
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
}

// INT 16h Keyboard Service Entry Point
void VISIBLE
handle_16(struct bregs *regs)
{
    //debug_enter(regs);
}

// INT09h : Keyboard Hardware Service Entry Point
void VISIBLE
handle_09(struct bregs *regs)
{
    debug_enter(regs);
}

// INT74h : PS/2 mouse hardware interrupt
void VISIBLE
handle_74(struct bregs *regs)
{
    debug_enter(regs);
}
