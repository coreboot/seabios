// 16bit code to access hard drives.
//
// Copyright (C) 2008  Kevin O'Connor <kevin@koconnor.net>
// Copyright (C) 2002  MandrakeSoft S.A.
//
// This file may be distributed under the terms of the GNU GPLv3 license.

#include "disk.h" // floppy_13
#include "biosvar.h" // struct bregs
#include "config.h" // CONFIG_*
#include "cmos.h" // inb_cmos
#include "util.h" // debug_enter
#include "ata.h" // ATA_*

static inline void
disk_ret(struct bregs *regs, u8 code)
{
    regs->ah = code;
    SET_BDA(disk_last_status, code);
    set_cf(regs, code);
}

// disk controller reset
static void
disk_1300(struct bregs *regs, u8 device)
{
    ata_reset(device);
}

// read disk status
static void
disk_1301(struct bregs *regs, u8 device)
{
    regs->ah = GET_BDA(disk_last_status);
    disk_ret(regs, DISK_RET_SUCCESS);
}

static int
check_params(struct bregs *regs, u8 device)
{
    u16 count       = regs->al;
    u16 cylinder    = regs->ch | ((((u16) regs->cl) << 2) & 0x300);
    u16 sector      = regs->cl & 0x3f;
    u16 head        = regs->dh;

    if ((count > 128) || (count == 0) || (sector == 0)) {
        BX_INFO("int13_harddisk: function %02x, parameter out of range!\n"
                , regs->ah);
        disk_ret(regs, DISK_RET_EPARAM);
        return -1;
    }

    u16 nlc   = GET_EBDA(ata.devices[device].lchs.cylinders);
    u16 nlh   = GET_EBDA(ata.devices[device].lchs.heads);
    u16 nlspt = GET_EBDA(ata.devices[device].lchs.spt);

    // sanity check on cyl heads, sec
    if ( (cylinder >= nlc) || (head >= nlh) || (sector > nlspt )) {
        BX_INFO("int13_harddisk: function %02x, parameters out of"
                " range %04x/%04x/%04x!\n"
                , regs->ah, cylinder, head, sector);
        disk_ret(regs, DISK_RET_EPARAM);
        return -1;
    }
    return 0;
}

static void
disk_1302(struct bregs *regs, u8 device)
{
    int ret = check_params(regs, device);
    if (ret)
        return;
    u16 count       = regs->al;
    u16 cylinder    = regs->ch | ((((u16) regs->cl) << 2) & 0x300);
    u16 sector      = regs->cl & 0x3f;
    u16 head        = regs->dh;
    u16 nph   = GET_EBDA(ata.devices[device].pchs.heads);
    u16 npspt = GET_EBDA(ata.devices[device].pchs.spt);

    u16 nlh   = GET_EBDA(ata.devices[device].lchs.heads);
    u16 nlspt = GET_EBDA(ata.devices[device].lchs.spt);

    u32 lba = 0;
    // if needed, translate lchs to lba, and execute command
    if ( (nph != nlh) || (npspt != nlspt)) {
        lba = (((((u32)cylinder * (u32)nlh) + (u32)head) * (u32)nlspt)
               + (u32)sector - 1);
        sector = 0; // this forces the command to be lba
    }

    u16 segment = regs->es;
    u16 offset  = regs->bx;

    u8 status = ata_cmd_data_in(device, ATA_CMD_READ_SECTORS
                                , count, cylinder, head, sector
                                , lba, segment, offset);

    // Set nb of sector transferred
    regs->al = GET_EBDA(ata.trsfsectors);

    if (status != 0) {
        BX_INFO("int13_harddisk: function %02x, error %02x !\n",regs->ah,status);
        disk_ret(regs, DISK_RET_EBADTRACK);
    }
    disk_ret(regs, DISK_RET_SUCCESS);
}

static void
disk_1303(struct bregs *regs, u8 device)
{
    int ret = check_params(regs, device);
    if (ret)
        return;
    u16 count       = regs->al;
    u16 cylinder    = regs->ch | ((((u16) regs->cl) << 2) & 0x300);
    u16 sector      = regs->cl & 0x3f;
    u16 head        = regs->dh;
    u16 nph   = GET_EBDA(ata.devices[device].pchs.heads);
    u16 npspt = GET_EBDA(ata.devices[device].pchs.spt);

    u16 nlh   = GET_EBDA(ata.devices[device].lchs.heads);
    u16 nlspt = GET_EBDA(ata.devices[device].lchs.spt);

    u32 lba = 0;
    // if needed, translate lchs to lba, and execute command
    if ( (nph != nlh) || (npspt != nlspt)) {
        lba = (((((u32)cylinder * (u32)nlh) + (u32)head) * (u32)nlspt)
               + (u32)sector - 1);
        sector = 0; // this forces the command to be lba
    }

    u16 segment = regs->es;
    u16 offset  = regs->bx;

    u8 status = ata_cmd_data_out(device, ATA_CMD_READ_SECTORS
                                 , count, cylinder, head, sector
                                 , lba, segment, offset);

    // Set nb of sector transferred
    regs->al = GET_EBDA(ata.trsfsectors);

    if (status != 0) {
        BX_INFO("int13_harddisk: function %02x, error %02x !\n",regs->ah,status);
        disk_ret(regs, DISK_RET_EBADTRACK);
    }
    disk_ret(regs, DISK_RET_SUCCESS);
}

