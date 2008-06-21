// Raw screen writing code.
//
// Copyright (C) 2008  Kevin O'Connor <kevin@koconnor.net>
//
// This file may be distributed under the terms of the GNU GPLv3 license.

#include <stdarg.h> // va_list

#include "farptr.h" // GET_VAR
#include "util.h" // printf
#include "biosvar.h" // struct bregs

#define DEBUG_PORT 0x03f8
#define DEBUG_TIMEOUT 100000

void
debug_serial_setup()
{
    if (!CONFIG_DEBUG_SERIAL)
        return;
    // setup for serial logging: 8N1
    u8 oldparam, newparam = 0x03;
    oldparam = inb(DEBUG_PORT+3);
    outb(newparam, DEBUG_PORT+3);
    // Disable irqs
    u8 oldier, newier = 0;
    oldier = inb(DEBUG_PORT+1);
    outb(newier, DEBUG_PORT+1);

    if (oldparam != newparam || oldier != newier)
        dprintf(1, "Changing serial settings was %x/%x now %x/%x\n"
                , oldparam, oldier, newparam, newier);
}

static void
debug_serial(char c)
{
    int timeout = DEBUG_TIMEOUT;
    while ((inb(DEBUG_PORT+5) & 0x60) != 0x60)
        if (!timeout--)
            // Ran out of time.
            return;
    outb(c, DEBUG_PORT);
}

static void
screenc(u8 c)
{
    struct bregs br;
    memset(&br, 0, sizeof(br));
    br.ah = 0x0e;
    br.al = c;
    call16_int(0x10, &br);
}

// Write a charcter to the framebuffer.
static void
putc(u16 action, char c)
{
    if (CONFIG_DEBUG_LEVEL) {
        if (! CONFIG_COREBOOT)
            // Send character to debug port.
            outb(c, PORT_BIOS_DEBUG);
        if (CONFIG_DEBUG_SERIAL) {
            // Send character to serial port.
            if (c == '\n')
                debug_serial('\r');
            debug_serial(c);
        }
    }

    if (action) {
        // Send character to video screen.
        if (c == '\n')
            screenc('\r');
        screenc(c);
    }
}

// Write a string to the framebuffer.
static void
puts(u16 action, const char *s)
{
    for (; *s; s++)
        putc(action, *s);
}

// Write a string to the framebuffer.
static void
puts_cs(u16 action, const char *s)
{
    for (;; s++) {
        char c = GET_VAR(CS, *(u8*)s);
        if (!c)
            break;
        putc(action, c);
    }
}

// Write an unsigned integer to the screen.
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

// Write a single digit hex character to the screen.
static inline void
putsinglehex(u16 action, u32 val)
{
    if (val <= 9)
        val = '0' + val;
    else
        val = 'a' + val - 10;
    putc(action, val);
}

// Write an integer in hexadecimal to the screen.
static void
puthex(u16 action, u32 val)
{
    putsinglehex(action, (val >> 28) & 0xf);
    putsinglehex(action, (val >> 24) & 0xf);
    putsinglehex(action, (val >> 20) & 0xf);
    putsinglehex(action, (val >> 16) & 0xf);
    putsinglehex(action, (val >> 12) & 0xf);
    putsinglehex(action, (val >> 8) & 0xf);
    putsinglehex(action, (val >> 4) & 0xf);
    putsinglehex(action, (val >> 0) & 0xf);
}

static inline int
isdigit(u8 c)
{
    return c - '0' < 10;
}

static void
bvprintf(u16 action, const char *fmt, va_list args)
{
    const char *s = fmt;
    for (;; s++) {
        char c = GET_VAR(CS, *(u8*)s);
        if (!c)
            break;
        if (c != '%') {
            putc(action, c);
            continue;
        }
        const char *n = s+1;
        for (;;) {
            c = GET_VAR(CS, *(u8*)n);
            if (!isdigit(c))
                break;
            n++;
        }
        if (c == 'l') {
            // Ignore long format indicator
            n++;
            c = GET_VAR(CS, *(u8*)n);
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
        case 'x':
            val = va_arg(args, s32);
            puthex(action, val);
            break;
        case 'c':
            val = va_arg(args, int);
            putc(action, val);
            break;
        case '.':
            // Hack to support "%.s" - meaning string on stack.
            if (GET_VAR(CS, *(u8*)(n+1)) != 's')
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
}

void
BX_PANIC(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    bvprintf(0, fmt, args);
    va_end(args);

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

static void
dump_regs(const char *fname, const char *type, struct bregs *regs)
{
    if (!regs) {
        dprintf(1, "%s %s: NULL\n", type, fname);
        return;
    }
    dprintf(1, "%s %s: a=%x b=%x c=%x d=%x si=%x di=%x\n"
            , type, fname, regs->eax, regs->ebx, regs->ecx, regs->edx
            , regs->esi, regs->edi);
    dprintf(1, "  ds=%x es=%x ip=%x cs=%x f=%x r=%p\n"
            , regs->ds, regs->es, regs->ip, regs->cs, regs->flags, regs);
}

void
__debug_isr(const char *fname)
{
    puts_cs(0, fname);
    putc(0, '\n');
}

// Function called on handler startup.
void
__debug_enter(const char *fname, struct bregs *regs)
{
    // XXX - implement run time suppression test
    dump_regs(fname, "enter", regs);
}

void
__debug_fail(const char *fname, struct bregs *regs)
{
    dump_regs(fname, "fail", regs);
}

void
__debug_stub(const char *fname, struct bregs *regs)
{
    dump_regs(fname, "stub", regs);
}
