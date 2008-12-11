// Basic x86 asm functions and function defs.
//
// Copyright (C) 2008  Kevin O'Connor <kevin@koconnor.net>
//
// This file may be distributed under the terms of the GNU GPLv3 license.
#ifndef __UTIL_H
#define __UTIL_H

#include "types.h" // u32

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

static inline void wbinvd(void)
{
    asm volatile("wbinvd");
}

static inline void cpuid(u32 index, u32 *eax, u32 *ebx, u32 *ecx, u32 *edx)
{
    asm("cpuid"
        : "=a" (*eax), "=b" (*ebx), "=c" (*ecx), "=d" (*edx)
        : "0" (index));
}

static inline u64 rdtscll(void)
{
    u64 val;
    asm volatile("rdtsc" : "=A" (val));
    return val;
}

void *memset(void *s, int c, size_t n);
void *memcpy(void *d1, const void *s1, size_t len);
void *memcpy_far(void *far_d1, const void *far_s1, size_t len);
void *memmove(void *d, const void *s, size_t len);

struct bregs;
inline void call16(struct bregs *callregs);
inline void call16big(struct bregs *callregs);
inline void __call16_int(struct bregs *callregs, u16 offset);
#define call16_int(nr, callregs) do {                           \
        extern void irq_trampoline_ ##nr ();                    \
        __call16_int((callregs), (u32)&irq_trampoline_ ##nr );  \
    } while (0)

// output.c
void debug_serial_setup();
void BX_PANIC(const char *fmt, ...)
    __attribute__ ((format (printf, 1, 2)))
    __attribute__ ((noreturn));
void printf(const char *fmt, ...)
    __attribute__ ((format (printf, 1, 2)));
void __dprintf(const char *fmt, ...)
    __attribute__ ((format (printf, 1, 2)));
#define dprintf(lvl, fmt, args...) do {                         \
        if (CONFIG_DEBUG_LEVEL && (lvl) <= CONFIG_DEBUG_LEVEL)  \
            __dprintf((fmt) , ##args );                         \
    } while (0)
void __debug_enter(const char *fname, struct bregs *regs);
void __debug_stub(const char *fname, int lineno, struct bregs *regs);
void __debug_isr(const char *fname);
#define debug_enter(regs, lvl) do {                     \
        if ((lvl) && (lvl) <= CONFIG_DEBUG_LEVEL)       \
            __debug_enter(__func__, (regs));            \
    } while (0)
#define debug_isr(lvl) do {                             \
        if ((lvl) && (lvl) <= CONFIG_DEBUG_LEVEL)       \
            __debug_isr(__func__);                      \
    } while (0)
#define debug_stub(regs) \
    __debug_stub(__func__, __LINE__, (regs))

// kbd.c
void kbd_setup();
void handle_15c2(struct bregs *regs);

// mouse.c
void mouse_setup();

// system.c
void mathcp_setup();

// serial.c
void serial_setup();
void lpt_setup();

// clock.c
void timer_setup();
void ndelay(u32 count);
void udelay(u32 count);
void mdelay(u32 count);
void handle_1583(struct bregs *regs);
void handle_1586(struct bregs *regs);

// apm.c
void VISIBLE16 handle_1553(struct bregs *regs);

// pcibios.c
void handle_1ab1(struct bregs *regs);

// util.c
u8 checksum(u8 *far_data, u32 len);

// shadow.c
void make_bios_writable();
void make_bios_readonly();

// pciinit.c
void pci_bios_setup(void);

// smm.c
void smm_init();

// smpdetect.c
int smp_probe(void);
void smp_probe_setup(void);

// mptable.c
void mptable_init(void);

// smbios.c
void smbios_init(void);

// boot.c
void printf_bootdev(u16 bootdev);

// post_menu.c
void interactive_bootmenu();

// coreboot.c
void coreboot_fill_map();

// vgahooks.c
void handle_155f();

// optionroms.c
void vga_setup();
void optionrom_setup();

// resume.c
void init_dma();

// romlayout.S
void reset_vector() __attribute__ ((noreturn));

#endif // util.h
