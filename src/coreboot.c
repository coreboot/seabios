// Coreboot interface support.
//
// Copyright (C) 2008  Kevin O'Connor <kevin@koconnor.net>
//
// This file may be distributed under the terms of the GNU GPLv3 license.

#include "memmap.h" // add_e820
#include "util.h" // dprintf
#include "pci.h" // struct pir_header
#include "acpi.h" // struct rsdp_descriptor
#include "biosvar.h" // GET_EBDA


/****************************************************************
 * BIOS table copying
 ****************************************************************/

static void
copy_pir(void *pos)
{
    struct pir_header *p = pos;
    if (p->signature != PIR_SIGNATURE)
        return;
    if (PirOffset)
        return;
    if (p->size < sizeof(*p))
        return;
    if (checksum(pos, p->size) != 0)
        return;
    bios_table_cur_addr = ALIGN(bios_table_cur_addr, 16);
    if (bios_table_cur_addr + p->size > bios_table_end_addr) {
        dprintf(1, "No room to copy PIR table!\n");
        return;
    }
    dprintf(1, "Copying PIR from %p to %x\n", pos, bios_table_cur_addr);
    memcpy((void*)bios_table_cur_addr, pos, p->size);
    PirOffset = bios_table_cur_addr - BUILD_BIOS_ADDR;
    bios_table_cur_addr += p->size;
}

static void
copy_mptable(void *pos)
{
    struct mptable_floating_s *p = pos;
    if (p->signature != MPTABLE_SIGNAURE)
        return;
    if (!p->physaddr)
        return;
    if (checksum(pos, sizeof(*p)) != 0)
        return;
    u32 length = p->length * 16;
    bios_table_cur_addr = ALIGN(bios_table_cur_addr, 16);
    if (bios_table_cur_addr + length > bios_table_end_addr) {
        dprintf(1, "No room to copy MPTABLE!\n");
        return;
    }
    dprintf(1, "Copying MPTABLE from %p to %x\n", pos, bios_table_cur_addr);
    memcpy((void*)bios_table_cur_addr, pos, length);
    bios_table_cur_addr += length;
}

static void
copy_acpi_rsdp(void *pos)
{
    if (RsdpAddr)
        return;
    struct rsdp_descriptor *p = pos;
    if (p->signature != RSDP_SIGNATURE)
        return;
    u32 length = 20;
    if (checksum(pos, length) != 0)
        return;
    if (p->revision > 1) {
        length = p->length;
        if (checksum(pos, length) != 0)
            return;
    }
    bios_table_cur_addr = ALIGN(bios_table_cur_addr, 16);
    if (bios_table_cur_addr + length > bios_table_end_addr) {
        dprintf(1, "No room to copy ACPI RSDP table!\n");
        return;
    }
    dprintf(1, "Copying ACPI RSDP from %p to %x\n", pos, bios_table_cur_addr);
    RsdpAddr = (void*)bios_table_cur_addr;
    memcpy(RsdpAddr, pos, length);
    bios_table_cur_addr += length;
}

// Attempt to find (and relocate) any standard bios tables found in a
// given address range.
static void
scan_tables(u32 start, u32 size)
{
    void *p = (void*)ALIGN(start, 16);
    void *end = (void*)start + size;
    for (; p<end; p += 16) {
        copy_pir(p);
        copy_mptable(p);
        copy_acpi_rsdp(p);
    }
}


/****************************************************************
 * Memory map
 ****************************************************************/

struct cb_header {
    u32 signature;
    u32 header_bytes;
    u32 header_checksum;
    u32 table_bytes;
    u32 table_checksum;
    u32 table_entries;
};

#define CB_SIGNATURE 0x4f49424C // "LBIO"

struct cb_memory_range {
    u64 start;
    u64 size;
    u32 type;
};

#define CB_MEM_TABLE    16

struct cb_memory {
    u32 tag;
    u32 size;
    struct cb_memory_range map[0];
};

#define CB_TAG_MEMORY 0x01

#define MEM_RANGE_COUNT(_rec) \
        (((_rec)->size - sizeof(*(_rec))) / sizeof((_rec)->map[0]))

static u16
ipchksum(char *buf, int count)
{
    u16 *p = (u16*)buf;
    u32 sum = 0;
    while (count > 1) {
        sum += *p++;
        count -= 2;
    }
    if (count)
        sum += *(u8*)p;
    sum = (sum >> 16) + (sum & 0xffff);
    sum += (sum >> 16);
    return ~sum;
}

// Try to locate the coreboot header in a given address range.
static struct cb_header *
find_cb_header(char *addr, int len)
{
    char *end = addr + len;
    for (; addr < end; addr += 16) {
        struct cb_header *cbh = (struct cb_header *)addr;
        if (cbh->signature != CB_SIGNATURE)
            continue;
        if (! cbh->table_bytes)
            continue;
        if (ipchksum(addr, sizeof(*cbh)) != 0)
            continue;
        if (ipchksum(addr + sizeof(*cbh), cbh->table_bytes)
            != cbh->table_checksum)
            continue;
        return cbh;
    }
    return NULL;
}

// Try to find the coreboot memory table in the given coreboot table.
static void *
find_cb_subtable(struct cb_header *cbh, u32 tag)
{
    char *tbl = (char *)cbh + sizeof(*cbh);
    int i;
    for (i=0; i<cbh->table_entries; i++) {
        struct cb_memory *cbm = (struct cb_memory *)tbl;
        tbl += cbm->size;
        if (cbm->tag == tag)
            return cbm;
    }
    return NULL;
}

// Populate max ram and e820 map info by scanning for a coreboot table.
void
coreboot_fill_map()
{
    dprintf(3, "Attempting to find coreboot table\n");
    struct cb_header *cbh = find_cb_header(0, 0x1000);
    if (!cbh)
        goto fail;
    struct cb_memory *cbm = find_cb_subtable(cbh, CB_TAG_MEMORY);
    if (!cbm)
        goto fail;

    u64 maxram = 0, maxram_over4G = 0;
    int i, count = MEM_RANGE_COUNT(cbm);
    for (i=0; i<count; i++) {
        struct cb_memory_range *m = &cbm->map[i];
        u32 type = m->type;
        if (type == CB_MEM_TABLE) {
            type = E820_RESERVED;
            scan_tables(m->start, m->size);
        } else if (type == E820_ACPI || type == E820_RAM) {
            u64 end = m->start + m->size;
            if (end > 0x100000000ull) {
                end -= 0x100000000ull;
                if (end > maxram_over4G)
                    maxram_over4G = end;
            } else if (end > maxram)
                maxram = end;
        }
        add_e820(m->start, m->size, type);
    }

    RamSize = maxram;
    RamSizeOver4G = maxram_over4G;

    // Ughh - coreboot likes to set a map at 0x0000-0x1000, but this
    // confuses grub.  So, override it.
    add_e820(0, 16*1024, E820_RAM);

    // XXX - just create dummy smbios table for now - should detect if
    // smbios/dmi table is found from coreboot and use that instead.
    smbios_init();

    return;

fail:
    // No table found..  Use 16Megs as a dummy value.
    dprintf(1, "Unable to find coreboot table!\n");
    RamSize = 16*1024*1024;
    RamSizeOver4G = 0;
    add_e820(0, 16*1024*1024, E820_RAM);
    return;
}
