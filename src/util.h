// Basic x86 asm functions and function defs.
//
// Copyright (C) 2008  Kevin O'Connor <kevin@koconnor.net>
//
// This file may be distributed under the terms of the GNU GPLv3 license.

#include "ioport.h" // outb

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

#define DEBUGF(fmt, args...)
#define BX_PANIC(fmt, args...)
#define BX_INFO(fmt, args...)

static inline void
memset(void *s, int c, size_t n)
{
    while (n)
        ((char *)s)[n--] = c;
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
    bprintf(0, fmt , ##args )

// kbd.c
void handle_15c2(struct bregs *regs);
