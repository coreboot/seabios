// CPU count detection
//
// Copyright (C) 2008  Kevin O'Connor <kevin@koconnor.net>
// Copyright (C) 2006 Fabrice Bellard
//
// This file may be distributed under the terms of the GNU GPLv3 license.

#include "util.h" // dprintf
#include "config.h" // CONFIG_*
#include "cmos.h" // CMOS_BIOS_SMP_COUNT

#define CPUID_APIC (1 << 9)

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

u32 smp_cpus VAR16;
extern void smp_ap_boot_code();
ASM16(
    "  .global smp_ap_boot_code\n"
    "smp_ap_boot_code:\n"
    // Increment the cpu counter
    "  movw $" __stringify(SEG_BIOS) ", %ax\n"
    "  movw %ax, %ds\n"
    "  lock incl smp_cpus\n"
    // Halt the processor.
    "  jmp permanent_halt\n"
    );

/* find the number of CPUs by launching a SIPI to them */
int
smp_probe(void)
{
    if (smp_cpus)
        return smp_cpus;

    u32 eax, ebx, ecx, cpuid_features;
    cpuid(1, &eax, &ebx, &ecx, &cpuid_features);
    if (! (cpuid_features & CPUID_APIC)) {
        // No apic - only the main cpu is present.
        smp_cpus = 1;
        return 1;
    }

    // Init the counter.
    writel(&smp_cpus, 1);

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
        while (inb_cmos(CMOS_BIOS_SMP_COUNT) + 1 != readl(&smp_cpus))
            ;

    // Restore memory.
    *(u64*)BUILD_AP_BOOT_ADDR = old;

    u32 count = readl(&smp_cpus);
    dprintf(1, "Found %d cpu(s)\n", count);
    return count;
}

// Reset smp_cpus to zero (forces a recheck on reboots).
void
smp_probe_setup(void)
{
    smp_cpus = 0;
}
