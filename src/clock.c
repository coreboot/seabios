// 16bit code to handle system clocks.
//
// Copyright (C) 2008  Kevin O'Connor <kevin@koconnor.net>
// Copyright (C) 2002  MandrakeSoft S.A.
//
// This file may be distributed under the terms of the GNU GPLv3 license.

#include "biosvar.h" // struct bregs
#include "util.h" // debug_enter
#include "disk.h" // floppy_tick
#include "cmos.h" // inb_cmos

#define DEBUGF1(fmt, args...) bprintf(0, fmt , ##args)
#define DEBUGF(fmt, args...)

static void
pit_setup()
{
    // timer0: binary count, 16bit count, mode 2
    outb(0x34, PORT_PIT_MODE);
    // maximum count of 0000H = 18.2Hz
    outb(0x0, PORT_PIT_COUNTER0);
    outb(0x0, PORT_PIT_COUNTER0);
}

static u32
bcd2bin(u8 val)
{
    return (val & 0xf) + ((val >> 4) * 10);
}

void
timer_setup()
{
    pit_setup();

    u32 seconds = bcd2bin(inb_cmos(CMOS_RTC_SECONDS));
    u32 ticks = (seconds * 18206507) / 1000000;
    u32 minutes = bcd2bin(inb_cmos(CMOS_RTC_MINUTES));
    ticks += (minutes * 10923904) / 10000;
    u32 hours = bcd2bin(inb_cmos(CMOS_RTC_HOURS));
    ticks += (hours * 65543427) / 1000;
    SET_BDA(timer_counter, ticks);
    SET_BDA(timer_rollover, 0);
}

static void
init_rtc()
{
    outb_cmos(0x26, CMOS_STATUS_A);
    outb_cmos(0x02, CMOS_STATUS_B);
    inb_cmos(CMOS_STATUS_C);
    inb_cmos(CMOS_STATUS_D);
}

static u8
rtc_updating()
{
    // This function checks to see if the update-in-progress bit
    // is set in CMOS Status Register A.  If not, it returns 0.
    // If it is set, it tries to wait until there is a transition
    // to 0, and will return 0 if such a transition occurs.  A 1
    // is returned only after timing out.  The maximum period
    // that this bit should be set is constrained to 244useconds.
    // The count I use below guarantees coverage or more than
    // this time, with any reasonable IPS setting.

    u16 count = 25000;
    while (--count != 0) {
        if ( (inb_cmos(CMOS_STATUS_A) & 0x80) == 0 )
            return 0;
    }
    return 1; // update-in-progress never transitioned to 0
}

// get current clock count
static void
handle_1a00(struct bregs *regs)
{
    u32 ticks = GET_BDA(timer_counter);
    regs->cx = ticks >> 16;
    regs->dx = ticks;
    regs->al = GET_BDA(timer_rollover);
    SET_BDA(timer_rollover, 0); // reset flag
    set_success(regs);
}

// Set Current Clock Count
static void
handle_1a01(struct bregs *regs)
{
    u32 ticks = (regs->cx << 16) | regs->dx;
    SET_BDA(timer_counter, ticks);
    SET_BDA(timer_rollover, 0); // reset flag
    regs->ah = 0;
    set_success(regs);
}

// Read CMOS Time
static void
handle_1a02(struct bregs *regs)
{
    if (rtc_updating()) {
        set_fail(regs);
        return;
    }

    regs->dh = inb_cmos(CMOS_RTC_SECONDS);
    regs->cl = inb_cmos(CMOS_RTC_MINUTES);
    regs->ch = inb_cmos(CMOS_RTC_HOURS);
    regs->dl = inb_cmos(CMOS_STATUS_B) & 0x01;
    regs->ah = 0;
    regs->al = regs->ch;
    set_success(regs);
}

// Set CMOS Time
static void
handle_1a03(struct bregs *regs)
{
    // Using a debugger, I notice the following masking/setting
    // of bits in Status Register B, by setting Reg B to
    // a few values and getting its value after INT 1A was called.
    //
    //        try#1       try#2       try#3
    // before 1111 1101   0111 1101   0000 0000
    // after  0110 0010   0110 0010   0000 0010
    //
    // Bit4 in try#1 flipped in hardware (forced low) due to bit7=1
    // My assumption: RegB = ((RegB & 01100000b) | 00000010b)
    if (rtc_updating()) {
        init_rtc();
        // fall through as if an update were not in progress
    }
    outb_cmos(regs->dh, CMOS_RTC_SECONDS);
    outb_cmos(regs->cl, CMOS_RTC_MINUTES);
    outb_cmos(regs->ch, CMOS_RTC_HOURS);
    // Set Daylight Savings time enabled bit to requested value
    u8 val8 = (inb_cmos(CMOS_STATUS_B) & 0x60) | 0x02 | (regs->dl & 0x01);
    outb_cmos(val8, CMOS_STATUS_B);
    regs->ah = 0;
    regs->al = val8; // val last written to Reg B
    set_success(regs);
}

