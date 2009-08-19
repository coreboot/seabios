// Code for emulating a drive via high-memory accesses.
//
// Copyright (C) 2009  Kevin O'Connor <kevin@koconnor.net>
//
// This file may be distributed under the terms of the GNU LGPLv3 license.

#include "disk.h" // process_ramdisk_op
#include "util.h" // dprintf
#include "memmap.h" // add_e820
#include "biosvar.h" // GET_GLOBAL
#include "bregs.h" // struct bregs

#define RAMDISK_SECTOR_SIZE 512

void
ramdisk_setup()
{
    if (!CONFIG_COREBOOT_FLASH || !CONFIG_FLASH_FLOPPY)
        return;

    // Find image.
    struct cbfs_file *file = cbfs_findprefix("floppyimg/", NULL);
    if (!file)
        return;
    u32 size = cbfs_datasize(file);
    dprintf(3, "Found floppy file %s of size %d\n", cbfs_filename(file), size);
    int ftype = find_floppy_type(size);
    if (ftype < 0) {
        dprintf(3, "No floppy type found for ramdisk size\n");
        return;
    }

    // Allocate ram for image.
    struct e820entry *e = find_high_area(size);
    if (!e) {
        dprintf(3, "No ram for ramdisk\n");
        return;
    }
    u32 loc = e->start + e->size - size;
    add_e820(loc, size, E820_RESERVED);

    // Copy image into ram.
    cbfs_copyfile(file, (void*)loc, size);

    // Setup driver.
    dprintf(1, "Mapping CBFS floppy %s to addr %x\n", cbfs_filename(file), loc);
    addFloppy(loc, ftype, DTYPE_RAMDISK);
}

static int
ramdisk_op(struct disk_op_s *op, int iswrite)
{
    u32 offset = GET_GLOBAL(Drives.drives[op->driveid].cntl_id);
    offset += (u32)op->lba * RAMDISK_SECTOR_SIZE;
    u64 opd = GDT_DATA | GDT_LIMIT(0xfffff) | GDT_BASE((u32)op->buf_fl);
    u64 ramd = GDT_DATA | GDT_LIMIT(0xfffff) | GDT_BASE(offset);

    u64 gdt[6];
    if (iswrite) {
        gdt[2] = opd;
        gdt[3] = ramd;
    } else {
        gdt[2] = ramd;
        gdt[3] = opd;
    }

    // Call 0x1587 to copy data.
    struct bregs br;
    memset(&br, 0, sizeof(br));
    br.ah = 0x87;
    br.es = GET_SEG(SS);
    br.si = (u32)gdt;
    br.cx = op->count * RAMDISK_SECTOR_SIZE / 2;
    call16_int(0x15, &br);

    return DISK_RET_SUCCESS;
}

int
process_ramdisk_op(struct disk_op_s *op)
{
    if (!CONFIG_COREBOOT_FLASH || !CONFIG_FLASH_FLOPPY)
        return 0;

    switch (op->command) {
    case CMD_READ:
        return ramdisk_op(op, 0);
    case CMD_WRITE:
        return ramdisk_op(op, 1);
    case CMD_VERIFY:
    case CMD_FORMAT:
    case CMD_RESET:
        return DISK_RET_SUCCESS;
    default:
        op->count = 0;
        return DISK_RET_EPARAM;
    }
}
