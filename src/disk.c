// 16bit code to access hard drives.
//
// Copyright (C) 2008  Kevin O'Connor <kevin@koconnor.net>
// Copyright (C) 2002  MandrakeSoft S.A.
//
// This file may be distributed under the terms of the GNU LGPLv3 license.

#include "disk.h" // floppy_13
#include "biosvar.h" // SET_BDA
#include "config.h" // CONFIG_*
#include "util.h" // debug_enter
#include "pic.h" // eoi_pic2
#include "bregs.h" // struct bregs
#include "pci.h" // pci_bdf_to_bus
#include "ata.h" // ATA_CB_DC


/****************************************************************
 * Helper functions
 ****************************************************************/

void
__disk_ret(struct bregs *regs, u32 linecode, const char *fname)
{
    u8 code = linecode;
    if (regs->dl < EXTSTART_HD)
        SET_BDA(floppy_last_status, code);
    else
        SET_BDA(disk_last_status, code);
    if (code)
        __set_code_fail(regs, linecode, fname);
    else
        set_code_success(regs);
}

static void
__disk_stub(struct bregs *regs, int lineno, const char *fname)
{
    __debug_stub(regs, lineno, fname);
    __disk_ret(regs, DISK_RET_SUCCESS | (lineno << 8), fname);
}

#define DISK_STUB(regs)                         \
    __disk_stub((regs), __LINE__, __func__)

// Obtain the requested disk lba from an old-style chs request.
static int
legacy_lba(struct bregs *regs, u16 lchs_seg, struct chs_s *lchs_far)
{
    u8 count = regs->al;
    u16 cylinder = regs->ch | ((((u16)regs->cl) << 2) & 0x300);
    u16 sector = regs->cl & 0x3f;
    u16 head = regs->dh;

    if (count > 128 || count == 0 || sector == 0) {
        dprintf(1, "int13_harddisk: function %02x, parameter out of range!\n"
                , regs->ah);
        disk_ret(regs, DISK_RET_EPARAM);
        return -1;
    }

    u16 nlc = GET_FARVAR(lchs_seg, lchs_far->cylinders);
    u16 nlh = GET_FARVAR(lchs_seg, lchs_far->heads);
    u16 nlspt = GET_FARVAR(lchs_seg, lchs_far->spt);

    // sanity check on cyl heads, sec
    if (cylinder >= nlc || head >= nlh || sector > nlspt) {
        dprintf(1, "int13_harddisk: function %02x, parameters out of"
                " range %04x/%04x/%04x!\n"
                , regs->ah, cylinder, head, sector);
        disk_ret(regs, DISK_RET_EPARAM);
        return -1;
    }

    // translate lchs to lba
    return (((((u32)cylinder * (u32)nlh) + (u32)head) * (u32)nlspt)
            + (u32)sector - 1);
}

// Perform read/write/verify using old-style chs accesses
static void
basic_access(struct bregs *regs, u8 driveid, u16 command)
{
    struct disk_op_s dop;
    dop.driveid = driveid;
    dop.command = command;
    int lba = legacy_lba(regs, get_global_seg(), &Drives.drives[driveid].lchs);
    if (lba < 0)
        return;
    dop.lba = lba;
    dop.count = regs->al;
    dop.buf_fl = MAKE_FLATPTR(regs->es, regs->bx);

    int status = send_disk_op(&dop);

    regs->al = dop.count;

    disk_ret(regs, status);
}

