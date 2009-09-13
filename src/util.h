// Basic x86 asm functions and function defs.
//
// Copyright (C) 2008,2009  Kevin O'Connor <kevin@koconnor.net>
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

#define CPUID_MSR (1 << 5)
#define CPUID_APIC (1 << 9)
#define CPUID_MTRR (1 << 12)
static inline void cpuid(u32 index, u32 *eax, u32 *ebx, u32 *ecx, u32 *edx)
{
    asm("cpuid"
        : "=a" (*eax), "=b" (*ebx), "=c" (*ecx), "=d" (*edx)
        : "0" (index));
}

static inline u64 rdmsr(u32 index)
{
    u64 ret;
    asm ("rdmsr" : "=A"(ret) : "c"(index));
    return ret;
}

static inline void wrmsr(u32 index, u64 val)
{
    asm volatile ("wrmsr" : : "c"(index), "A"(val));
}

static inline u64 rdtscll(void)
{
    u64 val;
    asm volatile("rdtsc" : "=A" (val));
    return val;
}

static inline u32 __ffs(u32 word)
{
    asm("bsf %1,%0"
        : "=r" (word)
        : "rm" (word));
    return word;
}

// GDT bit manipulation
#define GDT_BASE(v)  ((((u64)(v) & 0xff000000) << 32)           \
                      | (((u64)(v) & 0x00ffffff) << 16))
#define GDT_LIMIT(v) ((((u64)(v) & 0x000f0000) << 32)   \
                      | (((u64)(v) & 0x0000ffff) << 0))
#define GDT_CODE     (0x9bULL << 40) // Code segment - P,R,A bits also set
#define GDT_DATA     (0x93ULL << 40) // Data segment - W,A bits also set
#define GDT_B        (0x1ULL << 54)  // Big flag
#define GDT_G        (0x1ULL << 55)  // Granularity flag

#define call16_simpint(nr, peax, pflags) do {                           \
        ASSERT16();                                                     \
        asm volatile(                                                   \
            "stc\n"                                                     \
            "int %2\n"                                                  \
            "pushfl\n"                                                  \
            "popl %1\n"                                                 \
            "cli\n"                                                     \
            "cld"                                                       \
            : "+a"(*peax), "=r"(*pflags)                                \
            : "i"(nr)                                                   \
            : "cc", "memory");                                          \
    } while (0)

// util.c
inline u32 stack_hop(u32 eax, u32 edx, u32 ecx, void *func);
u8 checksum_far(u16 buf_seg, void *buf_far, u32 len);
u8 checksum(void *buf, u32 len);
int memcmp(const void *s1, const void *s2, size_t n);
size_t strlen(const char *s);
int strcmp(const char *s1, const char *s2);
inline void memset_far(u16 d_seg, void *d_far, u8 c, size_t len);
inline void memset16_far(u16 d_seg, void *d_far, u16 c, size_t len);
void *memset(void *s, int c, size_t n);
void *memcpy(void *d1, const void *s1, size_t len);
#if MODE16 == 0
#define memcpy __builtin_memcpy
#endif
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
void pci_setup(void);

// smm.c
void smm_init();

// smp.c
extern u32 CountCPUs;
void wrmsr_smp(u32 index, u64 val);
void smp_probe(void);
void smp_probe_setup(void);

// smbios.c
void smbios_init(void);

// coreboot.c
struct cbfs_file;
struct cbfs_file *cbfs_findprefix(const char *prefix, struct cbfs_file *last);
u32 cbfs_datasize(struct cbfs_file *file);
const char *cbfs_filename(struct cbfs_file *file);
int cbfs_copyfile(struct cbfs_file *file, void *dst, u32 maxlen);
int cbfs_copy_optionrom(void *dst, u32 maxlen, u32 vendev);
void cbfs_run_payload(struct cbfs_file *file);

void coreboot_copy_biostable();
void coreboot_setup();

// vgahooks.c
extern int VGAbdf;
void handle_155f();
void vgahook_setup(const char *vendor, const char *part);

// optionroms.c
void call_bcv(u16 seg, u16 ip);
void optionrom_setup();
void vga_setup();
void s3_resume_vga_init();
extern u32 RomEnd;

// resume.c
void init_dma();

// pnpbios.c
#define PNP_SIGNATURE 0x506e5024 // $PnP
u16 get_pnp_offset();
void pnp_setup();

// pmm.c
extern struct zone_s ZoneLow, ZoneHigh, ZoneFSeg, ZoneTmpLow, ZoneTmpHigh;
void *zone_malloc(struct zone_s *zone, u32 size, u32 align);
void malloc_setup();
void malloc_finalize();
void pmm_setup();
void pmm_finalize();
// Minimum alignment of malloc'd memory
#define MALLOC_MIN_ALIGN 16
// Helper functions for memory allocation.
static inline void *malloc_high(u32 size) {
    return zone_malloc(&ZoneHigh, size, MALLOC_MIN_ALIGN);
}
static inline void *malloc_fseg(u32 size) {
    return zone_malloc(&ZoneFSeg, size, MALLOC_MIN_ALIGN);
}
static inline void *memalign_tmphigh(u32 align, u32 size) {
    return zone_malloc(&ZoneTmpHigh, size, align);
}
static inline void *memalign_high(u32 align, u32 size) {
    return zone_malloc(&ZoneHigh, size, align);
}

// mtrr.c
void mtrr_setup(void);

// romlayout.S
void reset_vector() __attribute__ ((noreturn));

// misc.c
extern u8 BiosChecksum;

// mptable.c
extern int irq0override;

// version (auto generated file out/version.c)
extern const char VERSION[];

#endif // util.h
