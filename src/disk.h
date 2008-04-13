// Definitions for X86 bios disks.
//
// Copyright (C) 2008  Kevin O'Connor <kevin@koconnor.net>
//
// This file may be distributed under the terms of the GNU GPLv3 license.
#ifndef __DISK_H
#define __DISK_H

#include "ioport.h" // outb
#include "biosvar.h" // struct bregs
#include "util.h" // set_code_fail

#define DISK_RET_SUCCESS       0x00
#define DISK_RET_EPARAM        0x01
#define DISK_RET_EADDRNOTFOUND 0x02
#define DISK_RET_EWRITEPROTECT 0x03
#define DISK_RET_ECHANGED      0x06
#define DISK_RET_EBOUNDARY     0x09
#define DISK_RET_EBADTRACK     0x0c
#define DISK_RET_ECONTROLLER   0x20
#define DISK_RET_ETIMEOUT      0x80
#define DISK_RET_ENOTLOCKED    0xb0
#define DISK_RET_ELOCKED       0xb1
#define DISK_RET_ENOTREMOVABLE 0xb2
#define DISK_RET_ETOOMANYLOCKS 0xb4
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
} PACKED;

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
} PACKED;

#define GET_INT13DPT(regs,var)                                          \
    GET_FARVAR((regs)->ds, ((struct int13dpt_s*)((regs)->si+0))->var)
#define SET_INT13DPT(regs,var,val)                                      \
    SET_FARVAR((regs)->ds, ((struct int13dpt_s*)((regs)->si+0))->var, (val))

// Floppy "Disk Base Table"
struct floppy_dbt_s {
    u8 specify1;
    u8 specify2;
    u8 shutoff_ticks;
    u8 bps_code;
    u8 sectors;
    u8 interblock_len;
    u8 data_len;
    u8 gap_len;
    u8 fill_byte;
    u8 settle_time;
    u8 startup_time;
} PACKED;

struct floppy_ext_dbt_s {
    struct floppy_dbt_s dbt;
    // Extra fields
    u8 max_track;
    u8 data_rate;
    u8 drive_type;
} PACKED;

// Helper function for setting up a return code.
void __disk_ret(const char *fname, struct bregs *regs, u8 code);
#define disk_ret(regs, code) \
    __disk_ret(__func__, (regs), (code))

// floppy.c
extern struct floppy_ext_dbt_s diskette_param_table2;
void floppy_drive_setup();
void floppy_13(struct bregs *regs, u8 drive);
void floppy_tick();

// disk.c
void disk_13(struct bregs *regs, u8 device);
void disk_13XX(struct bregs *regs, u8 device);

// cdrom.c
int cdrom_read_emu(u16 device, u32 lba, u32 count, void *far_buffer);
void cdrom_13(struct bregs *regs, u8 device);
void cdemu_13(struct bregs *regs);
void cdemu_134b(struct bregs *regs);
u16 cdrom_boot();


#endif // disk.h