// Perform cdemu read/verify
void
cdemu_access(struct bregs *regs, u8 driveid, u16 command)
{
    struct disk_op_s dop;
    dop.driveid = driveid;
    dop.command = command;
    u16 ebda_seg = get_ebda_seg();
    int vlba = legacy_lba(
        regs, ebda_seg
        , (void*)offsetof(struct extended_bios_data_area_s, cdemu.lchs));
    if (vlba < 0)
        return;
    dop.lba = GET_EBDA2(ebda_seg, cdemu.ilba) + vlba / 4;
    u8 count = regs->al;
    u8 *cdbuf_far = (void*)offsetof(struct extended_bios_data_area_s, cdemu_buf);
    u8 *dest_far = (void*)(regs->bx+0);
    regs->al = 0;
    int status = DISK_RET_SUCCESS;

    if (vlba & 3) {
        dop.count = 1;
        dop.buf_fl = MAKE_FLATPTR(ebda_seg, cdbuf_far);
        status = send_disk_op(&dop);
        if (status)
            goto fail;
        u8 thiscount = 4 - (vlba & 3);
        if (thiscount > count)
            thiscount = count;
        count -= thiscount;
        memcpy_far(regs->es, dest_far
                   , ebda_seg, cdbuf_far + (vlba & 3) * 512
                   , thiscount * 512);
        dest_far += thiscount * 512;
        regs->al += thiscount;
        dop.lba++;
    }

    if (count > 3) {
        dop.count = count / 4;
        dop.buf_fl = MAKE_FLATPTR(regs->es, dest_far);
        status = send_disk_op(&dop);
        regs->al += dop.count * 4;
        if (status)
            goto fail;
        u8 thiscount = count & ~3;
        count &= 3;
        dest_far += thiscount * 512;
        dop.lba += thiscount / 4;
    }

    if (count) {
        dop.count = 1;
        dop.buf_fl = MAKE_FLATPTR(ebda_seg, cdbuf_far);
        status = send_disk_op(&dop);
        if (status)
            goto fail;
        u8 thiscount = count;
        memcpy_far(regs->es, dest_far, ebda_seg, cdbuf_far, thiscount * 512);
        regs->al += thiscount;
    }

fail:
    disk_ret(regs, status);
}

// Perform read/write/verify using new-style "int13ext" accesses.
static void
extended_access(struct bregs *regs, u8 driveid, u16 command)
{
    struct disk_op_s dop;
    // Get lba and check.
    dop.lba = GET_INT13EXT(regs, lba);
    dop.command = command;
    dop.driveid = driveid;
    if (dop.lba >= GET_GLOBAL(Drives.drives[driveid].sectors)) {
        dprintf(1, "int13_harddisk: function %02x. LBA out of range\n"
                , regs->ah);
        disk_ret(regs, DISK_RET_EPARAM);
        return;
    }

    dop.buf_fl = SEGOFF_TO_FLATPTR(GET_INT13EXT(regs, data));
    dop.count = GET_INT13EXT(regs, count);

    int status = send_disk_op(&dop);

    SET_INT13EXT(regs, count, dop.count);

    disk_ret(regs, status);
}


/****************************************************************
 * Hard Drive functions
 ****************************************************************/

// disk controller reset
static void
disk_1300(struct bregs *regs, u8 driveid)
{
    struct disk_op_s dop;
    dop.driveid = driveid;
    dop.command = CMD_RESET;
    int status = send_disk_op(&dop);
    disk_ret(regs, status);
}

// read disk status
static void
disk_1301(struct bregs *regs, u8 driveid)
{
    u8 v;
    if (regs->dl < EXTSTART_HD)
        // Floppy
        v = GET_BDA(floppy_last_status);
    else
        v = GET_BDA(disk_last_status);
    regs->ah = v;
    set_cf(regs, v);
    // XXX - clear disk_last_status?
}

// read disk sectors
static void
disk_1302(struct bregs *regs, u8 driveid)
{
    basic_access(regs, driveid, CMD_READ);
}

// write disk sectors
static void
disk_1303(struct bregs *regs, u8 driveid)
{
    basic_access(regs, driveid, CMD_WRITE);
}

// verify disk sectors
static void
disk_1304(struct bregs *regs, u8 driveid)
{
    basic_access(regs, driveid, CMD_VERIFY);
    // FIXME verify
}

