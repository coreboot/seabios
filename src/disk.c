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

#define DEBUGF1(fmt, args...) bprintf(0, fmt , ##args)
#define DEBUGF(fmt, args...)


/****************************************************************
 * Helper functions
 ****************************************************************/

void
__disk_ret(const char *fname, struct bregs *regs, u8 code)
{
    SET_BDA(disk_last_status, code);
    if (code)
        __set_code_fail(fname, regs, code);
    else
        set_code_success(regs);
}

static void
__disk_stub(const char *fname, struct bregs *regs)
{
    __debug_stub(fname, regs);
    __disk_ret(fname, regs, DISK_RET_SUCCESS);
}

#define DISK_STUB(regs) \
    __disk_stub(__func__, (regs))

static void
basic_access(struct bregs *regs, u8 device, u16 command)
{
    u8 type = GET_EBDA(ata.devices[device].type);
    u16 nlc, nlh, nlspt;
    if (type == ATA_TYPE_ATA) {
        nlc   = GET_EBDA(ata.devices[device].lchs.cylinders);
        nlh   = GET_EBDA(ata.devices[device].lchs.heads);
        nlspt = GET_EBDA(ata.devices[device].lchs.spt);
    } else {
        // Must be cd emulation.
        nlc   = GET_EBDA(cdemu.vdevice.cylinders);
        nlh   = GET_EBDA(cdemu.vdevice.heads);
        nlspt = GET_EBDA(cdemu.vdevice.spt);
    }

    u16 count       = regs->al;
    u16 cylinder    = regs->ch | ((((u16) regs->cl) << 2) & 0x300);
    u16 sector      = regs->cl & 0x3f;
    u16 head        = regs->dh;

    if (count > 128 || count == 0 || sector == 0) {
        BX_INFO("int13_harddisk: function %02x, parameter out of range!\n"
                , regs->ah);
        disk_ret(regs, DISK_RET_EPARAM);
        return;
    }

    // sanity check on cyl heads, sec
    if (cylinder >= nlc || head >= nlh || sector > nlspt) {
        BX_INFO("int13_harddisk: function %02x, parameters out of"
                " range %04x/%04x/%04x!\n"
                , regs->ah, cylinder, head, sector);
        disk_ret(regs, DISK_RET_EPARAM);
        return;
    }

    if (!command) {
        // If verify or seek
        disk_ret(regs, DISK_RET_SUCCESS);
        return;
    }

    // translate lchs to lba
    u32 lba = (((((u32)cylinder * (u32)nlh) + (u32)head) * (u32)nlspt)
               + (u32)sector - 1);

    u16 segment = regs->es;
    u16 offset  = regs->bx;
    void *far_buffer = MAKE_FARPTR(segment, offset);

    irq_enable();

    int status;
    if (type == ATA_TYPE_ATA)
        status = ata_cmd_data(device, command, lba, count, far_buffer);
    else
        status = cdrom_read_emu(device, lba, count, far_buffer);

    irq_disable();

    // Set nb of sector transferred
    regs->al = GET_EBDA(ata.trsfsectors);

    if (status != 0) {
        BX_INFO("int13_harddisk: function %02x, error %02x !\n"
                , regs->ah, status);
        disk_ret(regs, DISK_RET_EBADTRACK);
    }
    disk_ret(regs, DISK_RET_SUCCESS);
}

