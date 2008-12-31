// Definitions for X86 bios disks.
//
// Copyright (C) 2008  Kevin O'Connor <kevin@koconnor.net>
//
// This file may be distributed under the terms of the GNU GPLv3 license.
#ifndef __DISK_H
#define __DISK_H

#include "types.h" // u8
#include "config.h" // CONFIG_*

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


/****************************************************************
 * Interface structs
 ****************************************************************/

// Bios disk structures.
struct int13ext_s {
    u8  size;
    u8  reserved;
    u16 count;
    u16 offset;
    u16 segment;
    u64 lba;
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
    u64 sector_count;
    u16 blksize;
    u16 dpte_offset;
    u16 dpte_segment;
    u16 key;
    u8  dpi_length;
    u8  reserved1;
    u16 reserved2;
    u8  host_bus[4];
    u8  iface_type[8];
    u64 iface_path;
    u64 device_path;
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
struct bregs;
void __disk_ret(const char *fname, int lineno, struct bregs *regs, u8 code);
#define disk_ret(regs, code) \
    __disk_ret(__func__, __LINE__, (regs), (code))


/****************************************************************
 * Global storage
 ****************************************************************/

struct chs_s {
    u16 heads;      // # heads
    u16 cylinders;  // # cylinders
    u16 spt;        // # sectors / track
};

struct ata_channel_s {
    u16 iobase1;      // IO Base 1
    u16 iobase2;      // IO Base 2
    u16 pci_bdf;
    u8  irq;          // IRQ
};

struct ata_device_s {
    u8  type;         // Detected type of ata (ata/atapi/none/unknown)
    u8  device;       // Detected type of attached devices (hd/cd/none)
    u8  removable;    // Removable device flag
    u8  mode;         // transfer mode : PIO 16/32 bits - IRQ - ISADMA - PCIDMA
    u16 blksize;      // block size

    u8  translation;  // type of translation
    struct chs_s  lchs;         // Logical CHS
    struct chs_s  pchs;         // Physical CHS

    u64 sectors;      // Total sectors count
};

struct ata_s {
    // ATA channels info
    struct ata_channel_s channels[CONFIG_MAX_ATA_INTERFACES];

    // ATA devices info
    struct ata_device_s  devices[CONFIG_MAX_ATA_DEVICES];
    //
    // map between bios hd/cd id and ata channels
    u8 cdcount;
    u8 idmap[2][CONFIG_MAX_ATA_DEVICES];
};


/****************************************************************
 * Function defs
 ****************************************************************/

// ata.c
extern struct ata_s ATA;

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
int cdrom_boot();


#endif // disk.h