// format disk track
static void
disk_1305(struct bregs *regs, u8 driveid)
{
    DISK_STUB(regs);

    u16 nlh = GET_GLOBAL(Drives.drives[driveid].lchs.heads);
    u16 nlspt = GET_GLOBAL(Drives.drives[driveid].lchs.spt);

    u8 num_sectors = regs->al;
    u8 head        = regs->dh;

    if (head >= nlh || num_sectors == 0 || num_sectors > nlspt) {
        disk_ret(regs, DISK_RET_EPARAM);
        return;
    }

    struct disk_op_s dop;
    dop.driveid = driveid;
    dop.command = CMD_FORMAT;
    dop.lba = head;
    dop.count = num_sectors;
    dop.buf_fl = MAKE_FLATPTR(regs->es, regs->bx);
    int status = send_disk_op(&dop);
    disk_ret(regs, status);
}

// read disk drive parameters
static void
disk_1308(struct bregs *regs, u8 driveid)
{
    // Get logical geometry from table
    u16 nlc = GET_GLOBAL(Drives.drives[driveid].lchs.cylinders) - 1;
    u16 nlh = GET_GLOBAL(Drives.drives[driveid].lchs.heads) - 1;
    u16 nlspt = GET_GLOBAL(Drives.drives[driveid].lchs.spt);
    u8 count;
    if (regs->dl < EXTSTART_HD) {
        // Floppy
        count = GET_GLOBAL(Drives.floppycount);

        regs->bx = GET_GLOBAL(Drives.drives[driveid].floppy_type);

        // set es & di to point to 11 byte diskette param table in ROM
        regs->es = SEG_BIOS;
        regs->di = (u32)&diskette_param_table2;
    } else if (regs->dl < EXTSTART_CD) {
        // Hard drive
        count = GET_BDA(hdcount);
        nlc--;  // last sector reserved
    } else {
        // Not supported on CDROM
        disk_ret(regs, DISK_RET_EPARAM);
        return;
    }

    regs->al = 0;
    regs->ch = nlc & 0xff;
    regs->cl = ((nlc >> 2) & 0xc0) | (nlspt & 0x3f);
    regs->dh = nlh;

    disk_ret(regs, DISK_RET_SUCCESS);
    regs->dl = count;
}

// initialize drive parameters
static void
disk_1309(struct bregs *regs, u8 driveid)
{
    DISK_STUB(regs);
}

// seek to specified cylinder
static void
disk_130c(struct bregs *regs, u8 driveid)
{
    DISK_STUB(regs);
}

// alternate disk reset
static void
disk_130d(struct bregs *regs, u8 driveid)
{
    DISK_STUB(regs);
}

// check drive ready
static void
disk_1310(struct bregs *regs, u8 driveid)
{
    // should look at 40:8E also???

    struct disk_op_s dop;
    dop.driveid = driveid;
    dop.command = CMD_ISREADY;
    int status = send_disk_op(&dop);
    disk_ret(regs, status);
}

// recalibrate
static void
disk_1311(struct bregs *regs, u8 driveid)
{
    DISK_STUB(regs);
}

// controller internal diagnostic
static void
disk_1314(struct bregs *regs, u8 driveid)
{
    DISK_STUB(regs);
}

// read disk drive size
static void
disk_1315(struct bregs *regs, u8 driveid)
{
    disk_ret(regs, DISK_RET_SUCCESS);
    if (regs->dl < EXTSTART_HD || regs->dl >= EXTSTART_CD) {
        // Floppy or cdrom
        regs->ah = 1;
        return;
    }
    // Hard drive

    // Get logical geometry from table
    u16 nlc   = GET_GLOBAL(Drives.drives[driveid].lchs.cylinders);
    u16 nlh   = GET_GLOBAL(Drives.drives[driveid].lchs.heads);
    u16 nlspt = GET_GLOBAL(Drives.drives[driveid].lchs.spt);

    // Compute sector count seen by int13
    u32 lba = (u32)(nlc - 1) * (u32)nlh * (u32)nlspt;
    regs->cx = lba >> 16;
    regs->dx = lba & 0xffff;
    regs->ah = 3; // hard disk accessible
}

