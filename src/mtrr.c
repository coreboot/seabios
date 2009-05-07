// Initialize MTRRs - mostly useful on KVM.
//
// Copyright (C) 2006 Fabrice Bellard
//
// This file may be distributed under the terms of the GNU LGPLv3 license.

#include "util.h" // dprintf
#include "biosvar.h" // GET_EBDA

#define MSR_MTRRcap                    0x000000fe
#define MSR_MTRRfix64K_00000           0x00000250
#define MSR_MTRRfix16K_80000           0x00000258
#define MSR_MTRRfix16K_A0000           0x00000259
#define MSR_MTRRfix4K_C0000            0x00000268
#define MSR_MTRRfix4K_C8000            0x00000269
#define MSR_MTRRfix4K_D0000            0x0000026a
#define MSR_MTRRfix4K_D8000            0x0000026b
#define MSR_MTRRfix4K_E0000            0x0000026c
#define MSR_MTRRfix4K_E8000            0x0000026d
#define MSR_MTRRfix4K_F0000            0x0000026e
#define MSR_MTRRfix4K_F8000            0x0000026f
#define MSR_MTRRdefType                0x000002ff

#define MTRRphysBase_MSR(reg) (0x200 + 2 * (reg))
#define MTRRphysMask_MSR(reg) (0x200 + 2 * (reg) + 1)

static u64 rdmsr(u32 index)
{
    u64 ret;
    asm ("rdmsr" : "=A"(ret) : "c"(index));
    return ret;
}

static void wrmsr(u32 index, u64 val)
{
    asm volatile ("wrmsr" : : "c"(index), "A"(val));
}

static void wrmsr_smp(u32 index, u64 val)
{
    // XXX - should run this on other CPUs also.
    wrmsr(index, val);
}

void mtrr_setup(void)
{
    if (! CONFIG_KVM)
        return;
    dprintf(3, "init mtrr\n");

    int i, vcnt, fix, wc;
    u32 ram_size = GET_GLOBAL(RamSize);
    u32 mtrr_cap;
    union {
        u8 valb[8];
        u64 val;
    } u;

    mtrr_cap = rdmsr(MSR_MTRRcap);
    vcnt = mtrr_cap & 0xff;
    fix = mtrr_cap & 0x100;
    wc = mtrr_cap & 0x400;
    if (!vcnt || !fix)
       return;
    u.val = 0;
    for (i = 0; i < 8; ++i)
        if (ram_size >= 65536 * (i + 1))
            u.valb[i] = 6;
    wrmsr_smp(MSR_MTRRfix64K_00000, u.val);
    u.val = 0;
    for (i = 0; i < 8; ++i)
        if (ram_size >= 65536 * 8 + 16384 * (i + 1))
            u.valb[i] = 6;
    wrmsr_smp(MSR_MTRRfix16K_80000, u.val);
    wrmsr_smp(MSR_MTRRfix16K_A0000, 0);
    wrmsr_smp(MSR_MTRRfix4K_C0000, 0);
    wrmsr_smp(MSR_MTRRfix4K_C8000, 0);
    wrmsr_smp(MSR_MTRRfix4K_D0000, 0);
    wrmsr_smp(MSR_MTRRfix4K_D8000, 0);
    wrmsr_smp(MSR_MTRRfix4K_E0000, 0);
    wrmsr_smp(MSR_MTRRfix4K_E8000, 0);
    wrmsr_smp(MSR_MTRRfix4K_F0000, 0);
    wrmsr_smp(MSR_MTRRfix4K_F8000, 0);
    /* Mark 3.5-4GB as UC, anything not specified defaults to WB */
    wrmsr_smp(MTRRphysBase_MSR(0), 0xe0000000ull | 0);
    wrmsr_smp(MTRRphysMask_MSR(0), ~(0x20000000ull - 1) | 0x800);
    wrmsr_smp(MSR_MTRRdefType, 0xc06);
}
