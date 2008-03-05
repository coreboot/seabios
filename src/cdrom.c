// 16bit code to access cdrom drives.
//
// Copyright (C) 2008  Kevin O'Connor <kevin@koconnor.net>
// Copyright (C) 2002  MandrakeSoft S.A.
//
// This file may be distributed under the terms of the GNU GPLv3 license.

#include "disk.h" // cdrom_13
#include "util.h" // memset
#include "ata.h" // ATA_CMD_READ_SECTORS


/****************************************************************
 * CDROM functions
 ****************************************************************/

// read disk drive size
static void
cdrom_1315(struct bregs *regs, u8 device)
{
    disk_ret(regs, DISK_RET_EADDRNOTFOUND);
}

// lock
static void
cdrom_134500(struct bregs *regs, u8 device)
{
    u8 locks = GET_EBDA(ata.devices[device].lock);
    if (locks == 0xff) {
        regs->al = 1;
        disk_ret(regs, DISK_RET_ETOOMANYLOCKS);
        return;
    }
    SET_EBDA(ata.devices[device].lock, locks + 1);
    regs->al = 1;
    disk_ret(regs, DISK_RET_SUCCESS);
}

// unlock
static void
cdrom_134501(struct bregs *regs, u8 device)
{
    u8 locks = GET_EBDA(ata.devices[device].lock);
    if (locks == 0x00) {
        regs->al = 0;
        disk_ret(regs, DISK_RET_ENOTLOCKED);
        return;
    }
    locks--;
    SET_EBDA(ata.devices[device].lock, locks);
    regs->al = (locks ? 1 : 0);
    disk_ret(regs, DISK_RET_SUCCESS);
}

// status
static void
cdrom_134502(struct bregs *regs, u8 device)
{
    u8 locks = GET_EBDA(ata.devices[device].lock);
    regs->al = (locks ? 1 : 0);
    disk_ret(regs, DISK_RET_SUCCESS);
}

static void
cdrom_1345XX(struct bregs *regs, u8 device)
{
    disk_ret(regs, DISK_RET_EPARAM);
}

// IBM/MS lock/unlock drive
static void
cdrom_1345(struct bregs *regs, u8 device)
{
    switch (regs->al) {
    case 0x00: cdrom_134500(regs, device); break;
    case 0x01: cdrom_134501(regs, device); break;
    case 0x02: cdrom_134502(regs, device); break;
    default:   cdrom_1345XX(regs, device); break;
    }
}

// IBM/MS eject media
static void
cdrom_1346(struct bregs *regs, u8 device)
{
    u8 locks = GET_EBDA(ata.devices[device].lock);
    if (locks != 0) {
        disk_ret(regs, DISK_RET_ELOCKED);
        return;
    }

    // FIXME should handle 0x31 no media in device
    // FIXME should handle 0xb5 valid request failed

    // Call removable media eject
    struct bregs br;
    memset(&br, 0, sizeof(br));
    br.ah = 0x52;
    call16_int(0x15, &br);

    if (br.ah || br.flags & F_CF) {
        disk_ret(regs, DISK_RET_ELOCKED);
        return;
    }
    disk_ret(regs, DISK_RET_SUCCESS);
}

// IBM/MS extended media change
static void
cdrom_1349(struct bregs *regs, u8 device)
{
    // always send changed ??
    regs->ah = DISK_RET_ECHANGED;
    set_cf(regs, 1);
}

static void
cdrom_ok(struct bregs *regs, u8 device)
{
    disk_ret(regs, DISK_RET_SUCCESS);
}

static void
cdrom_wp(struct bregs *regs, u8 device)
{
    disk_ret(regs, DISK_RET_EWRITEPROTECT);
}

void
cdrom_13(struct bregs *regs, u8 device)
{
    //debug_stub(regs);

    switch (regs->ah) {
    case 0x15: cdrom_1315(regs, device); break;
    case 0x45: cdrom_1345(regs, device); break;
    case 0x46: cdrom_1346(regs, device); break;
    case 0x49: cdrom_1349(regs, device); break;

    // These functions are the same as for hard disks
    case 0x01:
    case 0x41:
    case 0x42:
    case 0x44:
    case 0x47:
    case 0x48:
    case 0x4e:
        disk_13(regs, device);
        break;

    // all these functions return SUCCESS
    case 0x00: // disk controller reset
    case 0x09: // initialize drive parameters
    case 0x0c: // seek to specified cylinder
    case 0x0d: // alternate disk reset
    case 0x10: // check drive ready
    case 0x11: // recalibrate
    case 0x14: // controller internal diagnostic
    case 0x16: // detect disk change
        cdrom_ok(regs, device);
        break;

    // all these functions return disk write-protected
    case 0x03: // write disk sectors
    case 0x05: // format disk track
    case 0x43: // IBM/MS extended write
        cdrom_wp(regs, device);
        break;

    default:   disk_13XX(regs, device); break;
    }
}


