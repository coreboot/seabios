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


/****************************************************************
 * Helper functions
 ****************************************************************/

static inline void
disk_ret(struct bregs *regs, u8 code)
{
    regs->ah = code;
    SET_BDA(disk_last_status, code);
    set_cf(regs, code);
}

#define DISK_STUB(regs) do {                    \
        struct bregs *__regs = (regs);          \
        debug_stub(__regs);                     \
        disk_ret(__regs, DISK_RET_SUCCESS);     \
    } while (0)

static u8
checksum_seg(u16 seg, u16 offset, u32 len)
{
    u32 i;
    u8 sum = 0;
    for (i=0; i<len; i++)
        sum += GET_FARVAR(seg, *(u8*)(offset+i));
    return sum;
}

static void
basic_access(struct bregs *regs, u8 device, u16 command)
{
    u16 count       = regs->al;
    u16 cylinder    = regs->ch | ((((u16) regs->cl) << 2) & 0x300);
    u16 sector      = regs->cl & 0x3f;
    u16 head        = regs->dh;

    if ((count > 128) || (count == 0) || (sector == 0)) {
        BX_INFO("int13_harddisk: function %02x, parameter out of range!\n"
                , regs->ah);
        disk_ret(regs, DISK_RET_EPARAM);
        return;
    }

    u16 nlc   = GET_EBDA(ata.devices[device].lchs.cylinders);
    u16 nlh   = GET_EBDA(ata.devices[device].lchs.heads);
    u16 nlspt = GET_EBDA(ata.devices[device].lchs.spt);
    u16 nph   = GET_EBDA(ata.devices[device].pchs.heads);
    u16 npspt = GET_EBDA(ata.devices[device].pchs.spt);

    // sanity check on cyl heads, sec
    if ( (cylinder >= nlc) || (head >= nlh) || (sector > nlspt )) {
        BX_INFO("int13_harddisk: function %02x, parameters out of"
                " range %04x/%04x/%04x!\n"
                , regs->ah, cylinder, head, sector);
        disk_ret(regs, DISK_RET_EPARAM);
        return;
    }

    u32 lba = 0;
    // if needed, translate lchs to lba, and execute command
    if ( (nph != nlh) || (npspt != nlspt)) {
        lba = (((((u32)cylinder * (u32)nlh) + (u32)head) * (u32)nlspt)
               + (u32)sector - 1);
        sector = 0; // this forces the command to be lba
    }

    u16 segment = regs->es;
    u16 offset  = regs->bx;

    u8 status;
    switch (command) {
    case ATA_CMD_READ_SECTORS:
        status = ata_cmd_data_in(device, ATA_CMD_READ_SECTORS
                                 , count, cylinder, head, sector
                                 , lba, segment, offset);
        break;
    case ATA_CMD_WRITE_SECTORS:
        status = ata_cmd_data_out(device, ATA_CMD_WRITE_SECTORS
                                  , count, cylinder, head, sector
                                  , lba, segment, offset);
        break;
    default:
        disk_ret(regs, DISK_RET_SUCCESS);
        return;
    }

    // Set nb of sector transferred
    regs->al = GET_EBDA(ata.trsfsectors);

    if (status != 0) {
        BX_INFO("int13_harddisk: function %02x, error %02x !\n",regs->ah,status);
        disk_ret(regs, DISK_RET_EBADTRACK);
    }
    disk_ret(regs, DISK_RET_SUCCESS);
}

