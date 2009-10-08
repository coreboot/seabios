// Raw screen writing and debug output code.
//
// Copyright (C) 2008  Kevin O'Connor <kevin@koconnor.net>
//
// This file may be distributed under the terms of the GNU LGPLv3 license.

#include <stdarg.h> // va_list

#include "farptr.h" // GET_VAR
#include "util.h" // printf
#include "bregs.h" // struct bregs
#include "config.h" // CONFIG_*
#include "biosvar.h" // GET_GLOBAL

#define DEBUG_PORT PORT_SERIAL1
#define DEBUG_TIMEOUT 100000

void
debug_serial_setup()
{
    if (!CONFIG_DEBUG_SERIAL)
        return;
    // setup for serial logging: 8N1
    u8 oldparam, newparam = 0x03;
    oldparam = inb(DEBUG_PORT+SEROFF_LCR);
    outb(newparam, DEBUG_PORT+SEROFF_LCR);
    // Disable irqs
    u8 oldier, newier = 0;
    oldier = inb(DEBUG_PORT+SEROFF_IER);
    outb(newier, DEBUG_PORT+SEROFF_IER);

    if (oldparam != newparam || oldier != newier)
        dprintf(1, "Changing serial settings was %x/%x now %x/%x\n"
                , oldparam, oldier, newparam, newier);
}

// Write a character to the serial port.
static void
debug_serial(char c)
{
    if (!CONFIG_DEBUG_SERIAL)
        return;
    int timeout = DEBUG_TIMEOUT;
    while ((inb(DEBUG_PORT+SEROFF_LSR) & 0x60) != 0x60)
        if (!timeout--)
            // Ran out of time.
            return;
    outb(c, DEBUG_PORT+SEROFF_DATA);
}

// Make sure all serial port writes have been completely sent.
static void
debug_serial_flush()
{
    if (!CONFIG_DEBUG_SERIAL)
        return;
    int timeout = DEBUG_TIMEOUT;
    while ((inb(DEBUG_PORT+SEROFF_LSR) & 0x40) != 0x40)
        if (!timeout--)
            // Ran out of time.
            return;
}

// Show a character on the screen.
static void
screenc(u8 c)
{
    if (MODE16)
        // printf is only used in 32bit code.
        return;
    struct bregs br;
    memset(&br, 0, sizeof(br));
    br.flags = F_IF;
    br.ah = 0x0e;
    br.al = c;
    call16_int(0x10, &br);
}

// Output a character.
static void
putc(u16 action, char c)
{
    if (CONFIG_DEBUG_LEVEL && (CONFIG_SCREEN_AND_DEBUG || !action)) {
        if (! CONFIG_COREBOOT)
            // Send character to debug port.
            outb(c, PORT_BIOS_DEBUG);
        // Send character to serial port.
        if (c == '\n')
            debug_serial('\r');
        debug_serial(c);
    }

    if (action) {
        // Send character to video screen.
        if (c == '\n')
            screenc('\r');
        screenc(c);
    }
}

// Ouptut a string.
static void
puts(u16 action, const char *s)
{
    for (; *s; s++)
        putc(action, *s);
}

// Output a string that is in the CS segment.
static void
puts_cs(u16 action, const char *s)
{
    char *vs = (char*)s;
    for (;; vs++) {
        char c = GET_GLOBAL(*vs);
        if (!c)
            break;
        putc(action, c);
    }
}

// Output an unsigned integer.
static void
putuint(u16 action, u32 val)
{
    char buf[12];
    char *d = &buf[sizeof(buf) - 1];
    *d-- = '\0';
    for (;;) {
        *d = (val % 10) + '0';
        val /= 10;
        if (!val)
            break;
        d--;
    }
    puts(action, d);
}

// Output a single digit hex character.
static inline void
putsinglehex(u16 action, u32 val)
{
    if (val <= 9)
        val = '0' + val;
    else
        val = 'a' + val - 10;
    putc(action, val);
}

// Output an integer in hexadecimal.
static void
puthex(u16 action, u32 val, int width)
{
    if (!width) {
        u32 tmp = val;
        width = 1;
        if (tmp > 0xffff) {
            width += 4;
            tmp >>= 16;
        }
        if (tmp > 0xff) {
            width += 2;
            tmp >>= 8;
        }
        if (tmp > 0xf)
            width += 1;
    }

    switch (width) {
    default: putsinglehex(action, (val >> 28) & 0xf);
    case 7:  putsinglehex(action, (val >> 24) & 0xf);
    case 6:  putsinglehex(action, (val >> 20) & 0xf);
    case 5:  putsinglehex(action, (val >> 16) & 0xf);
    case 4:  putsinglehex(action, (val >> 12) & 0xf);
    case 3:  putsinglehex(action, (val >> 8) & 0xf);
    case 2:  putsinglehex(action, (val >> 4) & 0xf);
    case 1:  putsinglehex(action, (val >> 0) & 0xf);
    }
}

