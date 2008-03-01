// Raw screen writing code.
//
// Copyright (C) 2008  Kevin O'Connor <kevin@koconnor.net>
//
// This file may be distributed under the terms of the GNU GPLv3 license.

#include <stdarg.h> // va_list

#include "farptr.h" // GET_VAR
#include "util.h" // bprintf
#include "biosvar.h" // struct bregs

static void
screenc(u8 c)
{
    struct bregs br;
    memset(&br, 0, sizeof(br));
    br.ah = 0x0e;
    br.al = c;
    call16_int(0x10, &br);
}

// XXX
#define PORT_DEBUG  0x403

// Write a charcter to the framebuffer.
static void
putc(u16 action, char c)
{
    outb(c, PORT_DEBUG);
    if (action) {
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
        char c = GET_VAR(CS, (u8)*s);
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

void
bprintf(u16 action, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    const char *s = fmt;
    for (;; s++) {
        char c = GET_VAR(CS, (u8)*s);
        if (!c)
            break;
        if (c != '%') {
            putc(action, c);
            continue;
        }
        const char *n = s+1;
        c = GET_VAR(CS, (u8)*n);
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
        case 'x':
            val = va_arg(args, s32);
            puthex(action, val);
            break;
        case 'c':
            val = va_arg(args, int);
            putc(action, val);
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
    va_end(args);
}

static void
dump_regs(const char *fname, const char *type, struct bregs *regs)
{
    if (!regs) {
        bprintf(0, "%s %s: NULL\n", type, fname);
        return;
    }
    bprintf(0, "%s %s: a=%x b=%x c=%x d=%x si=%x di=%x\n"
            , type, fname, regs->eax, regs->ebx, regs->ecx, regs->edx
            , regs->esi, regs->edi);
    bprintf(0, "  ds=%x es=%x bp=%x sp=%x ip=%x cs=%x f=%x\n"
            , regs->ds, regs->es, regs->ebp, regs->esp
            , regs->ip, regs->cs, regs->flags);
}

// Function called on handler startup.
void
__debug_enter(const char *fname, struct bregs *regs)
{
    // XXX - implement run time suppression test
    dump_regs(fname, "enter", regs);
}

void
__debug_exit(const char *fname, struct bregs *regs)
{
    if (! (regs->flags & F_CF))
        return;
    dump_regs(fname, "exit", regs);
}

void
__debug_stub(const char *fname, struct bregs *regs)
{
    dump_regs(fname, "stub", regs);
}