/****************************************************************
 * CD emulation
 ****************************************************************/

// read disk sectors
static void
cdemu_1302(struct bregs *regs, u8 device)
{
    emu_access(regs, device, ATA_CMD_READ_SECTORS);
}

// verify disk sectors
static void
cdemu_1304(struct bregs *regs, u8 device)
{
    emu_access(regs, device, 0);
}

// read disk drive parameters
static void
cdemu_1308(struct bregs *regs, u8 device)
{
    u16 nlc   = GET_EBDA(cdemu.vdevice.cylinders) - 1;
    u16 nlh   = GET_EBDA(cdemu.vdevice.heads) - 1;
    u16 nlspt = GET_EBDA(cdemu.vdevice.spt);

    regs->al = 0x00;
    regs->bl = 0x00;
    regs->ch = nlc & 0xff;
    regs->cl = ((nlc >> 2) & 0xc0) | (nlspt  & 0x3f);
    regs->dh = nlh;
    // FIXME ElTorito Various. should send the real count of drives 1 or 2
    // FIXME ElTorito Harddisk. should send the HD count
    regs->dl = 0x02;
    u8 media = GET_EBDA(cdemu.media);
    if (media <= 3)
        regs->bl = media * 2;

    regs->es = SEG_BIOS;
    regs->di = (u16)&diskette_param_table2;

    disk_ret(regs, DISK_RET_SUCCESS);
}

void
cdemu_13(struct bregs *regs)
{
    //debug_stub(regs);

    u8 device  = GET_EBDA(cdemu.controller_index) * 2;
    device += GET_EBDA(cdemu.device_spec);

    switch (regs->ah) {
    case 0x02: cdemu_1302(regs, device); break;
    case 0x04: cdemu_1304(regs, device); break;
    case 0x08: cdemu_1308(regs, device); break;
    // XXX - All other calls get passed to standard CDROM functions.
    default: cdrom_13(regs, device); break;
    }
}

struct eltorito_s {
    u8 size;
    u8 media;
    u8 emulated_drive;
    u8 controller_index;
    u32 ilba;
    u16 device_spec;
    u16 buffer_segment;
    u16 load_segment;
    u16 sector_count;
    u8 cylinders;
    u8 sectors;
    u8 heads;
};

#define SET_INT13ET(regs,var,val)                                      \
    SET_FARVAR((regs)->ds, ((struct eltorito_s*)((regs)->si+0))->var, (val))

// ElTorito - Terminate disk emu
void
cdemu_134b(struct bregs *regs)
{
    // FIXME ElTorito Hardcoded
    SET_INT13ET(regs, size, 0x13);
    SET_INT13ET(regs, media, GET_EBDA(cdemu.media));
    SET_INT13ET(regs, emulated_drive, GET_EBDA(cdemu.emulated_drive));
    SET_INT13ET(regs, controller_index, GET_EBDA(cdemu.controller_index));
    SET_INT13ET(regs, ilba, GET_EBDA(cdemu.ilba));
    SET_INT13ET(regs, device_spec, GET_EBDA(cdemu.device_spec));
    SET_INT13ET(regs, buffer_segment, GET_EBDA(cdemu.buffer_segment));
    SET_INT13ET(regs, load_segment, GET_EBDA(cdemu.load_segment));
    SET_INT13ET(regs, sector_count, GET_EBDA(cdemu.sector_count));
    SET_INT13ET(regs, cylinders, GET_EBDA(cdemu.vdevice.cylinders));
    SET_INT13ET(regs, sectors, GET_EBDA(cdemu.vdevice.spt));
    SET_INT13ET(regs, heads, GET_EBDA(cdemu.vdevice.heads));

    // If we have to terminate emulation
    if (regs->al == 0x00) {
        // FIXME ElTorito Various. Should be handled accordingly to spec
        SET_EBDA(cdemu.active, 0x00); // bye bye
    }

    disk_ret(regs, DISK_RET_SUCCESS);
}
