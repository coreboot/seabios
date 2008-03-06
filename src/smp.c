/*
 * smp.c
 * SMP support for BIOS.
 * 
 * Copyright (C) 2008  Nguyen Anh Quynh <aquynh@gmail.com>
 * Copyright (C) 2002  MandrakeSoft S.A.
 * 
 * This file may be distributed under the terms of the GNU GPLv3 license.
 */

#include "config.h"
#include "hardware.h"
#include "smp.h"
#include "types.h"
#include "util.h"
#include "acpi.h" // ACPI_DATA_SIZE

int smp_cpus;

static inline void
writel(void *addr, u32 val)
{
    *(volatile u32 *)addr = val;
}

static inline void
writew(void *addr, u16 val)
{
    *(volatile u16 *)addr = val;
}

static inline u32
readl(const void *addr)
{
    return *(volatile const u32 *)addr;
}

static inline u16
readw(const void *addr)
{
    return *(volatile const u16 *)addr;
}

static void
delay_ms(int n)
{
    int i, j;
    for(i = 0; i < n; i++) {
#ifdef QEMU_SUPPORT
        /* approximative ! */
        for(j = 0; j < 1000000; j++);
#else
        {
          int r1, r2;
          j = 66;
          r1 = inb(0x61) & 0x10;
          do {
            r2 = inb(0x61) & 0x10;
            if (r1 != r2) {
              j--;
              r1 = r2;
            }
          } while (j > 0);
        }
#endif
    }
}

asm (
    ".globl smp_ap_boot_code_start \n"
    ".globl smp_ap_boot_code_end   \n"
    "                              \n"
    "  .code16                     \n"
    "smp_ap_boot_code_start:       \n"
    "  xorw %ax, %ax               \n"
    "  movw %ax, %ds               \n"
    "  //incw CPU_COUNT_ADDR       \n"
    "  incw 0xf000                 \n"
    "1:                            \n"
    "  hlt                         \n"
    "  jmp 1b                      \n"
    "smp_ap_boot_code_end:         \n"
    "  .code32                     \n"
    );

extern u8 smp_ap_boot_code_start;
extern u8 smp_ap_boot_code_end;

/* find the number of CPUs by launching a SIPI to them */
void
smp_probe(void)
{
    u32 val, sipi_vector;

    smp_cpus = 1;
    if (cpuid_features & CPUID_APIC) {

        /* enable local APIC */
        val = readl(APIC_BASE + APIC_SVR);
        val |= APIC_ENABLED;
        writel(APIC_BASE + APIC_SVR, val);

        writew((void *)CPU_COUNT_ADDR, 1);

        /* copy AP boot code */
        memcpy((void *)AP_BOOT_ADDR, &smp_ap_boot_code_start,
               &smp_ap_boot_code_end - &smp_ap_boot_code_start);

        /* broadcast SIPI */
        writel(APIC_BASE + APIC_ICR_LOW, 0x000C4500);
        sipi_vector = AP_BOOT_ADDR >> 12;
        writel(APIC_BASE + APIC_ICR_LOW, 0x000C4600 | sipi_vector);

        delay_ms(10);

        smp_cpus = readw((void *)CPU_COUNT_ADDR);
    }

    BX_INFO("Found %d cpu(s)\n", smp_cpus);
}

/****************************************************/
/* Multi Processor table init */

static void
putb(u8 **pp, int val)
{
    u8 *q;
    q = *pp;
    *q++ = val;
    *pp = q;
}

static void
putstr(u8 **pp, const char *str)
{
    u8 *q;
    q = *pp;
    while (*str)
        *q++ = *str++;
    *pp = q;
}

static void
putle16(u8 **pp, int val)
{
    u8 *q;
    q = *pp;
    *q++ = val;
    *q++ = val >> 8;
    *pp = q;
}

static void
putle32(u8 **pp, int val)
{
    u8 *q;
    q = *pp;
    *q++ = val;
    *q++ = val >> 8;
    *q++ = val >> 16;
    *q++ = val >> 24;
    *pp = q;
}

static int
mpf_checksum(const u8 *data, int len)
{
    int sum, i;

    sum = 0;
    for(i = 0; i < len; i++)
        sum += data[i];

    return sum & 0xff;
}

