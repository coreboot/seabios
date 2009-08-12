// 16bit code to access cdrom drives.
//
// Copyright (C) 2008,2009  Kevin O'Connor <kevin@koconnor.net>
// Copyright (C) 2002  MandrakeSoft S.A.
//
// This file may be distributed under the terms of the GNU LGPLv3 license.

#include "disk.h" // cdrom_13
#include "util.h" // memset
#include "bregs.h" // struct bregs
#include "biosvar.h" // GET_EBDA
#include "ata.h" // ATA_CMD_REQUEST_SENSE


/****************************************************************
 * CDROM functions
 ****************************************************************/

// read disk drive size
static void
cdrom_1315(struct bregs *regs, u8 driveid)
{
    disk_ret(regs, DISK_RET_EADDRNOTFOUND);
}

// lock
static void
cdrom_134500(struct bregs *regs, u8 driveid)
{
    u16 ebda_seg = get_ebda_seg();
    u8 locks = GET_EBDA2(ebda_seg, cdrom_locks[driveid]);
    if (locks == 0xff) {
        regs->al = 1;
        disk_ret(regs, DISK_RET_ETOOMANYLOCKS);
        return;
    }
    SET_EBDA2(ebda_seg, cdrom_locks[driveid], locks + 1);
    regs->al = 1;
    disk_ret(regs, DISK_RET_SUCCESS);
}

// unlock
static void
cdrom_134501(struct bregs *regs, u8 driveid)
{
    u16 ebda_seg = get_ebda_seg();
    u8 locks = GET_EBDA2(ebda_seg, cdrom_locks[driveid]);
    if (locks == 0x00) {
        regs->al = 0;
        disk_ret(regs, DISK_RET_ENOTLOCKED);
        return;
    }
    locks--;
    SET_EBDA2(ebda_seg, cdrom_locks[driveid], locks);
    regs->al = (locks ? 1 : 0);
    disk_ret(regs, DISK_RET_SUCCESS);
}

// status
static void
cdrom_134502(struct bregs *regs, u8 driveid)
{
    u8 locks = GET_EBDA(cdrom_locks[driveid]);
    regs->al = (locks ? 1 : 0);
    disk_ret(regs, DISK_RET_SUCCESS);
}

static void
cdrom_1345XX(struct bregs *regs, u8 driveid)
{
    disk_ret(regs, DISK_RET_EPARAM);
}

// IBM/MS lock/unlock drive
static void
cdrom_1345(struct bregs *regs, u8 driveid)
{
    switch (regs->al) {
    case 0x00: cdrom_134500(regs, driveid); break;
    case 0x01: cdrom_134501(regs, driveid); break;
    case 0x02: cdrom_134502(regs, driveid); break;
    default:   cdrom_1345XX(regs, driveid); break;
    }
}