static void
disk_1316(struct bregs *regs, u8 driveid)
{
    if (regs->dl >= EXTSTART_HD) {
        // Hard drive
        disk_ret(regs, DISK_RET_EPARAM);
        return;
    }
    disk_ret(regs, DISK_RET_ECHANGED);
}

// IBM/MS installation check
static void
disk_1341(struct bregs *regs, u8 driveid)
{
    regs->bx = 0xaa55;  // install check
    regs->cx = 0x0007;  // ext disk access and edd, removable supported
    disk_ret(regs, DISK_RET_SUCCESS);
    regs->ah = 0x30;    // EDD 3.0
}

// IBM/MS extended read
static void
disk_1342(struct bregs *regs, u8 driveid)
{
    extended_access(regs, driveid, CMD_READ);
}

// IBM/MS extended write
static void
disk_1343(struct bregs *regs, u8 driveid)
{
    extended_access(regs, driveid, CMD_WRITE);
}

// IBM/MS verify
static void
disk_1344(struct bregs *regs, u8 driveid)
{
    extended_access(regs, driveid, CMD_VERIFY);
}

// lock
static void
disk_134500(struct bregs *regs, u8 driveid)
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
disk_134501(struct bregs *regs, u8 driveid)
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
disk_134502(struct bregs *regs, u8 driveid)
{
    u8 locks = GET_EBDA(cdrom_locks[driveid]);
    regs->al = (locks ? 1 : 0);
    disk_ret(regs, DISK_RET_SUCCESS);
}

static void
disk_1345XX(struct bregs *regs, u8 driveid)
{
    disk_ret(regs, DISK_RET_EPARAM);
}

// IBM/MS lock/unlock drive
static void
disk_1345(struct bregs *regs, u8 driveid)
{
    if (regs->dl < EXTSTART_CD) {
        // Always success for HD
        disk_ret(regs, DISK_RET_SUCCESS);
        return;
    }

    switch (regs->al) {
    case 0x00: disk_134500(regs, driveid); break;
    case 0x01: disk_134501(regs, driveid); break;
    case 0x02: disk_134502(regs, driveid); break;
    default:   disk_1345XX(regs, driveid); break;
    }
}

