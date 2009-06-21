// CPU count detection
//
// Copyright (C) 2008  Kevin O'Connor <kevin@koconnor.net>
// Copyright (C) 2006 Fabrice Bellard
//
// This file may be distributed under the terms of the GNU LGPLv3 license.

#include "util.h" // dprintf
#include "config.h" // CONFIG_*
#include "cmos.h" // CMOS_BIOS_SMP_COUNT
#include "farptr.h" // ASSERT32

#define APIC_ICR_LOW ((u8*)BUILD_APIC_ADDR + 0x300)
#define APIC_SVR     ((u8*)BUILD_APIC_ADDR + 0x0F0)

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

struct { u32 ecx, eax, edx; } smp_mtrr[16] VAR16_32;
u32 smp_mtrr_count VAR16_32;

void
wrmsr_smp(u32 index, u64 val)
{
    wrmsr(index, val);
    if (smp_mtrr_count >= ARRAY_SIZE(smp_mtrr))
        return;
    smp_mtrr[smp_mtrr_count].ecx = index;
    smp_mtrr[smp_mtrr_count].eax = val;
    smp_mtrr[smp_mtrr_count].edx = val >> 32;
    smp_mtrr_count++;
}

u32 CountCPUs VAR16_32;
extern void smp_ap_boot_code();
ASM16(
    "  .global smp_ap_boot_code\n"
    "smp_ap_boot_code:\n"

    // Setup data segment
    "  movw $" __stringify(SEG_BIOS) ", %ax\n"
    "  movw %ax, %ds\n"

    // MTRR setup
    "  movl $smp_mtrr, %esi\n"
    "  movl smp_mtrr_count, %ebx\n"
    "1:testl %ebx, %ebx\n"
    "  jz 2f\n"
    "  movl 0(%esi), %ecx\n"
    "  movl 4(%esi), %eax\n"
    "  movl 8(%esi), %edx\n"
    "  wrmsr\n"
    "  addl $12, %esi\n"
    "  decl %ebx\n"
    "  jmp 1b\n"
    "2:\n"

    // Increment the cpu counter
    "  lock incl CountCPUs\n"

    // Halt the processor.
    "1:hlt\n"
    "  jmp 1b\n"
    );

// find and initialize the CPUs by launching a SIPI to them
void
smp_probe(void)
{
    ASSERT32();
    u32 eax, ebx, ecx, cpuid_features;
    cpuid(1, &eax, &ebx, &ecx, &cpuid_features);
    if (! (cpuid_features & CPUID_APIC)) {
        // No apic - only the main cpu is present.
        CountCPUs= 1;
        return;
    }

    // Init the counter.
    writel(&CountCPUs, 1);

    // Setup jump trampoline to counter code.
    u64 old = *(u64*)BUILD_AP_BOOT_ADDR;
    // ljmpw $SEG_BIOS, $(smp_ap_boot_code - BUILD_BIOS_ADDR)
    u64 new = (0xea | ((u64)SEG_BIOS<<24)
               | (((u32)smp_ap_boot_code - BUILD_BIOS_ADDR) << 8));
    *(u64*)BUILD_AP_BOOT_ADDR = new;

    // enable local APIC
    u32 val = readl(APIC_SVR);
    writel(APIC_SVR, val | APIC_ENABLED);

    // broadcast SIPI
    writel(APIC_ICR_LOW, 0x000C4500);
    u32 sipi_vector = BUILD_AP_BOOT_ADDR >> 12;
    writel(APIC_ICR_LOW, 0x000C4600 | sipi_vector);

    // Wait for other CPUs to process the SIPI.
    if (CONFIG_COREBOOT)
        mdelay(10);
    else
        while (inb_cmos(CMOS_BIOS_SMP_COUNT) + 1 != readl(&CountCPUs))
            ;

    // Restore memory.
    *(u64*)BUILD_AP_BOOT_ADDR = old;

    dprintf(1, "Found %d cpu(s)\n", readl(&CountCPUs));
}

// Reset variables to zero
void
smp_probe_setup(void)
{
    CountCPUs = 0;
    smp_mtrr_count = 0;
}
