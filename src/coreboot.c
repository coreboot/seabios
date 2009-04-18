// Coreboot interface support.
//
// Copyright (C) 2008  Kevin O'Connor <kevin@koconnor.net>
//
// This file may be distributed under the terms of the GNU LGPLv3 license.

#include "memmap.h" // add_e820
#include "util.h" // dprintf
#include "pci.h" // struct pir_header
#include "acpi.h" // struct rsdp_descriptor
#include "mptable.h" // MPTABLE_SIGNATURE
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
    if (p->signature != MPTABLE_SIGNATURE)
        return;
    if (!p->physaddr)
        return;
    if (checksum(pos, sizeof(*p)) != 0)
        return;
    u32 length = p->length * 16;
    bios_table_cur_addr = ALIGN(bios_table_cur_addr, 16);
    u16 mpclength = ((struct mptable_config_s *)p->physaddr)->length;
    if (bios_table_cur_addr + length + mpclength > bios_table_end_addr) {
        dprintf(1, "No room to copy MPTABLE!\n");
        return;
    }
    dprintf(1, "Copying MPTABLE from %p/%x to %x\n"
            , pos, p->physaddr, bios_table_cur_addr);
    memcpy((void*)bios_table_cur_addr, pos, length);
    struct mptable_floating_s *newp = (void*)bios_table_cur_addr;
    newp->physaddr = bios_table_cur_addr + length;
    newp->checksum = 0;
    newp->checksum = -checksum(newp, sizeof(*newp));
    memcpy((void*)bios_table_cur_addr + length, (void*)p->physaddr, mpclength);
    bios_table_cur_addr += length + mpclength;
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

struct cb_forward {
    u32 tag;
    u32 size;
    u64 forward;
};

#define CB_TAG_FORWARD 0x11

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
static void
coreboot_fill_map()
{
    dprintf(3, "Attempting to find coreboot table\n");

    // Init variables set in coreboot table memory scan.
    PirOffset = 0;
    RsdpAddr = 0;

    // Find coreboot table.
    struct cb_header *cbh = find_cb_header(0, 0x1000);
    if (!cbh)
        goto fail;
    struct cb_forward *cbf = find_cb_subtable(cbh, CB_TAG_FORWARD);
    if (cbf) {
        dprintf(3, "Found coreboot table forwarder.\n");
        cbh = find_cb_header((char *)((u32)cbf->forward), 0x100);
        if (!cbh)
            goto fail;
    }
    dprintf(3, "Now attempting to find coreboot memory map\n");
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


/****************************************************************
 * Coreboot flash format
 ****************************************************************/

// XXX - optimize
#define ntohl(x) ((((x)&0xff)<<24) | (((x)&0xff00)<<8) | \
                  (((x)&0xff0000) >> 8) | (((x)&0xff000000) >> 24))
#define htonl(x) ntohl(x)

#define CBFS_HEADER_MAGIC 0x4F524243
#define CBFS_HEADPTR_ADDR 0xFFFFFFFc
#define CBFS_VERSION1 0x31313131

struct cbfs_header {
    u32 magic;
    u32 version;
    u32 romsize;
    u32 bootblocksize;
    u32 align;
    u32 offset;
    u32 pad[2];
} PACKED;

static struct cbfs_header *CBHDR;

static void
cbfs_setup()
{
    if (! CONFIG_COREBOOT_FLASH)
        return;

    CBHDR = *(void **)CBFS_HEADPTR_ADDR;
    if (CBHDR->magic != htonl(CBFS_HEADER_MAGIC)) {
        dprintf(1, "Unable to find CBFS (got %x not %x)\n"
                , CBHDR->magic, htonl(CBFS_HEADER_MAGIC));
        CBHDR = NULL;
        return;
    }

    dprintf(1, "Found CBFS header at %p\n", CBHDR);
}

#define CBFS_FILE_MAGIC 0x455649484352414cLL // LARCHIVE

struct cbfs_file {
    u64 magic;
    u32 len;
    u32 type;
    u32 checksum;
    u32 offset;
    char filename[0];
} PACKED;

static struct cbfs_file *
cbfs_search(struct cbfs_file *file)
{
    for (;;) {
        if (file < (struct cbfs_file *)(0xFFFFFFFF - ntohl(CBHDR->romsize)))
            return NULL;
        if (file->magic == CBFS_FILE_MAGIC)
            return file;
        file = (void*)file + ntohl(CBHDR->align);
    }
}

static struct cbfs_file *
cbfs_getfirst()
{
    if (! CBHDR)
        return NULL;
    return cbfs_search((void *)(0 - ntohl(CBHDR->romsize) + ntohl(CBHDR->offset)));
}

