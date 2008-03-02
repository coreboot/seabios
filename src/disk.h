// Definitions for X86 bios disks.
//
// Copyright (C) 2008  Kevin O'Connor <kevin@koconnor.net>
//
// This file may be distributed under the terms of the GNU GPLv3 license.
#ifndef __DISK_H
#define __DISK_H

#include "ioport.h" // outb
#include "biosvar.h" // struct bregs

#define DISK_RET_SUCCESS       0x00
#define DISK_RET_EPARAM        0x01
#define DISK_RET_ECHANGED      0x06
#define DISK_RET_EBOUNDARY     0x09
#define DISK_RET_EBADTRACK     0x0c
#define DISK_RET_ECONTROLLER   0x20
#define DISK_RET_ETIMEOUT      0x80
#define DISK_RET_ENOTREMOVABLE 0xb2
#define DISK_RET_EMEDIA        0xC0
#define DISK_RET_ENOTREADY     0xAA

// Bios disk structures.
struct int13ext_s {
    u8  size;
    u8  reserved;
    u16 count;
    u16 offset;
    u16 segment;
    u32 lba1;
    u32 lba2;
};

#define GET_INT13EXT(regs,var)                                          \
    GET_FARVAR((regs)->ds, ((struct int13ext_s*)((regs)->si+0))->var)
#define SET_INT13EXT(regs,var,val)                                      \
    SET_FARVAR((regs)->ds, ((struct int13ext_s*)((regs)->si+0))->var, (val))

// Disk Physical Table definition
struct int13dpt_s {
    u16 size;
    u16 infos;
    u32 cylinders;
    u32 heads;
    u32 spt;
    u32 sector_count1;
    u32 sector_count2;
    u16 blksize;
    u16 dpte_offset;
    u16 dpte_segment;
    u16 key;
    u8  dpi_length;
    u8  reserved1;
    u16 reserved2;
    u8  host_bus[4];
    u8  iface_type[8];
    u8  iface_path[8];
    u8  device_path[8];
    u8  reserved3;
    u8  checksum;
};

#define GET_INT13DPT(regs,var)                                          \
    GET_FARVAR((regs)->ds, ((struct int13dpt_s*)((regs)->si+0))->var)
#define SET_INT13DPT(regs,var,val)                                      \
    SET_FARVAR((regs)->ds, ((struct int13dpt_s*)((regs)->si+0))->var, (val))


// floppy.c
void floppy_13(struct bregs *regs, u8 drive);
void floppy_tick();

#endif // disk.h
