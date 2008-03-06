/*
 * smp.h
 * SMP support for BIOS.
 * 
 * Copyright (C) 2008  Nguyen Anh Quynh <aquynh@gmail.com>
 * 
 * This file may be distributed under the terms of the GNU GPLv3 license.
 */

#ifndef __SMP_H
#define __SMP_H

#define APIC_BASE    ((u8 *)0xfee00000)
#define APIC_ICR_LOW 0x300
#define APIC_SVR     0x0F0
#define APIC_ID      0x020
#define APIC_LVT3    0x370

#define APIC_ENABLED 0x0100

#define AP_BOOT_ADDR 0x10000

#define CPU_COUNT_ADDR 0xf000

#define MPTABLE_MAX_SIZE  0x00002000

#define CPUID_APIC (1 << 9)

extern int smp_cpus;

/* find the number of CPUs by launching a SIPI to them */
void smp_probe(void);

void mptable_init(void);

#endif