static void
disk_1304(struct bregs *regs, u8 device)
{
    int ret = check_params(regs, device);
    if (ret)
        return;
    // FIXME verify
    disk_ret(regs, DISK_RET_SUCCESS);
}

#define DISK_STUB(regs) do {                    \
        struct bregs *__regs = (regs);          \
        debug_stub(__regs);                     \
        disk_ret(__regs, DISK_RET_SUCCESS);     \
    } while (0)

// format disk track
static void
disk_1305(struct bregs *regs, u8 device)
{
    DISK_STUB(regs);
}

// read disk drive parameters
static void
disk_1308(struct bregs *regs, u8 device)
{
    // Get logical geometry from table
    u16 nlc = GET_EBDA(ata.devices[device].lchs.cylinders);
    u16 nlh = GET_EBDA(ata.devices[device].lchs.heads);
    u16 nlspt = GET_EBDA(ata.devices[device].lchs.spt);
    u16 count = GET_EBDA(ata.hdcount);

    nlc = nlc - 2; /* 0 based , last sector not used */
    regs->al = 0;
    regs->ch = nlc & 0xff;
    regs->cl = ((nlc >> 2) & 0xc0) | (nlspt & 0x3f);
    regs->dh = nlh - 1;
    regs->dl = count; /* FIXME returns 0, 1, or n hard drives */

    // FIXME should set ES & DI
    disk_ret(regs, DISK_RET_SUCCESS);
}

// check drive ready
static void
disk_1310(struct bregs *regs, u8 device)
{
    // should look at 40:8E also???

    // Read the status from controller
    u8 status = inb(GET_EBDA(ata.channels[device/2].iobase1) + ATA_CB_STAT);
    if ( (status & ( ATA_CB_STAT_BSY | ATA_CB_STAT_RDY )) == ATA_CB_STAT_RDY )
        disk_ret(regs, DISK_RET_SUCCESS);
    else
        disk_ret(regs, DISK_RET_ENOTREADY);
}

// read disk drive size
static void
disk_1315(struct bregs *regs, u8 device)
{
    // Get logical geometry from table
    u16 nlc   = GET_EBDA(ata.devices[device].lchs.cylinders);
    u16 nlh   = GET_EBDA(ata.devices[device].lchs.heads);
    u16 nlspt = GET_EBDA(ata.devices[device].lchs.spt);

    // Compute sector count seen by int13
    u32 lba = (u32)(nlc - 1) * (u32)nlh * (u32)nlspt;
    regs->cx = lba >> 16;
    regs->dx = lba & 0xffff;

    disk_ret(regs, 0);
    regs->ah = 3; // hard disk accessible
}

static void
disk_13XX(struct bregs *regs, u8 device)
{
    BX_INFO("int13_harddisk: function %xh unsupported, returns fail\n", regs->ah);
    disk_ret(regs, DISK_RET_EPARAM);
}

static void
disk_13(struct bregs *regs, u8 drive)
{
    if (! CONFIG_ATA) {
        disk_13XX(regs, drive);
        return;
    }

    //debug_stub(regs);

    // clear completion flag
    SET_BDA(disk_interrupt_flag, 0);

    // basic check : device has to be defined
    if (drive < 0x80 || drive >= 0x80 + CONFIG_MAX_ATA_DEVICES) {
        disk_13XX(regs, drive);
        return;
    }

    // Get the ata channel
    u8 device = GET_EBDA(ata.hdidmap[drive-0x80]);

    // basic check : device has to be valid
    if (device >= CONFIG_MAX_ATA_DEVICES) {
        disk_13XX(regs, drive);
        return;
    }

    switch (regs->ah) {
    case 0x00: disk_1300(regs, device); break;
    case 0x01: disk_1301(regs, device); break;
    case 0x02: disk_1302(regs, device); break;
    case 0x03: disk_1303(regs, device); break;
    case 0x04: disk_1304(regs, device); break;
    case 0x05: disk_1305(regs, device); break;
    case 0x08: disk_1308(regs, device); break;
    case 0x10: disk_1310(regs, device); break;
    case 0x15: disk_1315(regs, device); break;
    // XXX - several others defined
    default:   disk_13XX(regs, device); break;
    }
}

static void
handle_legacy_disk(struct bregs *regs, u8 drive)
{
    if (drive < 0x80) {
        floppy_13(regs, drive);
        return;
    }
#if BX_USE_ATADRV
    if (drive >= 0xE0) {
        int13_cdrom(regs); // xxx
        return;
    }
#endif

    disk_13(regs, drive);
}

void VISIBLE
handle_40(struct bregs *regs)
{
    debug_enter(regs);
    handle_legacy_disk(regs, regs->dl);
    debug_exit(regs);
}

// INT 13h Fixed Disk Services Entry Point
void VISIBLE
handle_13(struct bregs *regs)
{
    //debug_enter(regs);
    u8 drive = regs->dl;
    // XXX
#if BX_ELTORITO_BOOT
    if (regs->ah >= 0x4a || regs->ah <= 0x4d) {
        int13_eltorito(regs);
    } else if (cdemu_isactive() && cdrom_emulated_drive()) {
        int13_cdemu(regs);
    } else
#endif
        handle_legacy_disk(regs, drive);
    debug_exit(regs);
}

// record completion in BIOS task complete flag
void VISIBLE
handle_76(struct bregs *regs)
{
    debug_enter(regs);
    SET_BDA(floppy_harddisk_info, 0xff);
    eoi_both_pics();
}
