// 16bit code to handle serial and printer services.
//
// Copyright (C) 2008  Kevin O'Connor <kevin@koconnor.net>
// Copyright (C) 2002  MandrakeSoft S.A.
//
// This file may be distributed under the terms of the GNU GPLv3 license.

#include "biosvar.h" // struct bregs
#include "util.h" // debug_enter

// INT 14h Serial Communications Service Entry Point
void VISIBLE
handle_14(struct bregs *regs)
{
    debug_enter(regs);
}

// INT17h : Printer Service Entry Point
void VISIBLE
handle_17(struct bregs *regs)
{
    debug_enter(regs);
}