// IBM/MS eject media
static void
cdrom_1346(struct bregs *regs, u8 driveid)
{
    u8 locks = GET_EBDA(cdrom_locks[driveid]);
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
cdrom_1349(struct bregs *regs, u8 driveid)
{
    set_fail(regs);
    // always send changed ??
    regs->ah = DISK_RET_ECHANGED;
}

static void
cdrom_ok(struct bregs *regs, u8 driveid)
{
    disk_ret(regs, DISK_RET_SUCCESS);
}

static void
cdrom_wp(struct bregs *regs, u8 driveid)
{
    disk_ret(regs, DISK_RET_EWRITEPROTECT);
}

void
cdrom_13(struct bregs *regs, u8 driveid)
{
    //debug_stub(regs);

    switch (regs->ah) {
    case 0x15: cdrom_1315(regs, driveid); break;
    case 0x45: cdrom_1345(regs, driveid); break;
    case 0x46: cdrom_1346(regs, driveid); break;
    case 0x49: cdrom_1349(regs, driveid); break;

    // These functions are the same as for hard disks
    case 0x01:
    case 0x41:
    case 0x42:
    case 0x44:
    case 0x47:
    case 0x48:
    case 0x4e:
        disk_13(regs, driveid);
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
        cdrom_ok(regs, driveid);
        break;

    // all these functions return disk write-protected
    case 0x03: // write disk sectors
    case 0x05: // format disk track
    case 0x43: // IBM/MS extended write
        cdrom_wp(regs, driveid);
        break;

    default:   disk_13XX(regs, driveid); break;
    }
}


/****************************************************************
 * CD emulation
 ****************************************************************/

static void
cdemu_1302(struct bregs *regs, u8 driveid)
{
    cdemu_access(regs, driveid, CMD_READ);
}

static void
cdemu_1304(struct bregs *regs, u8 driveid)
{
    cdemu_access(regs, driveid, CMD_VERIFY);
}

// read disk drive parameters
static void
cdemu_1308(struct bregs *regs, u8 driveid)
{
    u16 ebda_seg = get_ebda_seg();
    u16 nlc   = GET_EBDA2(ebda_seg, cdemu.lchs.cylinders) - 1;
    u16 nlh   = GET_EBDA2(ebda_seg, cdemu.lchs.heads) - 1;
    u16 nlspt = GET_EBDA2(ebda_seg, cdemu.lchs.spt);

    regs->al = 0x00;
    regs->bl = 0x00;
    regs->ch = nlc & 0xff;
    regs->cl = ((nlc >> 2) & 0xc0) | (nlspt & 0x3f);
    regs->dh = nlh;
    // FIXME ElTorito Various. should send the real count of drives 1 or 2
    // FIXME ElTorito Harddisk. should send the HD count
    regs->dl = 0x02;
    u8 media = GET_EBDA2(ebda_seg, cdemu.media);
    if (media <= 3)
        regs->bl = media * 2;

    regs->es = SEG_BIOS;
    regs->di = (u32)&diskette_param_table2;

    disk_ret(regs, DISK_RET_SUCCESS);
}

void
cdemu_13(struct bregs *regs)
{
    //debug_stub(regs);

    u16 ebda_seg = get_ebda_seg();
    u8 driveid = GET_EBDA2(ebda_seg, cdemu.emulated_driveid);

    switch (regs->ah) {
    case 0x02: cdemu_1302(regs, driveid); break;
    case 0x04: cdemu_1304(regs, driveid); break;
    case 0x08: cdemu_1308(regs, driveid); break;

    // These functions are the same as standard CDROM.
    case 0x00:
    case 0x01:
    case 0x03:
    case 0x05:
    case 0x09:
    case 0x0c:
    case 0x0d:
    case 0x10:
    case 0x11:
    case 0x14:
    case 0x15:
    case 0x16:
        cdrom_13(regs, driveid);
        break;

    default:   disk_13XX(regs, driveid); break;
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
    u16 ebda_seg = get_ebda_seg();
    SET_INT13ET(regs, size, 0x13);
    SET_INT13ET(regs, media, GET_EBDA2(ebda_seg, cdemu.media));
    SET_INT13ET(regs, emulated_drive
                , GET_EBDA2(ebda_seg, cdemu.emulated_extdrive));
    u8 driveid = GET_EBDA2(ebda_seg, cdemu.emulated_driveid);
    u8 cntl_id = GET_GLOBAL(Drives.drives[driveid].cntl_id);
    SET_INT13ET(regs, controller_index, cntl_id / 2);
    SET_INT13ET(regs, device_spec, cntl_id % 2);
    SET_INT13ET(regs, ilba, GET_EBDA2(ebda_seg, cdemu.ilba));
    SET_INT13ET(regs, buffer_segment, GET_EBDA2(ebda_seg, cdemu.buffer_segment));
    SET_INT13ET(regs, load_segment, GET_EBDA2(ebda_seg, cdemu.load_segment));
    SET_INT13ET(regs, sector_count, GET_EBDA2(ebda_seg, cdemu.sector_count));
    SET_INT13ET(regs, cylinders, GET_EBDA2(ebda_seg, cdemu.lchs.cylinders));
    SET_INT13ET(regs, sectors, GET_EBDA2(ebda_seg, cdemu.lchs.spt));
    SET_INT13ET(regs, heads, GET_EBDA2(ebda_seg, cdemu.lchs.heads));

    // If we have to terminate emulation
    if (regs->al == 0x00) {
        // FIXME ElTorito Various. Should be handled accordingly to spec
        SET_EBDA2(ebda_seg, cdemu.active, 0x00); // bye bye
    }

    disk_ret(regs, DISK_RET_SUCCESS);
}


/****************************************************************
 * CD booting
 ****************************************************************/

// Request SENSE
static int
atapi_get_sense(int driveid, u8 *asc, u8 *ascq)
{
    u8 atacmd[12], buffer[18];
    memset(atacmd, 0, sizeof(atacmd));
    atacmd[0] = ATA_CMD_REQUEST_SENSE;
    atacmd[4] = sizeof(buffer);
    int ret = ata_cmd_packet(driveid, atacmd, sizeof(atacmd), sizeof(buffer)
                             , MAKE_FLATPTR(GET_SEG(SS), buffer));
    if (ret)
        return ret;

    *asc = buffer[12];
    *ascq = buffer[13];

    return 0;
}

// Request capacity
static int
atapi_read_capacity(int driveid, u32 *blksize, u32 *sectors)
{
    u8 packet[12], buf[8];
    memset(packet, 0, sizeof(packet));
    packet[0] = 0x25; /* READ CAPACITY */
    int ret = ata_cmd_packet(driveid, packet, sizeof(packet), sizeof(buf)
                             , MAKE_FLATPTR(GET_SEG(SS), buf));
    if (ret)
        return ret;

    *blksize = (((u32)buf[4] << 24) | ((u32)buf[5] << 16)
                | ((u32)buf[6] << 8) | ((u32)buf[7] << 0));
    *sectors = (((u32)buf[0] << 24) | ((u32)buf[1] << 16)
                | ((u32)buf[2] << 8) | ((u32)buf[3] << 0));

    return 0;
}

static int
atapi_is_ready(u16 driveid)
{
    dprintf(6, "atapi_is_ready (driveid=%d)\n", driveid);

    /* Retry READ CAPACITY for 5 seconds unless MEDIUM NOT PRESENT is
     * reported by the device.  If the device reports "IN PROGRESS",
     * 30 seconds is added. */
    u32 blksize, sectors;
    int in_progress = 0;
    u64 end = calc_future_tsc(5000);
    for (;;) {
        if (rdtscll() > end) {
            dprintf(1, "read capacity failed\n");
            return -1;
        }

        int ret = atapi_read_capacity(driveid, &blksize, &sectors);
        if (!ret)
            // Success
            break;

        u8 asc, ascq;
        ret = atapi_get_sense(driveid, &asc, &ascq);
        if (ret)
            // Error - retry.
            continue;

        // Sense succeeded.
        if (asc == 0x3a) { /* MEDIUM NOT PRESENT */
            dprintf(1, "Device reports MEDIUM NOT PRESENT\n");
            return -1;
        }

        if (asc == 0x04 && ascq == 0x01 && !in_progress) {
            /* IN PROGRESS OF BECOMING READY */
            printf("Waiting for device to detect medium... ");
            /* Allow 30 seconds more */
            end = calc_future_tsc(30000);
            in_progress = 1;
        }
    }

    if (blksize != GET_GLOBAL(Drives.drives[driveid].blksize)) {
        printf("Unsupported sector size %u\n", blksize);
        return -1;
    }

    dprintf(6, "sectors=%u\n", sectors);
    printf("%dMB medium detected\n", sectors>>(20-11));
    return 0;
}

int
cdrom_boot(int cdid)
{
    // Verify device is a cdrom.
    if (cdid >= Drives.cdcount)
        return 1;
    int driveid = GET_GLOBAL(Drives.idmap[1][cdid]);

    int ret = atapi_is_ready(driveid);
    if (ret)
        dprintf(1, "atapi_is_ready returned %d\n", ret);

    // Read the Boot Record Volume Descriptor
    u8 buffer[2048];
    struct disk_op_s dop;
    memset(&dop, 0, sizeof(dop));
    dop.driveid = driveid;
    dop.lba = 0x11;
    dop.count = 1;
    dop.buf_fl = MAKE_FLATPTR(GET_SEG(SS), buffer);
    ret = cdrom_read(&dop);
    if (ret)
        return 3;

    // Validity checks
    if (buffer[0])
        return 4;
    if (strcmp((char*)&buffer[1], "CD001\001EL TORITO SPECIFICATION") != 0)
        return 5;

    // ok, now we calculate the Boot catalog address
    u32 lba = *(u32*)&buffer[0x47];

    // And we read the Boot Catalog
    dop.lba = lba;
    ret = cdrom_read(&dop);
    if (ret)
        return 7;

    // Validation entry
    if (buffer[0x00] != 0x01)
        return 8;   // Header
    if (buffer[0x01] != 0x00)
        return 9;   // Platform
    if (buffer[0x1E] != 0x55)
        return 10;  // key 1
    if (buffer[0x1F] != 0xAA)
        return 10;  // key 2

    // Initial/Default Entry
    if (buffer[0x20] != 0x88)
        return 11; // Bootable

    u16 ebda_seg = get_ebda_seg();
    u8 media = buffer[0x21];
    SET_EBDA2(ebda_seg, cdemu.media, media);

    SET_EBDA2(ebda_seg, cdemu.emulated_driveid, driveid);

    u16 boot_segment = *(u16*)&buffer[0x22];
    if (!boot_segment)
        boot_segment = 0x07C0;
    SET_EBDA2(ebda_seg, cdemu.load_segment, boot_segment);
    SET_EBDA2(ebda_seg, cdemu.buffer_segment, 0x0000);

    u16 nbsectors = *(u16*)&buffer[0x26];
    SET_EBDA2(ebda_seg, cdemu.sector_count, nbsectors);

    lba = *(u32*)&buffer[0x28];
    SET_EBDA2(ebda_seg, cdemu.ilba, lba);

    // And we read the image in memory
    dop.lba = lba;
    dop.count = DIV_ROUND_UP(nbsectors, 4);
    dop.buf_fl = MAKE_FLATPTR(boot_segment, 0);
    ret = cdrom_read(&dop);
    if (ret)
        return 12;

    if (media == 0) {
        // No emulation requested - return success.
        SET_EBDA2(ebda_seg, cdemu.emulated_extdrive, 0xE0 + cdid);
        return 0;
    }

    // Emulation of a floppy/harddisk requested
    if (! CONFIG_CDROM_EMU)
        return 13;

    // Set emulated drive id and increase bios installed hardware
    // number of devices
    if (media < 4) {
        // Floppy emulation
        SET_EBDA2(ebda_seg, cdemu.emulated_extdrive, 0x00);
        SETBITS_BDA(equipment_list_flags, 0x41);

        switch (media) {
        case 0x01:  // 1.2M floppy
            SET_EBDA2(ebda_seg, cdemu.lchs.spt, 15);
            SET_EBDA2(ebda_seg, cdemu.lchs.cylinders, 80);
            SET_EBDA2(ebda_seg, cdemu.lchs.heads, 2);
            break;
        case 0x02:  // 1.44M floppy
            SET_EBDA2(ebda_seg, cdemu.lchs.spt, 18);
            SET_EBDA2(ebda_seg, cdemu.lchs.cylinders, 80);
            SET_EBDA2(ebda_seg, cdemu.lchs.heads, 2);
            break;
        case 0x03:  // 2.88M floppy
            SET_EBDA2(ebda_seg, cdemu.lchs.spt, 36);
            SET_EBDA2(ebda_seg, cdemu.lchs.cylinders, 80);
            SET_EBDA2(ebda_seg, cdemu.lchs.heads, 2);
            break;
        }
    } else {
        // Harddrive emulation
        SET_EBDA2(ebda_seg, cdemu.emulated_extdrive, 0x80);
        SET_BDA(hdcount, GET_BDA(hdcount) + 1);

        // Peak at partition table to get chs.
        struct mbr_s *mbr = (void*)0;
        u8 sptcyl = GET_FARVAR(boot_segment, mbr->partitions[0].last.sptcyl);
        u8 cyllow = GET_FARVAR(boot_segment, mbr->partitions[0].last.cyllow);
        u8 heads = GET_FARVAR(boot_segment, mbr->partitions[0].last.heads);

        SET_EBDA2(ebda_seg, cdemu.lchs.spt, sptcyl & 0x3f);
        SET_EBDA2(ebda_seg, cdemu.lchs.cylinders
                  , ((sptcyl<<2)&0x300) + cyllow + 1);
        SET_EBDA2(ebda_seg, cdemu.lchs.heads, heads + 1);
    }

    // everything is ok, so from now on, the emulation is active
    SET_EBDA2(ebda_seg, cdemu.active, 0x01);
    dprintf(6, "cdemu media=%d\n", media);

    return 0;
}