// Read CMOS Date
static void
handle_1a04(struct bregs *regs)
{
    regs->ah = 0;
    if (rtc_updating()) {
        set_fail(regs);
        return;
    }
    regs->cl = inb_cmos(CMOS_RTC_YEAR);
    regs->dh = inb_cmos(CMOS_RTC_MONTH);
    regs->dl = inb_cmos(CMOS_RTC_DAY_MONTH);
    regs->ch = inb_cmos(CMOS_CENTURY);
    regs->al = regs->ch;
    set_success(regs);
}

// Set CMOS Date
static void
handle_1a05(struct bregs *regs)
{
    // Using a debugger, I notice the following masking/setting
    // of bits in Status Register B, by setting Reg B to
    // a few values and getting its value after INT 1A was called.
    //
    //        try#1       try#2       try#3       try#4
    // before 1111 1101   0111 1101   0000 0010   0000 0000
    // after  0110 1101   0111 1101   0000 0010   0000 0000
    //
    // Bit4 in try#1 flipped in hardware (forced low) due to bit7=1
    // My assumption: RegB = (RegB & 01111111b)
    if (rtc_updating()) {
        init_rtc();
        set_fail(regs);
        return;
    }
    outb_cmos(regs->cl, CMOS_RTC_YEAR);
    outb_cmos(regs->dh, CMOS_RTC_MONTH);
    outb_cmos(regs->dl, CMOS_RTC_DAY_MONTH);
    outb_cmos(regs->ch, CMOS_CENTURY);
    u8 val8 = inb_cmos(CMOS_STATUS_B) & 0x7f; // clear halt-clock bit
    outb_cmos(val8, CMOS_STATUS_B);
    regs->ah = 0;
    regs->al = val8; // AL = val last written to Reg B
    set_success(regs);
}

// Set Alarm Time in CMOS
static void
handle_1a06(struct bregs *regs)
{
    // Using a debugger, I notice the following masking/setting
    // of bits in Status Register B, by setting Reg B to
    // a few values and getting its value after INT 1A was called.
    //
    //        try#1       try#2       try#3
    // before 1101 1111   0101 1111   0000 0000
    // after  0110 1111   0111 1111   0010 0000
    //
    // Bit4 in try#1 flipped in hardware (forced low) due to bit7=1
    // My assumption: RegB = ((RegB & 01111111b) | 00100000b)
    u8 val8 = inb_cmos(CMOS_STATUS_B); // Get Status Reg B
    regs->ax = 0;
    if (val8 & 0x20) {
        // Alarm interrupt enabled already
        set_fail(regs);
        return;
    }
    if (rtc_updating()) {
        init_rtc();
        // fall through as if an update were not in progress
    }
    outb_cmos(regs->dh, CMOS_RTC_SECONDS_ALARM);
    outb_cmos(regs->cl, CMOS_RTC_MINUTES_ALARM);
    outb_cmos(regs->ch, CMOS_RTC_HOURS_ALARM);
    outb(inb(PORT_PIC2_DATA) & ~PIC2_IRQ8, PORT_PIC2_DATA); // enable IRQ 8
    // enable Status Reg B alarm bit, clear halt clock bit
    outb_cmos((val8 & 0x7f) | 0x20, CMOS_STATUS_B);
    set_success(regs);
}

// Turn off Alarm
static void
handle_1a07(struct bregs *regs)
{
    // Using a debugger, I notice the following masking/setting
    // of bits in Status Register B, by setting Reg B to
    // a few values and getting its value after INT 1A was called.
    //
    //        try#1       try#2       try#3       try#4
    // before 1111 1101   0111 1101   0010 0000   0010 0010
    // after  0100 0101   0101 0101   0000 0000   0000 0010
    //
    // Bit4 in try#1 flipped in hardware (forced low) due to bit7=1
    // My assumption: RegB = (RegB & 01010111b)
    u8 val8 = inb_cmos(CMOS_STATUS_B); // Get Status Reg B
    // clear clock-halt bit, disable alarm bit
    outb_cmos(val8 & 0x57, CMOS_STATUS_B); // disable alarm bit
    regs->ah = 0;
    regs->al = val8; // val last written to Reg B
    set_success(regs);
}

// Unsupported
static void
handle_1aXX(struct bregs *regs)
{
    set_fail(regs);
}

