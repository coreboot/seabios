// Basic x86 asm functions and function defs.
//
// Copyright (C) 2008  Kevin O'Connor <kevin@koconnor.net>
//
// This file may be distributed under the terms of the GNU LGPLv3 license.
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

// util.c
inline u32 stack_hop(u32 eax, u32 edx, u32 ecx, void *func);
u8 checksum_far(u16 buf_seg, void *buf_far, u32 len);
u8 checksum(void *buf, u32 len);
int memcmp(const void *s1, const void *s2, size_t n);
size_t strlen(const char *s);
int strcmp(const char *s1, const char *s2);
void *memset(void *s, int c, size_t n);
void *memcpy(void *d1, const void *s1, size_t len);
inline void memcpy_far(u16 d_seg, void *d_far
                       , u16 s_seg, const void *s_far, size_t len);
void *memmove(void *d, const void *s, size_t len);
char *strtcpy(char *dest, const char *src, size_t len);
struct bregs;
inline void call16(struct bregs *callregs);
inline void call16big(struct bregs *callregs);
inline void __call16_int(struct bregs *callregs, u16 offset);
#define call16_int(nr, callregs) do {                           \
        extern void irq_trampoline_ ##nr ();                    \
        __call16_int((callregs), (u32)&irq_trampoline_ ##nr );  \
    } while (0)
inline void call16_simpint(int nr, u32 *eax, u32 *flags);
void usleep(u32 usec);
int get_keystroke(int msec);

// output.c
void debug_serial_setup();
void panic(const char *fmt, ...)
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
void __debug_enter(struct bregs *regs, const char *fname);
void __debug_stub(struct bregs *regs, int lineno, const char *fname);
void __debug_isr(const char *fname);
#define debug_enter(regs, lvl) do {                     \
        if ((lvl) && (lvl) <= CONFIG_DEBUG_LEVEL)       \
            __debug_enter((regs), __func__);            \
    } while (0)
#define debug_isr(lvl) do {                             \
        if ((lvl) && (lvl) <= CONFIG_DEBUG_LEVEL)       \
            __debug_isr(__func__);                      \
    } while (0)
#define debug_stub(regs)                        \
    __debug_stub((regs), __LINE__, __func__)
void hexdump(void *d, int len);

// kbd.c
void kbd_setup();
void handle_15c2(struct bregs *regs);

// mouse.c
void mouse_setup();

// system.c
extern u32 RamSize;
extern u64 RamSizeOver4G;
void mathcp_setup();

// serial.c
void serial_setup();
void lpt_setup();

// clock.c
void timer_setup();
void ndelay(u32 count);
void udelay(u32 count);
void mdelay(u32 count);
u64 calc_future_tsc(u32 msecs);
void handle_1583(struct bregs *regs);
void handle_1586(struct bregs *regs);

// apm.c
void VISIBLE16 handle_1553(struct bregs *regs);

// pcibios.c
void handle_1ab1(struct bregs *regs);

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

// smbios.c
void smbios_init(void);

// coreboot.c
const char *cbfs_findNprefix(const char *prefix, int n);
void *cb_find_optionrom(u32 vendev);
void cbfs_run_payload(const char *filename);
void coreboot_setup();

// vgahooks.c
void handle_155f();

// optionroms.c
void call_bcv(u16 seg, u16 ip);
void vga_setup();
void optionrom_setup();

// resume.c
void init_dma();

// pnpbios.c
#define PNP_SIGNATURE 0x506e5024 // $PnP
u16 get_pnp_offset();
void pnp_setup();

// mtrr.c
void mtrr_setup(void);

// romlayout.S
void reset_vector() __attribute__ ((noreturn));

// misc.c
extern u8 BiosChecksum;

#endif // util.h