static void
extended_access(struct bregs *regs, u8 device, u16 command)
{
    u16 count = GET_INT13EXT(regs, count);

    // Can't use 64 bits lba
    u32 lba = GET_INT13EXT(regs, lba2);
    if (lba != 0L) {
        BX_PANIC("int13_harddisk: function %02x. Can't use 64bits lba\n"
                 , regs->ah);
        disk_ret(regs, DISK_RET_EPARAM);
        return;
    }

    u8 type = GET_EBDA(ata.devices[device].type);

    // Get 32 bits lba and check
    lba = GET_INT13EXT(regs, lba1);
    if (type == ATA_TYPE_ATA
        && lba >= GET_EBDA(ata.devices[device].sectors)) {
        BX_INFO("int13_harddisk: function %02x. LBA out of range\n", regs->ah);
        disk_ret(regs, DISK_RET_EPARAM);
        return;
    }

    if (!command) {
        // If verify or seek
        disk_ret(regs, DISK_RET_SUCCESS);
        return;
    }

    u16 segment = GET_INT13EXT(regs, segment);
    u16 offset = GET_INT13EXT(regs, offset);
    void *far_buffer = MAKE_FARPTR(segment, offset);

    irq_enable();

    u8 status;
    if (type == ATA_TYPE_ATA)
        status = ata_cmd_data(device, command, lba, count, far_buffer);
    else
        status = cdrom_read(device, lba, count, far_buffer);

    irq_disable();

    SET_INT13EXT(regs, count, GET_EBDA(ata.trsfsectors));

    if (status != 0) {
        BX_INFO("int13_harddisk: function %02x, error %02x !\n"
                , regs->ah, status);
        disk_ret(regs, DISK_RET_EBADTRACK);
        return;
    }
    disk_ret(regs, DISK_RET_SUCCESS);
}


/****************************************************************
 * Hard Drive functions
 ****************************************************************/

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
    u8 v = GET_BDA(disk_last_status);
    regs->ah = v;
    set_cf(regs, v);
    // XXX - clear disk_last_status?
}

// read disk sectors
static void
disk_1302(struct bregs *regs, u8 device)
{
    basic_access(regs, device, ATA_CMD_READ_SECTORS);
}

// write disk sectors
static void
disk_1303(struct bregs *regs, u8 device)
{
    basic_access(regs, device, ATA_CMD_WRITE_SECTORS);
}

// verify disk sectors
static void
disk_1304(struct bregs *regs, u8 device)
{
    basic_access(regs, device, 0);
    // FIXME verify
}

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

// initialize drive parameters
static void
disk_1309(struct bregs *regs, u8 device)
{
    DISK_STUB(regs);
}

// seek to specified cylinder
static void
disk_130c(struct bregs *regs, u8 device)
{
    DISK_STUB(regs);
}

