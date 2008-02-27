// Basic x86 asm functions and function defs.
//
// Copyright (C) 2008  Kevin O'Connor <kevin@koconnor.net>
//
// This file may be distributed under the terms of the GNU GPLv3 license.

#include "ioport.h" // outb
#include "biosvar.h" // struct bregs

static inline void irq_disable(void) {
        asm volatile("cli": : :"memory");
}

static inline void irq_enable(void) {
        asm volatile("sti": : :"memory");
}

static inline unsigned long irq_save(void)
{
    unsigned long flags;
    asm volatile("pushfl ; popl %0" : "=g" (flags));
    irq_disable();
    return flags;
}

static inline void irq_restore(unsigned long flags)
{
    asm volatile("pushl %0 ; popfl" : : "g" (flags) : "memory", "cc");
}

static inline void nop(void)
{
    asm volatile("nop");
}

#define DEBUGF(fmt, args...)
#define BX_PANIC(fmt, args...)
#define BX_INFO(fmt, args...)

static inline void
memset(void *s, int c, size_t n)
{
    while (n)
        ((char *)s)[--n] = c;
}

static inline void
eoi_master_pic()
{
    outb(PIC1_IRQ5, PORT_PIC1);
}

static inline void
eoi_both_pics()
{
    outb(PIC2_IRQ13, PORT_PIC2);
    eoi_master_pic();
}

static inline
void call16(struct bregs *callregs)
{
    asm volatile(
        "pushfl\n"   // Save flags
        "pushal\n"   // Save registers
        "calll __call16\n"
        "popal\n"
        "popfl\n"
        : : "a" (callregs), "m" (*callregs));
}

// XXX - this is ugly.
#ifdef MODE16
#define call16_int(nr, callregs) do {           \
        struct bregs *__br = (callregs);        \
        extern void irq_trampoline_ ##nr ();    \
        __br->cs = 0xf000;                      \
        __br->ip = (u16)&irq_trampoline_ ##nr;  \
        call16(__br);                           \
    } while (0)
#else
#include "../out/rom16.offset.auto.h"
#define call16_int(nr, callregs) do {           \
        struct bregs *__br = (callregs);        \
        __br->cs = 0xf000;                      \
        __br->ip = OFFSET_irq_trampoline_ ##nr; \
        call16(__br);                           \
    } while (0)
#endif

// output.c
void bprintf(u16 action, const char *fmt, ...)
    __attribute__ ((format (printf, 2, 3)));
struct bregs;
void __debug_enter(const char *fname, struct bregs *regs);
void __debug_exit(const char *fname, struct bregs *regs);
#define debug_enter(regs) \
    __debug_enter(__func__, regs)
#define debug_exit(regs) \
    __debug_exit(__func__, regs)
#define printf(fmt, args...)                     \
    bprintf(1, fmt , ##args )

// kbd.c
void handle_15c2(struct bregs *regs);

// clock.c
void handle_1583(struct bregs *regs);

// Frequent bios return helper
#define RET_EUNSUPPORTED 0x86
static inline void
handle_ret(struct bregs *regs, u8 code)
{
    regs->ah = code;
    set_cf(regs, code);
}