// INT 1Ah Time-of-day Service Entry Point
void VISIBLE16
handle_1a(struct bregs *regs)
{
    //debug_enter(regs);
    switch (regs->ah) {
    case 0x00: handle_1a00(regs); break;
    case 0x01: handle_1a01(regs); break;
    case 0x02: handle_1a02(regs); break;
    case 0x03: handle_1a03(regs); break;
    case 0x04: handle_1a04(regs); break;
    case 0x05: handle_1a05(regs); break;
    case 0x06: handle_1a06(regs); break;
    case 0x07: handle_1a07(regs); break;
    case 0xb1: handle_1ab1(regs); break;
    default:   handle_1aXX(regs); break;
    }
}

// User Timer Tick
void VISIBLE16
handle_1c()
{
    //debug_enter(regs);
}

// INT 08h System Timer ISR Entry Point
void VISIBLE16
handle_08()
{
    //debug_isr();
    irq_enable();

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

    // chain to user timer tick INT #0x1c
    struct bregs br;
    memset(&br, 0, sizeof(br));
    call16_int(0x1c, &br);

    irq_disable();

    eoi_master_pic();
}

// Set Interval requested.
static void
handle_158300(struct bregs *regs)
{
    if (GET_BDA(rtc_wait_flag) & RWS_WAIT_PENDING) {
        // Interval already set.
        DEBUGF("int15: Func 83h, failed, already waiting.\n" );
        set_code_fail(regs, RET_EUNSUPPORTED);
    }
    // Interval not already set.
    SET_BDA(rtc_wait_flag, RWS_WAIT_PENDING);  // Set status byte.
    u32 v = (regs->es << 16) | regs->bx;
    SET_BDA(ptr_user_wait_complete_flag, v);
    v = (regs->dx << 16) | regs->cx;
    SET_BDA(user_wait_timeout, v);

    // Unmask IRQ8 so INT70 will get through.
    u8 irqDisable = inb(PORT_PIC2_DATA);
    outb(irqDisable & ~PIC2_IRQ8, PORT_PIC2_DATA);
    // Turn on the Periodic Interrupt timer
    u8 bRegister = inb_cmos(CMOS_STATUS_B);
    outb_cmos(CMOS_STATUS_B, bRegister | CSB_EN_ALARM_IRQ);

    set_success(regs); // XXX - no set ah?
}

// Clear interval requested
static void
handle_158301(struct bregs *regs)
{
    SET_BDA(rtc_wait_flag, 0); // Clear status byte
    // Turn off the Periodic Interrupt timer
    u8 bRegister = inb_cmos(CMOS_STATUS_B);
    outb_cmos(CMOS_STATUS_B, bRegister & ~CSB_EN_ALARM_IRQ);
    set_success(regs); // XXX - no set ah?
}

static void
handle_1583XX(struct bregs *regs)
{
    set_code_fail(regs, RET_EUNSUPPORTED);
    regs->al--;
}

void
handle_1583(struct bregs *regs)
{
    switch (regs->al) {
    case 0x00: handle_158300(regs); break;
    case 0x01: handle_158301(regs); break;
    default:   handle_1583XX(regs); break;
    }
}

// int70h: IRQ8 - CMOS RTC
void VISIBLE16
handle_70()
{
    debug_isr();

    // Check which modes are enabled and have occurred.
    u8 registerB = inb_cmos(CMOS_STATUS_B);
    u8 registerC = inb_cmos(CMOS_STATUS_C);

    if (!(registerB & 0x60))
        goto done;
    if (registerC & 0x20) {
        // Handle Alarm Interrupt.
        struct bregs br;
        memset(&br, 0, sizeof(br));
        call16_int(0x4a, &br);
        irq_disable();
    }
    if (!(registerC & 0x40))
        goto done;

    // Handle Periodic Interrupt.

    if (!GET_BDA(rtc_wait_flag))
        goto done;

    // Wait Interval (Int 15, AH=83) active.
    u32 time = GET_BDA(user_wait_timeout);  // Time left in microseconds.
    if (time < 0x3D1) {
        // Done waiting.
        u32 segoff = GET_BDA(ptr_user_wait_complete_flag);
        u16 segment = segoff >> 16;
        u16 offset = segoff & 0xffff;
        // Turn off status byte.
        SET_BDA(rtc_wait_flag, 0);
        // Clear the Periodic Interrupt.
        outb_cmos(registerB & 0x37, CMOS_STATUS_B);
        // Write to specified flag byte.
        u8 oldval = GET_FARVAR(segment, *(u8*)(offset+0));
        SET_FARVAR(segment, *(u8*)(offset+0), oldval | 0x80);
    } else {
        // Continue waiting.
        time -= 0x3D1;
        SET_BDA(user_wait_timeout, time);
    }

done:
    eoi_both_pics();
}
