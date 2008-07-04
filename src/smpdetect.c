// CPU count detection
//
// Copyright (C) 2008  Kevin O'Connor <kevin@koconnor.net>
// Copyright (C) 2006 Fabrice Bellard
//
// This file may be distributed under the terms of the GNU GPLv3 license.

#include "util.h" // dprintf
#include "config.h" // CONFIG_*

#define CPUID_APIC (1 << 9)

#define APIC_BASE    ((u8 *)0xfee00000)
#define APIC_ICR_LOW 0x300
#define APIC_SVR     0x0F0
#define APIC_ID      0x020
#define APIC_LVT3    0x370

#define APIC_ENABLED 0x0100

static inline void writel(void *addr, u32 val)
{
    *(volatile u32 *)addr = val;
}

static inline void writew(void *addr, u16 val)
{
    *(volatile u16 *)addr = val;
}

static inline void writeb(void *addr, u8 val)
{
    *(volatile u8 *)addr = val;
}

static inline u32 readl(const void *addr)
{
    return *(volatile const u32 *)addr;
}

static inline u16 readw(const void *addr)
{
    return *(volatile const u16 *)addr;
}

static inline u8 readb(const void *addr)
{
    return *(volatile const u8 *)addr;
}

asm(
    ".globl smp_ap_boot_code_start\n"
    ".globl smp_ap_boot_code_end\n"
    "  .code16\n"

    "smp_ap_boot_code_start:\n"
    "  xor %ax, %ax\n"
    "  mov %ax, %ds\n"
    "  incw " __stringify(BUILD_CPU_COUNT_ADDR) "\n"
    "1:\n"
    "  hlt\n"
    "  jmp 1b\n"
    "smp_ap_boot_code_end:\n"

    "  .code32\n"
    );

extern u8 smp_ap_boot_code_start;
extern u8 smp_ap_boot_code_end;

static int smp_cpus;

/* find the number of CPUs by launching a SIPI to them */
int
smp_probe(void)
{
    if (smp_cpus)
        return smp_cpus;

    smp_cpus = 1;

    u32 eax, ebx, ecx, cpuid_features;
    cpuid(1, &eax, &ebx, &ecx, &cpuid_features);
    if (cpuid_features & CPUID_APIC) {
        /* enable local APIC */
        u32 val = readl(APIC_BASE + APIC_SVR);
        val |= APIC_ENABLED;
        writel(APIC_BASE + APIC_SVR, val);

        writew((void *)BUILD_CPU_COUNT_ADDR, 1);
        /* copy AP boot code */
        memcpy((void *)BUILD_AP_BOOT_ADDR, &smp_ap_boot_code_start,
               &smp_ap_boot_code_end - &smp_ap_boot_code_start);

        /* broadcast SIPI */
        writel(APIC_BASE + APIC_ICR_LOW, 0x000C4500);
        u32 sipi_vector = BUILD_AP_BOOT_ADDR >> 12;
        writel(APIC_BASE + APIC_ICR_LOW, 0x000C4600 | sipi_vector);

        usleep(10*1000);

        smp_cpus = readw((void *)BUILD_CPU_COUNT_ADDR);
    }
    dprintf(1, "Found %d cpu(s)\n", smp_cpus);

    return smp_cpus;
}