void
mptable_init(void)
{
    u8 *mp_config_table, *q, *float_pointer_struct;
    int ioapic_id, i, len;
    int mp_config_table_size;

#ifdef QEMU_SUPPORT
    if (smp_cpus <= 1)
        return;
#endif

#ifdef CONFIG_USE_EBDA_TABLES
    mp_config_table = (u8 *)(ram_size - ACPI_DATA_SIZE - MPTABLE_MAX_SIZE);
#else
    bios_table_cur_addr = align(bios_table_cur_addr, 16);
    mp_config_table = (u8 *)bios_table_cur_addr;
#endif
    q = mp_config_table;
    putstr(&q, "PCMP"); /* "PCMP signature */
    putle16(&q, 0); /* table length (patched later) */
    putb(&q, 4); /* spec rev */
    putb(&q, 0); /* checksum (patched later) */
#ifdef QEMU_SUPPORT
    putstr(&q, "QEMUCPU "); /* OEM id */
#else
    putstr(&q, "BOCHSCPU");
#endif
    putstr(&q, "0.1         "); /* vendor id */
    putle32(&q, 0); /* OEM table ptr */
    putle16(&q, 0); /* OEM table size */
    putle16(&q, smp_cpus + 18); /* entry count */
    putle32(&q, 0xfee00000); /* local APIC addr */
    putle16(&q, 0); /* ext table length */
    putb(&q, 0); /* ext table checksum */
    putb(&q, 0); /* reserved */

    for(i = 0; i < smp_cpus; i++) {
        putb(&q, 0); /* entry type = processor */
        putb(&q, i); /* APIC id */
        putb(&q, 0x11); /* local APIC version number */
        if (i == 0)
            putb(&q, 3); /* cpu flags: enabled, bootstrap cpu */
        else
            putb(&q, 1); /* cpu flags: enabled */
        putb(&q, 0); /* cpu signature */
        putb(&q, 6);
        putb(&q, 0);
        putb(&q, 0);
        putle16(&q, 0x201); /* feature flags */
        putle16(&q, 0);

        putle16(&q, 0); /* reserved */
        putle16(&q, 0);
        putle16(&q, 0);
        putle16(&q, 0);
    }

    /* isa bus */
    putb(&q, 1); /* entry type = bus */
    putb(&q, 0); /* bus ID */
    putstr(&q, "ISA   ");

    /* ioapic */
    ioapic_id = smp_cpus;
    putb(&q, 2); /* entry type = I/O APIC */
    putb(&q, ioapic_id); /* apic ID */
    putb(&q, 0x11); /* I/O APIC version number */
    putb(&q, 1); /* enable */
    putle32(&q, 0xfec00000); /* I/O APIC addr */

    /* irqs */
    for (i = 0; i < 16; i++) {
        putb(&q, 3); /* entry type = I/O interrupt */
        putb(&q, 0); /* interrupt type = vectored interrupt */
        putb(&q, 0); /* flags: po=0, el=0 */
        putb(&q, 0);
        putb(&q, 0); /* source bus ID = ISA */
        putb(&q, i); /* source bus IRQ */
        putb(&q, ioapic_id); /* dest I/O APIC ID */
        putb(&q, i); /* dest I/O APIC interrupt in */
    }
    /* patch length */
    len = q - mp_config_table;
    mp_config_table[4] = len;
    mp_config_table[5] = len >> 8;
    mp_config_table[7] = -mpf_checksum(mp_config_table, q - mp_config_table);

    mp_config_table_size = q - mp_config_table;

#ifndef CONFIG_USE_EBDA_TABLES
    bios_table_cur_addr += mp_config_table_size;
#endif

    /* floating pointer structure */
#ifdef CONFIG_USE_EBDA_TABLES
    ebda_cur_addr = align(ebda_cur_addr, 16);
    float_pointer_struct = (u8 *)ebda_cur_addr;
#else
    bios_table_cur_addr = align(bios_table_cur_addr, 16);
    float_pointer_struct = (u8 *)bios_table_cur_addr;
#endif

    q = float_pointer_struct;
    putstr(&q, "_MP_");
    /* pointer to MP config table */
    putle32(&q, (unsigned long)mp_config_table);

    putb(&q, 1); /* length in 16 byte units */
    putb(&q, 4); /* MP spec revision */
    putb(&q, 0); /* checksum (patched later) */
    putb(&q, 0); /* MP feature byte 1 */

    putb(&q, 0);
    putb(&q, 0);
    putb(&q, 0);
    putb(&q, 0);
    float_pointer_struct[10] =
        -mpf_checksum(float_pointer_struct, q - float_pointer_struct);

#ifdef CONFIG_USE_EBDA_TABLES
    ebda_cur_addr += (q - float_pointer_struct);
#else
    bios_table_cur_addr += (q - float_pointer_struct);
#endif

    BX_INFO("MP table addr=0x%08lx MPC table addr=0x%08lx size=0x%x\n",
            (unsigned long)float_pointer_struct,
            (unsigned long)mp_config_table,
            mp_config_table_size);
}