// alternate disk reset
static void
disk_130d(struct bregs *regs, u8 device)
{
    DISK_STUB(regs);
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

// recalibrate
static void
disk_1311(struct bregs *regs, u8 device)
{
    DISK_STUB(regs);
}

// controller internal diagnostic
static void
disk_1314(struct bregs *regs, u8 device)
{
    DISK_STUB(regs);
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

    disk_ret(regs, DISK_RET_SUCCESS);
    regs->ah = 3; // hard disk accessible
}

// IBM/MS installation check
static void
disk_1341(struct bregs *regs, u8 device)
{
    regs->bx = 0xaa55;  // install check
    regs->cx = 0x0007;  // ext disk access and edd, removable supported
    disk_ret(regs, DISK_RET_SUCCESS);
    regs->ah = 0x30;    // EDD 3.0
}

// IBM/MS extended read
static void
disk_1342(struct bregs *regs, u8 device)
{
    extended_access(regs, device, ATA_CMD_READ_SECTORS);
}

// IBM/MS extended write
static void
disk_1343(struct bregs *regs, u8 device)
{
    extended_access(regs, device, ATA_CMD_WRITE_SECTORS);
}

// IBM/MS verify
static void
disk_1344(struct bregs *regs, u8 device)
{
    extended_access(regs, device, 0);
}

// IBM/MS lock/unlock drive
static void
disk_1345(struct bregs *regs, u8 device)
{
    // Always success for HD
    disk_ret(regs, DISK_RET_SUCCESS);
}

// IBM/MS eject media
static void
disk_1346(struct bregs *regs, u8 device)
{
    // Volume Not Removable
    disk_ret(regs, DISK_RET_ENOTREMOVABLE);
}

// IBM/MS extended seek
static void
disk_1347(struct bregs *regs, u8 device)
{
    extended_access(regs, device, 0);
}

// IBM/MS get drive parameters
static void
disk_1348(struct bregs *regs, u8 device)
{
    u16 size = GET_INT13DPT(regs, size);

    // Buffer is too small
    if (size < 0x1a) {
        disk_ret(regs, DISK_RET_EPARAM);
        return;
    }

    // EDD 1.x

    u8  type    = GET_EBDA(ata.devices[device].type);
    u16 npc     = GET_EBDA(ata.devices[device].pchs.cylinders);
    u16 nph     = GET_EBDA(ata.devices[device].pchs.heads);
    u16 npspt   = GET_EBDA(ata.devices[device].pchs.spt);
    u32 lba     = GET_EBDA(ata.devices[device].sectors);
    u16 blksize = GET_EBDA(ata.devices[device].blksize);

    SET_INT13DPT(regs, size, 0x1a);
    if (type == ATA_TYPE_ATA) {
        if ((lba/npspt)/nph > 0x3fff) {
            SET_INT13DPT(regs, infos, 0x00); // geometry is invalid
            SET_INT13DPT(regs, cylinders, 0x3fff);
        } else {
            SET_INT13DPT(regs, infos, 0x02); // geometry is valid
            SET_INT13DPT(regs, cylinders, (u32)npc);
        }
        SET_INT13DPT(regs, heads, (u32)nph);
        SET_INT13DPT(regs, spt, (u32)npspt);
        SET_INT13DPT(regs, sector_count1, lba);  // FIXME should be Bit64
        SET_INT13DPT(regs, sector_count2, 0L);
    } else {
        // ATAPI
        // 0x74 = removable, media change, lockable, max values
        SET_INT13DPT(regs, infos, 0x74);
        SET_INT13DPT(regs, cylinders, 0xffffffff);
        SET_INT13DPT(regs, heads, 0xffffffff);
        SET_INT13DPT(regs, spt, 0xffffffff);
        SET_INT13DPT(regs, sector_count1, 0xffffffff);  // FIXME should be Bit64
        SET_INT13DPT(regs, sector_count2, 0xffffffff);
    }
    SET_INT13DPT(regs, blksize, blksize);

    if (size < 0x1e) {
        disk_ret(regs, DISK_RET_SUCCESS);
        return;
    }

    // EDD 2.x

    SET_INT13DPT(regs, size, 0x1e);

    SET_INT13DPT(regs, dpte_segment, SEG_EBDA);
    SET_INT13DPT(regs, dpte_offset
                 , offsetof(struct extended_bios_data_area_s, ata.dpte));

    // Fill in dpte
    u8 channel = device / 2;
    u16 iobase1 = GET_EBDA(ata.channels[channel].iobase1);
    u16 iobase2 = GET_EBDA(ata.channels[channel].iobase2);
    u8 irq = GET_EBDA(ata.channels[channel].irq);
    u8 mode = GET_EBDA(ata.devices[device].mode);

    u16 options;
    if (type == ATA_TYPE_ATA) {
        u8 translation = GET_EBDA(ata.devices[device].translation);
        options  = (translation==ATA_TRANSLATION_NONE?0:1)<<3; // chs translation
        options |= (translation==ATA_TRANSLATION_LBA?1:0)<<9;
        options |= (translation==ATA_TRANSLATION_RECHS?3:0)<<9;
    } else {
        // ATAPI
        options  = (1<<5); // removable device
        options |= (1<<6); // atapi device
    }
    options |= (1<<4); // lba translation
    options |= (mode==ATA_MODE_PIO32?1:0)<<7;

    SET_EBDA(ata.dpte.iobase1, iobase1);
    SET_EBDA(ata.dpte.iobase2, iobase2 + ATA_CB_DC);
    SET_EBDA(ata.dpte.prefix, (0xe | (device % 2))<<4 );
    SET_EBDA(ata.dpte.unused, 0xcb );
    SET_EBDA(ata.dpte.irq, irq );
    SET_EBDA(ata.dpte.blkcount, 1 );
    SET_EBDA(ata.dpte.dma, 0 );
    SET_EBDA(ata.dpte.pio, 0 );
    SET_EBDA(ata.dpte.options, options);
    SET_EBDA(ata.dpte.reserved, 0);
    if (size >= 0x42)
        SET_EBDA(ata.dpte.revision, 0x11);
    else
        SET_EBDA(ata.dpte.revision, 0x10);

    u8 *p = MAKE_FARPTR(SEG_EBDA
                        , offsetof(struct extended_bios_data_area_s, ata.dpte));
    u8 sum = checksum(p, 15);
    SET_EBDA(ata.dpte.checksum, ~sum);

    if (size < 0x42) {
        disk_ret(regs, DISK_RET_SUCCESS);
        return;
    }

    // EDD 3.x
    channel = device / 2;
    u8 iface = GET_EBDA(ata.channels[channel].iface);
    iobase1 = GET_EBDA(ata.channels[channel].iobase1);

    SET_INT13DPT(regs, size, 0x42);
    SET_INT13DPT(regs, key, 0xbedd);
    SET_INT13DPT(regs, dpi_length, 0x24);
    SET_INT13DPT(regs, reserved1, 0);
    SET_INT13DPT(regs, reserved2, 0);

    if (iface==ATA_IFACE_ISA) {
        SET_INT13DPT(regs, host_bus[0], 'I');
        SET_INT13DPT(regs, host_bus[1], 'S');
        SET_INT13DPT(regs, host_bus[2], 'A');
        SET_INT13DPT(regs, host_bus[3], 0);
    } else {
        // FIXME PCI
    }
    SET_INT13DPT(regs, iface_type[0], 'A');
    SET_INT13DPT(regs, iface_type[1], 'T');
    SET_INT13DPT(regs, iface_type[2], 'A');
    SET_INT13DPT(regs, iface_type[3], 0);

    if (iface==ATA_IFACE_ISA) {
        SET_INT13DPT(regs, iface_path[0], iobase1);
        SET_INT13DPT(regs, iface_path[2], 0);
        SET_INT13DPT(regs, iface_path[4], 0L);
    } else {
        // FIXME PCI
    }
    SET_INT13DPT(regs, device_path[0], device%2);
    SET_INT13DPT(regs, device_path[1], 0);
    SET_INT13DPT(regs, device_path[2], 0);
    SET_INT13DPT(regs, device_path[4], 0L);

    sum = checksum(MAKE_FARPTR(regs->ds, 30), 34);
    SET_INT13DPT(regs, checksum, ~sum);
}

// IBM/MS extended media change
static void
disk_1349(struct bregs *regs, u8 device)
{
    // Always success for HD
    disk_ret(regs, DISK_RET_SUCCESS);
}

static void
disk_134e01(struct bregs *regs, u8 device)
{
    disk_ret(regs, DISK_RET_SUCCESS);
}

static void
disk_134e03(struct bregs *regs, u8 device)
{
    disk_ret(regs, DISK_RET_SUCCESS);
}

static void
disk_134e04(struct bregs *regs, u8 device)
{
    disk_ret(regs, DISK_RET_SUCCESS);
}

static void
disk_134e06(struct bregs *regs, u8 device)
{
    disk_ret(regs, DISK_RET_SUCCESS);
}

static void
disk_134eXX(struct bregs *regs, u8 device)
{
    disk_ret(regs, DISK_RET_EPARAM);
}

// IBM/MS set hardware configuration
static void
disk_134e(struct bregs *regs, u8 device)
{
    switch (regs->al) {
    case 0x01: disk_134e01(regs, device); break;
    case 0x03: disk_134e03(regs, device); break;
    case 0x04: disk_134e04(regs, device); break;
    case 0x06: disk_134e06(regs, device); break;
    default:   disk_134eXX(regs, device); break;
    }
}

void
disk_13XX(struct bregs *regs, u8 device)
{
    disk_ret(regs, DISK_RET_EPARAM);
}

void
disk_13(struct bregs *regs, u8 device)
{
    //debug_stub(regs);

    // clear completion flag
    SET_BDA(disk_interrupt_flag, 0);

    switch (regs->ah) {
    case 0x00: disk_1300(regs, device); break;
    case 0x01: disk_1301(regs, device); break;
    case 0x02: disk_1302(regs, device); break;
    case 0x03: disk_1303(regs, device); break;
    case 0x04: disk_1304(regs, device); break;
    case 0x05: disk_1305(regs, device); break;
    case 0x08: disk_1308(regs, device); break;
    case 0x09: disk_1309(regs, device); break;
    case 0x0c: disk_130c(regs, device); break;
    case 0x0d: disk_130d(regs, device); break;
    case 0x10: disk_1310(regs, device); break;
    case 0x11: disk_1311(regs, device); break;
    case 0x14: disk_1314(regs, device); break;
    case 0x15: disk_1315(regs, device); break;
    case 0x41: disk_1341(regs, device); break;
    case 0x42: disk_1342(regs, device); break;
    case 0x43: disk_1343(regs, device); break;
    case 0x44: disk_1344(regs, device); break;
    case 0x45: disk_1345(regs, device); break;
    case 0x46: disk_1346(regs, device); break;
    case 0x47: disk_1347(regs, device); break;
    case 0x48: disk_1348(regs, device); break;
    case 0x49: disk_1349(regs, device); break;
    case 0x4e: disk_134e(regs, device); break;
    default:   disk_13XX(regs, device); break;
    }
}


/****************************************************************
 * Entry points
 ****************************************************************/

static u8
get_device(struct bregs *regs, u8 iscd, u8 drive)
{
    // basic check : device has to be defined
    if (drive >= CONFIG_MAX_ATA_DEVICES) {
        disk_ret(regs, DISK_RET_EPARAM);
        return CONFIG_MAX_ATA_DEVICES;
    }

    // Get the ata channel
    u8 device = GET_EBDA(ata.idmap[iscd][drive]);

    // basic check : device has to be valid
    if (device >= CONFIG_MAX_ATA_DEVICES) {
        disk_ret(regs, DISK_RET_EPARAM);
        return CONFIG_MAX_ATA_DEVICES;
    }

    return device;
}

static void
handle_legacy_disk(struct bregs *regs, u8 drive)
{
    if (drive < 0x80) {
        floppy_13(regs, drive);
        return;
    }

    if (! CONFIG_ATA) {
        // XXX - old code had other disk access method.
        disk_ret(regs, DISK_RET_EPARAM);
        return;
    }

    if (drive >= 0xe0) {
        u8 device = get_device(regs, 1, drive - 0xe0);
        if (device >= CONFIG_MAX_ATA_DEVICES)
            return;
        cdrom_13(regs, device);
        return;
    }

    u8 device = get_device(regs, 0, drive - 0x80);
    if (device >= CONFIG_MAX_ATA_DEVICES)
        return;
    disk_13(regs, device);
}

void VISIBLE16
handle_40(struct bregs *regs)
{
    debug_enter(regs);
    handle_legacy_disk(regs, regs->dl);
}

// INT 13h Fixed Disk Services Entry Point
void VISIBLE16
handle_13(struct bregs *regs)
{
    //debug_enter(regs);
    u8 drive = regs->dl;

    if (CONFIG_CDROM_EMU) {
        if (regs->ah == 0x4b) {
            cdemu_134b(regs);
            return;
        }
        if (GET_EBDA(cdemu.active)) {
            if (drive == GET_EBDA(cdemu.emulated_drive)) {
                cdemu_13(regs);
                return;
            }
            if (drive < 0xe0)
                drive--;
        }
    }
    handle_legacy_disk(regs, drive);
}

// record completion in BIOS task complete flag
void VISIBLE16
handle_76()
{
    debug_isr();
    SET_BDA(disk_interrupt_flag, 0xff);
    eoi_both_pics();
}
