// Coreboot interface support.
//
// Copyright (C) 2008,2009  Kevin O'Connor <kevin@koconnor.net>
//
// This file may be distributed under the terms of the GNU LGPLv3 license.

#include "config.h" // CONFIG_*
#include "malloc.h" // malloc_fseg
#include "output.h" // dprintf
#include "std/acpi.h" // struct rsdp_descriptor
#include "std/mptable.h" // MPTABLE_SIGNATURE
#include "std/pirtable.h" // struct pir_header
#include "std/smbios.h" // struct smbios_entry_point
#include "string.h" // memcpy
#include "util.h" // copy_table

static void
copy_pir(void *pos)
{
    struct pir_header *p = pos;
    if (p->signature != PIR_SIGNATURE)
        return;
    if (PirAddr)
        return;
    if (p->size < sizeof(*p))
        return;
    if (checksum(pos, p->size) != 0)
        return;
    void *newpos = malloc_fseg(p->size);
    if (!newpos) {
        warn_noalloc();
        return;
    }
    dprintf(1, "Copying PIR from %p to %p\n", pos, newpos);
    memcpy(newpos, pos, p->size);
    PirAddr = newpos;
}

static void
copy_mptable(void *pos)
{
    struct mptable_floating_s *p = pos;
    if (p->signature != MPTABLE_SIGNATURE)
        return;
    if (!p->physaddr)
        return;
    if (checksum(pos, sizeof(*p)) != 0)
        return;
    u32 length = p->length * 16;
    u16 mpclength = ((struct mptable_config_s *)p->physaddr)->length;
    struct mptable_floating_s *newpos = malloc_fseg(length + mpclength);
    if (!newpos) {
        warn_noalloc();
        return;
    }
    dprintf(1, "Copying MPTABLE from %p/%x to %p\n", pos, p->physaddr, newpos);
    memcpy(newpos, pos, length);
    newpos->physaddr = (u32)newpos + length;
    newpos->checksum -= checksum(newpos, sizeof(*newpos));
    memcpy((void*)newpos + length, (void*)p->physaddr, mpclength);
}

static int
get_acpi_rsdp_length(void *pos, unsigned size)
{
    struct rsdp_descriptor *p = pos;
    if (p->signature != RSDP_SIGNATURE)
        return -1;
    u32 length = 20;
    if (length > size)
        return -1;
    if (checksum(pos, length) != 0)
        return -1;
    if (p->revision > 1) {
        length = p->length;
        if (length > size)
            return -1;
        if (checksum(pos, length) != 0)
            return -1;
    }
    return length;
}

static void
copy_acpi_rsdp(void *pos)
{
    if (RsdpAddr)
        return;
    int length = get_acpi_rsdp_length(pos, -1);
    if (length < 0)
        return;
    void *newpos = malloc_fseg(length);
    if (!newpos) {
        warn_noalloc();
        return;
    }
    dprintf(1, "Copying ACPI RSDP from %p to %p\n", pos, newpos);
    memcpy(newpos, pos, length);
    RsdpAddr = newpos;
}

void
copy_smbios(void *pos)
{
    if (SMBiosAddr)
        return;
    struct smbios_entry_point *p = pos;
    if (memcmp(p->anchor_string, "_SM_", 4))
        return;
    if (checksum(pos, 0x10) != 0)
        return;
    if (memcmp(p->intermediate_anchor_string, "_DMI_", 5))
        return;
    if (checksum(pos+0x10, p->length-0x10) != 0)
        return;
    struct smbios_entry_point *newpos = malloc_fseg(p->length);
    if (!newpos) {
        warn_noalloc();
        return;
    }
    dprintf(1, "Copying SMBIOS entry point from %p to %p\n", pos, newpos);
    memcpy(newpos, pos, p->length);
    SMBiosAddr = newpos;
}

void
copy_table(void *pos)
{
    copy_pir(pos);
    copy_mptable(pos);
    copy_acpi_rsdp(pos);
    copy_smbios(pos);
}

void *find_acpi_rsdp(void)
{
    extern u8 zonefseg_start[], zonefseg_end[];
    unsigned long start = (unsigned long)zonefseg_start;
    unsigned long end = (unsigned long)zonefseg_end;
    unsigned long pos;

    for (pos = ALIGN(start, 0x10); pos <= ALIGN_DOWN(end, 0x10); pos += 0x10)
        if (get_acpi_rsdp_length((void *)pos, end - pos) >= 0)
            return (void *)pos;

    return NULL;
}
