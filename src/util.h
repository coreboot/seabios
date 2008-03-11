// Basic x86 asm functions and function defs.
//
// Copyright (C) 2008  Kevin O'Connor <kevin@koconnor.net>
//
// This file may be distributed under the terms of the GNU GPLv3 license.
#ifndef __UTIL_H
#define __UTIL_H

#include "ioport.h" // outb
#include "biosvar.h" // struct bregs

static inline void irq_disable(void)
{
    asm volatile("cli": : :"memory");
}

static inline void irq_enable(void)
{
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

static inline void hlt(void)
{
    asm volatile("hlt");
}

// XXX - move this to a c file and use PANIC PORT.
#define BX_PANIC(fmt, args...) do { \
        bprintf(0, fmt , ##args);   \
        for (;;)                    \
            hlt();                  \
    } while (0)

#define BX_INFO(fmt, args...) bprintf(0, fmt , ##args)

static inline void
memset(void *s, int c, size_t n)
{
    while (n)
        ((char *)s)[--n] = c;
}

static inline void *
memcpy(void *d1, const void *s1, size_t len)
{
    u8 *d = d1;
    const u8 *s = s1;

    while (len--) {
        *d++ = *s++;
    }
    return d1;
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
        "pushl %%ebp\n" // Save state
        "pushfl\n"
#ifdef MODE16
        "calll __call16\n"
#else
        "calll __call16_from32\n"
#endif
        "popfl\n"       // Restore state
        "popl %%ebp\n"
        : "+a" (callregs), "+m" (*callregs)
        :
        : "ebx", "ecx", "edx", "esi", "edi");
}

static inline
void __call16_int(struct bregs *callregs, u16 offset)
{
    callregs->cs = SEG_BIOS;
    callregs->ip = offset;
    call16(callregs);
}

#ifdef MODE16
#define call16_int(nr, callregs) do {                           \
        extern void irq_trampoline_ ##nr ();                    \
        __call16_int((callregs), (u16)&irq_trampoline_ ##nr );  \
    } while (0)
#else
#include "../out/rom16.offset.auto.h"
#define call16_int(nr, callregs)                                \
    __call16_int((callregs), OFFSET_irq_trampoline_ ##nr )
#endif

// output.c
void bprintf(u16 action, const char *fmt, ...)
    __attribute__ ((format (printf, 2, 3)));
void __debug_enter(const char *fname, struct bregs *regs);
void __debug_fail(const char *fname, struct bregs *regs);
void __debug_stub(const char *fname, struct bregs *regs);
void __debug_isr(const char *fname);
#define debug_enter(regs) \
    __debug_enter(__func__, regs)
#define debug_stub(regs) \
    __debug_stub(__func__, regs)
#define debug_isr(regs) \
    __debug_isr(__func__)
#define printf(fmt, args...)                     \
    bprintf(1, fmt , ##args )

// Frequently used return codes
#define RET_EUNSUPPORTED 0x86
static inline void
set_success(struct bregs *regs)
{
    set_cf(regs, 0);
}

static inline void
set_code_success(struct bregs *regs)
{
    regs->ah = 0;
    set_cf(regs, 0);
}

#define set_fail(regs) do {                     \
        __debug_fail(__func__, (regs));         \
        set_cf((regs), 1);                      \
    } while (0)

#define set_code_fail(regs, code) do {          \
        set_fail(regs);                         \
        (regs)->ah = (code);                    \
    } while (0)

// kbd.c
void handle_15c2(struct bregs *regs);

// clock.c
void handle_1583(struct bregs *regs);

// apm.c
void VISIBLE16 handle_1553(struct bregs *regs);

// util.c
void usleep(u32 count);

// rombios32.c
void rombios32_init(void);

#endif // util.h
