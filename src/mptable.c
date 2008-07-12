// MPTable generation (on emulators)
//
// Copyright (C) 2008  Kevin O'Connor <kevin@koconnor.net>
// Copyright (C) 2006 Fabrice Bellard
//
// This file may be distributed under the terms of the GNU GPLv3 license.

#include "util.h" // dprintf
#include "memmap.h" // bios_table_cur_addr
#include "config.h" // CONFIG_*

static void putb(u8 **pp, int val)
{
    u8 *q;
    q = *pp;
    *q++ = val;
    *pp = q;
}

static void putstr(u8 **pp, const char *str)
{
    u8 *q;
    q = *pp;
    while (*str)
        *q++ = *str++;
    *pp = q;
}

static void putle16(u8 **pp, int val)
{
    u8 *q;
    q = *pp;
    *q++ = val;
    *q++ = val >> 8;
    *pp = q;
}

static void putle32(u8 **pp, int val)
{
    u8 *q;
    q = *pp;
    *q++ = val;
    *q++ = val >> 8;
    *q++ = val >> 16;
    *q++ = val >> 24;
    *pp = q;
}

void
mptable_init(void)
{
    if (! CONFIG_MPTABLE)
        return;

    u8 *mp_config_table, *q, *float_pointer_struct;
    int ioapic_id, i, len;
    int mp_config_table_size;

    int smp_cpus = smp_probe();
    if (CONFIG_QEMU && smp_cpus <= 1)
        return;

    bios_table_cur_addr = ALIGN(bios_table_cur_addr, 16);
    mp_config_table = (u8 *)bios_table_cur_addr;
    q = mp_config_table;
    putstr(&q, "PCMP"); /* "PCMP signature */
    putle16(&q, 0); /* table length (patched later) */
    putb(&q, 4); /* spec rev */
    putb(&q, 0); /* checksum (patched later) */
    if (CONFIG_QEMU)
        putstr(&q, "QEMUCPU "); /* OEM id */
    else
        putstr(&q, "BOCHSCPU");
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
    for(i = 0; i < 16; i++) {
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

    mp_config_table[7] = -checksum(mp_config_table, q - mp_config_table);

    mp_config_table_size = q - mp_config_table;

    bios_table_cur_addr += mp_config_table_size;

    /* floating pointer structure */
    bios_table_cur_addr = ALIGN(bios_table_cur_addr, 16);
    float_pointer_struct = (u8 *)bios_table_cur_addr;
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
    float_pointer_struct[10] = -checksum(float_pointer_struct
                                         , q - float_pointer_struct);
    bios_table_cur_addr += (q - float_pointer_struct);
    dprintf(1, "MP table addr=0x%08lx MPC table addr=0x%08lx size=0x%x\n",
            (unsigned long)float_pointer_struct,
            (unsigned long)mp_config_table,
            mp_config_table_size);
}
