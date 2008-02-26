// 16bit code to handle system clocks.
//
// Copyright (C) 2008  Kevin O'Connor <kevin@koconnor.net>
// Copyright (C) 2002  MandrakeSoft S.A.
//
// This file may be distributed under the terms of the GNU GPLv3 license.

#include "biosvar.h" // struct bregs
#include "util.h" // debug_enter
#include "disk.h" // floppy_tick

// INT 1Ah Time-of-day Service Entry Point
void VISIBLE
handle_1a(struct bregs *regs)
{
    debug_enter(regs);
    set_cf(regs, 1);
}

// User Timer Tick
void VISIBLE
handle_1c(struct bregs *regs)
{
    debug_enter(regs);
}

// INT 08h System Timer ISR Entry Point
void VISIBLE
handle_08(struct bregs *regs)
{
//    debug_enter(regs);

    floppy_tick();

    u32 counter = GET_BDA(timer_counter);
    counter++;
    // compare to one days worth of timer ticks at 18.2 hz
    if (counter >= 0x001800B0) {
        // there has been a midnight rollover at this point
        counter = 0;
        SET_BDA(timer_rollover, GET_BDA(timer_rollover) + 1);
    }

    SET_BDA(timer_counter, counter);
    // XXX - int #0x1c
    eoi_master_pic();
}

// int70h: IRQ8 - CMOS RTC
void VISIBLE
handle_70(struct bregs *regs)
{
    debug_enter(regs);
}