static struct cbfs_file *
cbfs_getnext(struct cbfs_file *file)
{
    file = (void*)file + ALIGN(ntohl(file->len) + ntohl(file->offset), ntohl(CBHDR->align));
    return cbfs_search(file);
}

static struct cbfs_file *
cbfs_findfile(const char *fname)
{
    if (! CONFIG_COREBOOT_FLASH)
        return NULL;

    dprintf(3, "Searching CBFS for %s\n", fname);
    struct cbfs_file *file;
    for (file = cbfs_getfirst(); file; file = cbfs_getnext(file)) {
        dprintf(3, "Found CBFS file %s\n", file->filename);
        if (strcmp(fname, file->filename) == 0)
            return file;
    }
    return NULL;
}

const char *
cbfs_findNprefix(const char *prefix, int n)
{
    if (! CONFIG_COREBOOT_FLASH)
        return NULL;

    dprintf(3, "Searching CBFS for prefix %s\n", prefix);
    int len = strlen(prefix);
    struct cbfs_file *file;
    for (file = cbfs_getfirst(); file; file = cbfs_getnext(file)) {
        dprintf(3, "Found CBFS file %s\n", file->filename);
        if (memcmp(prefix, file->filename, len) == 0) {
            if (n <= 0)
                return file->filename;
            n--;
        }
    }
    return NULL;
}

static char
getHex(u8 x)
{
    if (x <= 9)
        return '0' + x;
    return 'a' + x - 10;
}

static u32
hexify4(u16 x)
{
    return ((getHex(x&0xf) << 24)
            | (getHex((x>>4)&0xf) << 16)
            | (getHex((x>>8)&0xf) << 8)
            | (getHex(x>>12)));
}

void *
cb_find_optionrom(u32 vendev)
{
    if (! CONFIG_COREBOOT_FLASH)
        return NULL;

    char fname[17];
    // Ughh - poor man's sprintf of "pci%04x,%04x.rom"
    *(u32*)fname = 0x20696370; // "pci "
    *(u32*)&fname[3] = hexify4(vendev);
    fname[7] = ',';
    *(u32*)&fname[8] = hexify4(vendev >> 16);
    *(u32*)&fname[12] = 0x6d6f722e; // ".rom"
    fname[16] = '\0';

    struct cbfs_file *file = cbfs_findfile(fname);
    if (!file)
        return NULL;
    // Found it.
    dprintf(3, "Found rom at %p\n", (void*)file + ntohl(file->offset));
    return (void*)file + ntohl(file->offset);
}

struct cbfs_payload_segment {
    u32 type;
    u32 compression;
    u32 offset;
    u64 load_addr;
    u32 len;
    u32 mem_len;
} PACKED;

#define PAYLOAD_SEGMENT_BSS    0x20535342
#define PAYLOAD_SEGMENT_ENTRY  0x52544E45

#define CBFS_COMPRESS_NONE  0

struct cbfs_payload {
    struct cbfs_payload_segment segments[1];
};

void
cbfs_run_payload(const char *filename)
{
    dprintf(1, "Run %s\n", filename);
    struct cbfs_file *file = cbfs_findfile(filename);
    if (!file)
        return;
    struct cbfs_payload *pay = (void*)file + ntohl(file->offset);
    struct cbfs_payload_segment *seg = pay->segments;
    for (;;) {
        if (seg->compression != htonl(CBFS_COMPRESS_NONE)) {
            dprintf(1, "No support for compressed payloads (%x)\n"
                    , seg->compression);
            return;
        }
        void *src = (void*)pay + ntohl(seg->offset);
        void *dest = (void*)ntohl((u32)seg->load_addr);
        u32 src_len = ntohl(seg->len);
        u32 dest_len = ntohl(seg->mem_len);
        switch (seg->type) {
        case PAYLOAD_SEGMENT_BSS:
            dprintf(3, "BSS segment %d@%p\n", dest_len, dest);
            memset(dest, 0, dest_len);
            break;
        case PAYLOAD_SEGMENT_ENTRY: {
            dprintf(1, "Calling addr %p\n", dest);
            void (*func)() = dest;
            func();
            return;
        }
        default:
            dprintf(3, "Segment %x %d@%p -> %d@%p\n"
                    , seg->type, src_len, src, dest_len, dest);
            if (src_len > dest_len)
                src_len = dest_len;
            memcpy(dest, src, src_len);
            if (dest_len > src_len)
                memset(dest + src_len, 0, dest_len - src_len);
            break;
        }
        seg++;
    }
}

void
coreboot_setup(void)
{
    coreboot_fill_map();
    cbfs_setup();
}
