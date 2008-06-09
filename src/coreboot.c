// Coreboot interface support.
//
// Copyright (C) 2008  Kevin O'Connor <kevin@koconnor.net>
//
// This file may be distributed under the terms of the GNU GPLv3 license.

#include "memmap.h" // add_e820
#include "util.h" // dprintf


/****************************************************************
 * Memory interface
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
        dprintf(1, "sig %p=%x\n", addr, cbh->signature);
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
static struct cb_memory *
find_cb_memory(struct cb_header *cbh)
{
    char *tbl = (char *)cbh + sizeof(*cbh);
    int i;
    for (i=0; i<cbh->table_entries; i++) {
        struct cb_memory *cbm = (struct cb_memory *)tbl;
        tbl += cbm->size;
        if (cbm->tag == CB_TAG_MEMORY)
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
    struct cb_memory *cbm = find_cb_memory(cbh);
    if (!cbm)
        goto fail;

    u64 maxram = 0;
    int i, count = MEM_RANGE_COUNT(cbm);
    for (i=0; i<count; i++) {
        struct cb_memory_range *m = &cbm->map[i];
        u32 type = m->type;
        if (type == CB_MEM_TABLE)
            type = E820_RESERVED;
        if ((type == E820_ACPI || type == E820_RAM)
            && (m->start + m->size) > maxram)
            maxram = m->start + m->size;
        add_e820(m->start, m->size, type);
    }

    // Ughh - coreboot likes to set a map at 0x0000-0x1000, but this
    // confuses grub.  So, override it.
    add_e820(0, 16*1024, E820_RAM);

    SET_EBDA(ram_size, maxram);
    return;

fail:
    // No table found..  Use 16Megs as a dummy value.
    dprintf(1, "Unable to find coreboot table!\n");
    SET_EBDA(ram_size, 16*1024*1024);
    add_e820(0, 16*1024*1024, E820_RAM);
    return;
}