// IBM/MS eject media
static void
disk_1346(struct bregs *regs, u8 driveid)
{
    if (regs->dl < EXTSTART_CD) {
        // Volume Not Removable
        disk_ret(regs, DISK_RET_ENOTREMOVABLE);
        return;
    }

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

// IBM/MS extended seek
static void
disk_1347(struct bregs *regs, u8 driveid)
{
    extended_access(regs, driveid, CMD_SEEK);
}

// IBM/MS get drive parameters
static void
disk_1348(struct bregs *regs, u8 driveid)
{
    u16 size = GET_INT13DPT(regs, size);

    // Buffer is too small
    if (size < 26) {
        disk_ret(regs, DISK_RET_EPARAM);
        return;
    }

    // EDD 1.x

    u8  type    = GET_GLOBAL(Drives.drives[driveid].type);
    u16 npc     = GET_GLOBAL(Drives.drives[driveid].pchs.cylinders);
    u16 nph     = GET_GLOBAL(Drives.drives[driveid].pchs.heads);
    u16 npspt   = GET_GLOBAL(Drives.drives[driveid].pchs.spt);
    u64 lba     = GET_GLOBAL(Drives.drives[driveid].sectors);
    u16 blksize = GET_GLOBAL(Drives.drives[driveid].blksize);

    dprintf(DEBUG_HDL_13, "disk_1348 size=%d t=%d chs=%d,%d,%d lba=%d bs=%d\n"
            , size, type, npc, nph, npspt, (u32)lba, blksize);

    SET_INT13DPT(regs, size, 26);
    if (type == DTYPE_ATAPI) {
        // 0x74 = removable, media change, lockable, max values
        SET_INT13DPT(regs, infos, 0x74);
        SET_INT13DPT(regs, cylinders, 0xffffffff);
        SET_INT13DPT(regs, heads, 0xffffffff);
        SET_INT13DPT(regs, spt, 0xffffffff);
        SET_INT13DPT(regs, sector_count, (u64)-1);
    } else {
        if (lba > (u64)npspt*nph*0x3fff) {
            SET_INT13DPT(regs, infos, 0x00); // geometry is invalid
            SET_INT13DPT(regs, cylinders, 0x3fff);
        } else {
            SET_INT13DPT(regs, infos, 0x02); // geometry is valid
            SET_INT13DPT(regs, cylinders, (u32)npc);
        }
        SET_INT13DPT(regs, heads, (u32)nph);
        SET_INT13DPT(regs, spt, (u32)npspt);
        SET_INT13DPT(regs, sector_count, lba);
    }
    SET_INT13DPT(regs, blksize, blksize);

    if (size < 30 || (type != DTYPE_ATA && type != DTYPE_ATAPI)) {
        disk_ret(regs, DISK_RET_SUCCESS);
        return;
    }

    // EDD 2.x

    u16 ebda_seg = get_ebda_seg();
    SET_INT13DPT(regs, size, 30);

    SET_INT13DPT(regs, dpte_segment, ebda_seg);
    SET_INT13DPT(regs, dpte_offset
                 , offsetof(struct extended_bios_data_area_s, dpte));

    // Fill in dpte
    u8 ataid = GET_GLOBAL(Drives.drives[driveid].cntl_id);
    u8 channel = ataid / 2;
    u8 slave = ataid % 2;
    u16 iobase1 = GET_GLOBAL(ATA_channels[channel].iobase1);
    u16 iobase2 = GET_GLOBAL(ATA_channels[channel].iobase2);
    u8 irq = GET_GLOBAL(ATA_channels[channel].irq);

    u16 options = 0;
    if (type == DTYPE_ATA) {
        u8 translation = GET_GLOBAL(Drives.drives[driveid].translation);
        if (translation != TRANSLATION_NONE) {
            options |= 1<<3; // CHS translation
            if (translation == TRANSLATION_LBA)
                options |= 1<<9;
            if (translation == TRANSLATION_RECHS)
                options |= 3<<9;
        }
    } else {
        // ATAPI
        options |= 1<<5; // removable device
        options |= 1<<6; // atapi device
    }
    options |= 1<<4; // lba translation
    if (CONFIG_ATA_PIO32)
        options |= 1<<7;

    SET_EBDA2(ebda_seg, dpte.iobase1, iobase1);
    SET_EBDA2(ebda_seg, dpte.iobase2, iobase2 + ATA_CB_DC);
    SET_EBDA2(ebda_seg, dpte.prefix, ((slave ? ATA_CB_DH_DEV1 : ATA_CB_DH_DEV0)
                                      | ATA_CB_DH_LBA));
    SET_EBDA2(ebda_seg, dpte.unused, 0xcb);
    SET_EBDA2(ebda_seg, dpte.irq, irq);
    SET_EBDA2(ebda_seg, dpte.blkcount, 1);
    SET_EBDA2(ebda_seg, dpte.dma, 0);
    SET_EBDA2(ebda_seg, dpte.pio, 0);
    SET_EBDA2(ebda_seg, dpte.options, options);
    SET_EBDA2(ebda_seg, dpte.reserved, 0);
    SET_EBDA2(ebda_seg, dpte.revision, 0x11);

    u8 sum = checksum_far(
        ebda_seg, (void*)offsetof(struct extended_bios_data_area_s, dpte), 15);
    SET_EBDA2(ebda_seg, dpte.checksum, -sum);

    if (size < 66) {
        disk_ret(regs, DISK_RET_SUCCESS);
        return;
    }

    // EDD 3.x
    SET_INT13DPT(regs, key, 0xbedd);
    SET_INT13DPT(regs, dpi_length, 36);
    SET_INT13DPT(regs, reserved1, 0);
    SET_INT13DPT(regs, reserved2, 0);

    SET_INT13DPT(regs, host_bus[0], 'P');
    SET_INT13DPT(regs, host_bus[1], 'C');
    SET_INT13DPT(regs, host_bus[2], 'I');
    SET_INT13DPT(regs, host_bus[3], 0);

    u32 bdf = GET_GLOBAL(ATA_channels[channel].pci_bdf);
    u32 path = (pci_bdf_to_bus(bdf) | (pci_bdf_to_dev(bdf) << 8)
                | (pci_bdf_to_fn(bdf) << 16));
    SET_INT13DPT(regs, iface_path, path);

    SET_INT13DPT(regs, iface_type[0], 'A');
    SET_INT13DPT(regs, iface_type[1], 'T');
    SET_INT13DPT(regs, iface_type[2], 'A');
    SET_INT13DPT(regs, iface_type[3], 0);
    SET_INT13DPT(regs, iface_type[4], 0);
    SET_INT13DPT(regs, iface_type[5], 0);
    SET_INT13DPT(regs, iface_type[6], 0);
    SET_INT13DPT(regs, iface_type[7], 0);

    SET_INT13DPT(regs, device_path, slave);

    SET_INT13DPT(regs, checksum
                 , -checksum_far(regs->ds, (void*)(regs->si+30), 35));

    disk_ret(regs, DISK_RET_SUCCESS);
}

// IBM/MS extended media change
static void
disk_1349(struct bregs *regs, u8 driveid)
{
    if (regs->dl < EXTSTART_CD) {
        // Always success for HD
        disk_ret(regs, DISK_RET_SUCCESS);
        return;
    }
    set_fail(regs);
    // always send changed ??
    regs->ah = DISK_RET_ECHANGED;
}

static void
disk_134e01(struct bregs *regs, u8 driveid)
{
    disk_ret(regs, DISK_RET_SUCCESS);
}

static void
disk_134e03(struct bregs *regs, u8 driveid)
{
    disk_ret(regs, DISK_RET_SUCCESS);
}

static void
disk_134e04(struct bregs *regs, u8 driveid)
{
    disk_ret(regs, DISK_RET_SUCCESS);
}

static void
disk_134e06(struct bregs *regs, u8 driveid)
{
    disk_ret(regs, DISK_RET_SUCCESS);
}

static void
disk_134eXX(struct bregs *regs, u8 driveid)
{
    disk_ret(regs, DISK_RET_EPARAM);
}

// IBM/MS set hardware configuration
static void
disk_134e(struct bregs *regs, u8 driveid)
{
    switch (regs->al) {
    case 0x01: disk_134e01(regs, driveid); break;
    case 0x03: disk_134e03(regs, driveid); break;
    case 0x04: disk_134e04(regs, driveid); break;
    case 0x06: disk_134e06(regs, driveid); break;
    default:   disk_134eXX(regs, driveid); break;
    }
}

void
disk_13XX(struct bregs *regs, u8 driveid)
{
    disk_ret(regs, DISK_RET_EPARAM);
}

void
disk_13(struct bregs *regs, u8 driveid)
{
    //debug_stub(regs);

    // clear completion flag
    SET_BDA(disk_interrupt_flag, 0);

    switch (regs->ah) {
    case 0x00: disk_1300(regs, driveid); break;
    case 0x01: disk_1301(regs, driveid); break;
    case 0x02: disk_1302(regs, driveid); break;
    case 0x03: disk_1303(regs, driveid); break;
    case 0x04: disk_1304(regs, driveid); break;
    case 0x05: disk_1305(regs, driveid); break;
    case 0x08: disk_1308(regs, driveid); break;
    case 0x09: disk_1309(regs, driveid); break;
    case 0x0c: disk_130c(regs, driveid); break;
    case 0x0d: disk_130d(regs, driveid); break;
    case 0x10: disk_1310(regs, driveid); break;
    case 0x11: disk_1311(regs, driveid); break;
    case 0x14: disk_1314(regs, driveid); break;
    case 0x15: disk_1315(regs, driveid); break;
    case 0x16: disk_1316(regs, driveid); break;
    case 0x41: disk_1341(regs, driveid); break;
    case 0x42: disk_1342(regs, driveid); break;
    case 0x43: disk_1343(regs, driveid); break;
    case 0x44: disk_1344(regs, driveid); break;
    case 0x45: disk_1345(regs, driveid); break;
    case 0x46: disk_1346(regs, driveid); break;
    case 0x47: disk_1347(regs, driveid); break;
    case 0x48: disk_1348(regs, driveid); break;
    case 0x49: disk_1349(regs, driveid); break;
    case 0x4e: disk_134e(regs, driveid); break;
    default:   disk_13XX(regs, driveid); break;
    }
}

static void
floppy_13(struct bregs *regs, u8 driveid)
{
    // Only limited commands are supported on floppies.
    switch (regs->ah) {
    case 0x00:
    case 0x01:
    case 0x02:
    case 0x03:
    case 0x04:
    case 0x05:
    case 0x08:
    case 0x15:
    case 0x16:
        disk_13(regs, driveid);
        break;
    default:   disk_13XX(regs, driveid); break;
    }
}


/****************************************************************
 * Entry points
 ****************************************************************/

static int
get_driveid(struct bregs *regs, u8 exttype, u8 extdriveoffset)
{
    // basic check : device has to be defined
    if (extdriveoffset >= ARRAY_SIZE(Drives.idmap[0]))
        return -1;

    // Get the ata channel
    u8 driveid = GET_GLOBAL(Drives.idmap[exttype][extdriveoffset]);

    // basic check : device has to be valid
    if (driveid >= ARRAY_SIZE(Drives.drives))
        return -1;

    return driveid;
}

static void
handle_legacy_disk(struct bregs *regs, u8 extdrive)
{
    if (! CONFIG_DRIVES) {
        // XXX - support handle_1301 anyway?
        disk_ret(regs, DISK_RET_EPARAM);
        return;
    }

    if (extdrive < EXTSTART_HD) {
        int driveid = get_driveid(regs, EXTTYPE_FLOPPY, extdrive);
        if (driveid < 0)
            goto fail;
        floppy_13(regs, driveid);
        return;
    }

    int driveid;
    if (extdrive >= EXTSTART_CD)
        driveid = get_driveid(regs, EXTTYPE_CD, extdrive - EXTSTART_CD);
    else
        driveid = get_driveid(regs, EXTTYPE_HD, extdrive - EXTSTART_HD);
    if (driveid < 0)
        goto fail;
    disk_13(regs, driveid);
    return;

fail:
    // XXX - support 1301/1308/1315 anyway?
    disk_ret(regs, DISK_RET_EPARAM);
}

void VISIBLE16
handle_40(struct bregs *regs)
{
    debug_enter(regs, DEBUG_HDL_40);
    handle_legacy_disk(regs, regs->dl);
}

// INT 13h Fixed Disk Services Entry Point
void VISIBLE16
handle_13(struct bregs *regs)
{
    debug_enter(regs, DEBUG_HDL_13);
    u8 extdrive = regs->dl;

    if (CONFIG_CDROM_EMU) {
        if (regs->ah == 0x4b) {
            cdemu_134b(regs);
            return;
        }
        u16 ebda_seg = get_ebda_seg();
        if (GET_EBDA2(ebda_seg, cdemu.active)) {
            u8 emudrive = GET_EBDA2(ebda_seg, cdemu.emulated_extdrive);
            if (extdrive == emudrive) {
                cdemu_13(regs);
                return;
            }
            if (extdrive < EXTSTART_CD && ((emudrive ^ extdrive) & 0x80) == 0)
                extdrive--;
        }
    }
    handle_legacy_disk(regs, extdrive);
}

// record completion in BIOS task complete flag
void VISIBLE16
handle_76()
{
    debug_isr(DEBUG_ISR_76);
    SET_BDA(disk_interrupt_flag, 0xff);
    eoi_pic2();
}

// Old Fixed Disk Parameter Table (newer tables are in the ebda).
struct fdpt_s OldFDPT VAR16FIXED(0xe401);
