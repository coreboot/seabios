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

static inline void cpu_relax(void)
{
    asm volatile("rep ; nop": : :"memory");
}

static inline void nop(void)
{
    asm volatile("nop");
}

static inline void hlt(void)
{
    asm volatile("hlt");
}

void *memset(void *s, int c, size_t n);
void *memcpy(void *d1, const void *s1, size_t len);

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

// Call a function with a specified register state.  Note that on
// return, the interrupt enable/disable flag may be altered.
static inline
void call16(struct bregs *callregs)
{
    asm volatile(
#ifdef MODE16
        "calll __call16\n"
#else
        "calll __call16_from32\n"
#endif
        : "+a" (callregs), "+m" (*callregs)
        :
        : "ebx", "ecx", "edx", "esi", "edi", "ebp", "cc");
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
        __call16_int((callregs), (u32)&irq_trampoline_ ##nr );  \
    } while (0)
#else
#include "../out/rom16.offset.auto.h"
#define call16_int(nr, callregs)                                \
    __call16_int((callregs), OFFSET_irq_trampoline_ ##nr )
#endif

// output.c
void BX_PANIC(const char *fmt, ...)
    __attribute__ ((format (printf, 1, 2)))
    __attribute__ ((noreturn));
void BX_INFO(const char *fmt, ...)
    __attribute__ ((format (printf, 1, 2)));
void printf(const char *fmt, ...)
    __attribute__ ((format (printf, 1, 2)));
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

void __set_fail(const char *fname, struct bregs *regs);
void __set_code_fail(const char *fname, struct bregs *regs, u8 code);

#define set_fail(regs) \
    __set_fail(__func__, (regs))
#define set_code_fail(regs, code)               \
    __set_code_fail(__func__, (regs), (code))

// kbd.c
void handle_15c2(struct bregs *regs);

// serial.c
void serial_setup();
void lpt_setup();

// clock.c
void timer_setup();
void handle_1583(struct bregs *regs);

// apm.c
void VISIBLE16 handle_1553(struct bregs *regs);

// pcibios.c
void handle_1ab1(struct bregs *regs);

// util.c
u8 checksum(u8 *far_data, u32 len);
void usleep(u32 count);

// rombios32.c
void rombios32_init(void);

// boot.c
void printf_bootdev(u16 bootdev);

// post_menu.c
void interactive_bootmenu();

#endif // util.h