static void
extended_access(struct bregs *regs, u8 device, u16 command)
{
    u16 count = GET_INT13EXT(regs, count);
    u16 segment = GET_INT13EXT(regs, segment);
    u16 offset = GET_INT13EXT(regs, offset);

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

    u8 status;
    switch (command) {
    case ATA_CMD_READ_SECTORS:
        if (type == ATA_TYPE_ATA) {
            status = ata_cmd_data_in(device, ATA_CMD_READ_SECTORS
                                     , count, 0, 0, 0
                                     , lba, segment, offset);
        } else {
            u8 atacmd[12];
            memset(atacmd, 0, sizeof(atacmd));
            atacmd[0]=0x28;                      // READ command
            atacmd[7]=(count & 0xff00) >> 8;     // Sectors
            atacmd[8]=(count & 0x00ff);          // Sectors
            atacmd[2]=(lba & 0xff000000) >> 24;  // LBA
            atacmd[3]=(lba & 0x00ff0000) >> 16;
            atacmd[4]=(lba & 0x0000ff00) >> 8;
            atacmd[5]=(lba & 0x000000ff);

            status = ata_cmd_packet(device, (u32)atacmd, sizeof(atacmd)
                                    , 0, count*2048L
                                    , ATA_DATA_IN, segment, offset);
        }
        break;
    case ATA_CMD_WRITE_SECTORS:
        status = ata_cmd_data_out(device, ATA_CMD_WRITE_SECTORS
                                  , count, 0, 0, 0
                                  , lba, segment, offset);
        break;
    default:
        // If verify or seek
        disk_ret(regs, DISK_RET_SUCCESS);
        return;
    }

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
    regs->ah = GET_BDA(disk_last_status);
    disk_ret(regs, DISK_RET_SUCCESS);
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

    disk_ret(regs, 0);
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

    SET_INT13DPT(regs, dpte_segment, EBDA_SEG);
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

    u8 sum = checksum_seg(EBDA_SEG
                          , offsetof(struct extended_bios_data_area_s, ata.dpte)
                          , 15);
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

    sum = checksum_seg(regs->ds, 30, 34);
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
    debug_stub(regs);
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

static void
disk_13XX(struct bregs *regs, u8 device)
{
    debug_stub(regs);
    disk_ret(regs, DISK_RET_EPARAM);
}

static void
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
    case 0x01: disk_1301(regs, device); break;
    case 0x41: disk_1341(regs, device); break;
    case 0x42: disk_1342(regs, device); break;
    case 0x44: disk_1344(regs, device); break;
    case 0x47: disk_1347(regs, device); break;
    case 0x48: disk_1348(regs, device); break;
    case 0x4e: disk_134e(regs, device); break;

    // all these functions return SUCCESS
    case 0x00: cdrom_ok(regs, device); break; // disk controller reset
    case 0x09: cdrom_ok(regs, device); break; // initialize drive parameters
    case 0x0c: cdrom_ok(regs, device); break; // seek to specified cylinder
    case 0x0d: cdrom_ok(regs, device); break; // alternate disk reset
    case 0x10: cdrom_ok(regs, device); break; // check drive ready
    case 0x11: cdrom_ok(regs, device); break; // recalibrate
    case 0x14: cdrom_ok(regs, device); break; // controller internal diagnostic
    case 0x16: cdrom_ok(regs, device); break; // detect disk change

    // all these functions return disk write-protected
    case 0x03: cdrom_wp(regs, device); break; // write disk sectors
    case 0x05: cdrom_wp(regs, device); break; // format disk track
    case 0x43: cdrom_wp(regs, device); break; // IBM/MS extended write

    default:   disk_13XX(regs, device); break;
    }
}


/****************************************************************
 * Entry points
 ****************************************************************/

static u8
get_device(struct bregs *regs, u8 drive)
{
    // basic check : device has to be defined
    if (drive >= CONFIG_MAX_ATA_DEVICES) {
        disk_ret(regs, DISK_RET_EPARAM);
        return CONFIG_MAX_ATA_DEVICES;
    }

    // Get the ata channel
    u8 device = GET_EBDA(ata.hdidmap[drive]);

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
        u8 device = get_device(regs, drive - 0xe0);
        if (device >= CONFIG_MAX_ATA_DEVICES)
            return;
        cdrom_13(regs, device);
        return;
    }

    u8 device = get_device(regs, drive - 0x80);
    if (device >= CONFIG_MAX_ATA_DEVICES)
        return;
    disk_13(regs, device);
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
    debug_isr(regs);
    SET_BDA(floppy_harddisk_info, 0xff);
    eoi_both_pics();
}