static inline int
isdigit(u8 c)
{
    return ((u8)(c - '0')) < 10;
}

static void
bvprintf(u16 action, const char *fmt, va_list args)
{
    const char *s = fmt;
    for (;; s++) {
        char c = GET_GLOBAL(*(u8*)s);
        if (!c)
            break;
        if (c != '%') {
            putc(action, c);
            continue;
        }
        const char *n = s+1;
        int field_width = 0;
        for (;;) {
            c = GET_GLOBAL(*(u8*)n);
            if (!isdigit(c))
                break;
            field_width = field_width * 10 + c - '0';
            n++;
        }
        if (c == 'l') {
            // Ignore long format indicator
            n++;
            c = GET_GLOBAL(*(u8*)n);
        }
        s32 val;
        const char *sarg;
        switch (c) {
        case '%':
            putc(action, '%');
            break;
        case 'd':
            val = va_arg(args, s32);
            if (val < 0) {
                putc(action, '-');
                val = -val;
            }
            putuint(action, val);
            break;
        case 'u':
            val = va_arg(args, s32);
            putuint(action, val);
            break;
        case 'p':
            /* %p always has 0x prepended */
            putc(action, '0');
            putc(action, 'x');
            field_width = 8;
        case 'x':
            val = va_arg(args, s32);
            puthex(action, val, field_width);
            break;
        case 'c':
            val = va_arg(args, int);
            putc(action, val);
            break;
        case '.':
            // Hack to support "%.s" - meaning string on stack.
            if (GET_GLOBAL(*(u8*)(n+1)) != 's')
                break;
            n++;
            sarg = va_arg(args, const char *);
            puts(action, sarg);
            break;
        case 's':
            sarg = va_arg(args, const char *);
            puts_cs(action, sarg);
            break;
        default:
            putc(action, '%');
            n = s;
        }
        s = n;
    }
    debug_serial_flush();
}

void
panic(const char *fmt, ...)
{
    if (CONFIG_DEBUG_LEVEL) {
        va_list args;
        va_start(args, fmt);
        bvprintf(0, fmt, args);
        va_end(args);
    }

    // XXX - use PANIC PORT.
    irq_disable();
    for (;;)
        hlt();
}

void
__dprintf(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    bvprintf(0, fmt, args);
    va_end(args);
}

void
printf(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    bvprintf(1, fmt, args);
    va_end(args);
}

void
hexdump(const void *d, int len)
{
    int count=0;
    while (len > 0) {
        if (count % 8 == 0) {
            putc(0, '\n');
            puthex(0, count*4, 8);
            putc(0, ':');
        } else {
            putc(0, ' ');
        }
        puthex(0, *(u32*)d, 8);
        count++;
        len-=4;
        d+=4;
    }
    putc(0, '\n');
    debug_serial_flush();
}

static void
dump_regs(struct bregs *regs)
{
    if (!regs) {
        dprintf(1, "  NULL\n");
        return;
    }
    dprintf(1, "   a=%08x  b=%08x  c=%08x  d=%08x ds=%04x es=%04x ss=%04x\n"
            , regs->eax, regs->ebx, regs->ecx, regs->edx
            , regs->ds, regs->es, GET_SEG(SS));
    dprintf(1, "  si=%08x di=%08x bp=%08x sp=%08x cs=%04x ip=%04x  f=%04x\n"
            , regs->esi, regs->edi, regs->ebp, (u32)&regs[1]
            , regs->code.seg, regs->code.offset, regs->flags);
}

// Report entry to an Interrupt Service Routine (ISR).
void
__debug_isr(const char *fname)
{
    puts_cs(0, fname);
    putc(0, '\n');
    debug_serial_flush();
}

// Function called on handler startup.
void
__debug_enter(struct bregs *regs, const char *fname)
{
    dprintf(1, "enter %s:\n", fname);
    dump_regs(regs);
}

// Send debugging output info.
void
__debug_stub(struct bregs *regs, int lineno, const char *fname)
{
    dprintf(1, "stub %s:%d:\n", fname, lineno);
    dump_regs(regs);
}

// Report on a handler returning a failure notification to the caller.
void
__set_fail(struct bregs *regs, int lineno, const char *fname)
{
    dprintf(1, "fail %s:%d:\n", fname, lineno);
    dump_regs(regs);
    set_fail_silent(regs);
}

// Report on a handler returning a failure code to the caller.  Note,
// the lineno and return code are encoded in the same parameter as gcc
// does a better job of scheduling function calls when there are 3 or
// less parameters.
void
__set_code_fail(struct bregs *regs, u32 linecode, const char *fname)
{
    u8 code = linecode;
    u32 lineno = linecode >> 8;
    dprintf(1, "fail %s:%d(%x):\n", fname, lineno, code);
    dump_regs(regs);
    set_code_fail_silent(regs, code);
}
