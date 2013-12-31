// Disk setup and access
//
// Copyright (C) 2008,2009  Kevin O'Connor <kevin@koconnor.net>
// Copyright (C) 2002  MandrakeSoft S.A.
//
// This file may be distributed under the terms of the GNU LGPLv3 license.

#include "biosvar.h" // GET_GLOBAL
#include "block.h" // process_op
#include "bregs.h" // struct bregs
#include "hw/ata.h" // process_ata_op
#include "hw/ahci.h" // process_ahci_op
#include "hw/blockcmd.h" // cdb_*
#include "hw/rtc.h" // rtc_read
#include "hw/virtio-blk.h" // process_virtio_blk_op
#include "malloc.h" // malloc_low
#include "output.h" // dprintf
#include "stacks.h" // stack_hop
#include "std/disk.h" // struct dpte_s
#include "string.h" // checksum
#include "util.h" // process_floppy_op

u8 FloppyCount VARFSEG;
u8 CDCount;
struct drive_s *IDMap[3][BUILD_MAX_EXTDRIVE] VARFSEG;
u8 *bounce_buf_fl VARFSEG;
struct dpte_s DefaultDPTE VARLOW;

struct drive_s *
getDrive(u8 exttype, u8 extdriveoffset)
{
    if (extdriveoffset >= ARRAY_SIZE(IDMap[0]))
        return NULL;
    return GET_GLOBAL(IDMap[exttype][extdriveoffset]);
}

int getDriveId(u8 exttype, struct drive_s *drive)
{
    ASSERT32FLAT();
    int i;
    for (i = 0; i < ARRAY_SIZE(IDMap[0]); i++)
        if (getDrive(exttype, i) == drive)
            return i;
    return -1;
}

int create_bounce_buf(void)
{
    if (bounce_buf_fl)
        return 0;

    u8 *buf = malloc_low(CDROM_SECTOR_SIZE);
    if (!buf) {
        warn_noalloc();
        return -1;
    }
    bounce_buf_fl = buf;
    return 0;
}

/****************************************************************
 * Disk geometry translation
 ****************************************************************/

static u8
get_translation(struct drive_s *drive)
{
    u8 type = drive->type;
    if (CONFIG_QEMU && type == DTYPE_ATA) {
        // Emulators pass in the translation info via nvram.
        u8 ataid = drive->cntl_id;
        u8 channel = ataid / 2;
        u8 translation = rtc_read(CMOS_BIOS_DISKTRANSFLAG + channel/2);
        translation >>= 2 * (ataid % 4);
        translation &= 0x03;
        return translation;
    }

    // Otherwise use a heuristic to determine translation type.
    u16 heads = drive->pchs.head;
    u16 cylinders = drive->pchs.cylinder;
    u16 spt = drive->pchs.sector;
    u64 sectors = drive->sectors;
    u64 psectors = (u64)heads * cylinders * spt;
    if (!heads || !cylinders || !spt || psectors > sectors)
        // pchs doesn't look valid - use LBA.
        return TRANSLATION_LBA;

    if (cylinders <= 1024 && heads <= 16 && spt <= 63)
        return TRANSLATION_NONE;
    if (cylinders * heads <= 131072)
        return TRANSLATION_LARGE;
    return TRANSLATION_LBA;
}

static void
setup_translation(struct drive_s *drive)
{
    u8 translation = get_translation(drive);
    drive->translation = translation;

    u16 heads = drive->pchs.head ;
    u16 cylinders = drive->pchs.cylinder;
    u16 spt = drive->pchs.sector;
    u64 sectors = drive->sectors;
    const char *desc = NULL;

    switch (translation) {
    default:
    case TRANSLATION_NONE:
        desc = "none";
        break;
    case TRANSLATION_LBA:
        desc = "lba";
        spt = 63;
        if (sectors > 63*255*1024) {
            heads = 255;
            cylinders = 1024;
            break;
        }
        u32 sect = (u32)sectors / 63;
        heads = sect / 1024;
        if (heads>128)
            heads = 255;
        else if (heads>64)
            heads = 128;
        else if (heads>32)
            heads = 64;
        else if (heads>16)
            heads = 32;
        else
            heads = 16;
        cylinders = sect / heads;
        break;
    case TRANSLATION_RECHS:
        desc = "r-echs";
        // Take care not to overflow
        if (heads==16) {
            if (cylinders>61439)
                cylinders=61439;
            heads=15;
            cylinders = (u16)((u32)(cylinders)*16/15);
        }
        // then go through the large bitshift process
    case TRANSLATION_LARGE:
        if (translation == TRANSLATION_LARGE)
            desc = "large";
        while (cylinders > 1024) {
            cylinders >>= 1;
            heads <<= 1;

            // If we max out the head count
            if (heads > 127)
                break;
        }
        break;
    }
    // clip to 1024 cylinders in lchs
    if (cylinders > 1024)
        cylinders = 1024;
    dprintf(1, "drive %p: PCHS=%u/%d/%d translation=%s LCHS=%d/%d/%d s=%d\n"
            , drive
            , drive->pchs.cylinder, drive->pchs.head, drive->pchs.sector
            , desc
            , cylinders, heads, spt
            , (u32)sectors);

    drive->lchs.head = heads;
    drive->lchs.cylinder = cylinders;
    drive->lchs.sector = spt;
}


/****************************************************************
 * Drive mapping
 ****************************************************************/

// Fill in Fixed Disk Parameter Table (located in ebda).
static void
fill_fdpt(struct drive_s *drive, int hdid)
{
    if (hdid > 1)
        return;

    u16 nlc = drive->lchs.cylinder;
    u16 nlh = drive->lchs.head;
    u16 nls = drive->lchs.sector;

    u16 npc = drive->pchs.cylinder;
    u16 nph = drive->pchs.head;
    u16 nps = drive->pchs.sector;

    struct fdpt_s *fdpt = &get_ebda_ptr()->fdpt[hdid];
    fdpt->precompensation = 0xffff;
    fdpt->drive_control_byte = 0xc0 | ((nph > 8) << 3);
    fdpt->landing_zone = npc;
    fdpt->cylinders = nlc;
    fdpt->heads = nlh;
    fdpt->sectors = nls;

    if (nlc != npc || nlh != nph || nls != nps) {
        // Logical mapping present - use extended structure.

        // complies with Phoenix style Translated Fixed Disk Parameter
        // Table (FDPT)
        fdpt->phys_cylinders = npc;
        fdpt->phys_heads = nph;
        fdpt->phys_sectors = nps;
        fdpt->a0h_signature = 0xa0;

        // Checksum structure.
        fdpt->checksum -= checksum(fdpt, sizeof(*fdpt));
    }

    if (hdid == 0)
        SET_IVT(0x41, SEGOFF(get_ebda_seg(), offsetof(
                                 struct extended_bios_data_area_s, fdpt[0])));
    else
        SET_IVT(0x46, SEGOFF(get_ebda_seg(), offsetof(
                                 struct extended_bios_data_area_s, fdpt[1])));
}

// Find spot to add a drive
static void
add_drive(struct drive_s **idmap, u8 *count, struct drive_s *drive)
{
    if (*count >= ARRAY_SIZE(IDMap[0])) {
        warn_noalloc();
        return;
    }
    idmap[*count] = drive;
    *count = *count + 1;
}

// Map a hard drive
void
map_hd_drive(struct drive_s *drive)
{
    ASSERT32FLAT();
    struct bios_data_area_s *bda = MAKE_FLATPTR(SEG_BDA, 0);
    int hdid = bda->hdcount;
    dprintf(3, "Mapping hd drive %p to %d\n", drive, hdid);
    add_drive(IDMap[EXTTYPE_HD], &bda->hdcount, drive);

    // Setup disk geometry translation.
    setup_translation(drive);

    // Fill "fdpt" structure.
    fill_fdpt(drive, hdid);
}

// Map a cd
void
map_cd_drive(struct drive_s *drive)
{
    ASSERT32FLAT();
    dprintf(3, "Mapping cd drive %p\n", drive);
    add_drive(IDMap[EXTTYPE_CD], &CDCount, drive);
}

// Map a floppy
void
map_floppy_drive(struct drive_s *drive)
{
    ASSERT32FLAT();
    dprintf(3, "Mapping floppy drive %p\n", drive);
    add_drive(IDMap[EXTTYPE_FLOPPY], &FloppyCount, drive);

    // Update equipment word bits for floppy
    if (FloppyCount == 1) {
        // 1 drive, ready for boot
        set_equipment_flags(0x41, 0x01);
        SET_BDA(floppy_harddisk_info, 0x07);
    } else if (FloppyCount >= 2) {
        // 2 drives, ready for boot
        set_equipment_flags(0x41, 0x41);
        SET_BDA(floppy_harddisk_info, 0x77);
    }
}


/****************************************************************
 * Return status functions
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
        __set_code_invalid(regs, linecode, fname);
    else
        set_code_success(regs);
}

void
__disk_ret_unimplemented(struct bregs *regs, u32 linecode, const char *fname)
{
    u8 code = linecode;
    if (regs->dl < EXTSTART_HD)
        SET_BDA(floppy_last_status, code);
    else
        SET_BDA(disk_last_status, code);
    __set_code_unimplemented(regs, linecode, fname);
}


/****************************************************************
 * 16bit calling interface
 ****************************************************************/

int VISIBLE32FLAT
process_scsi_op(struct disk_op_s *op)
{
    switch (op->command) {
    case CMD_READ:
        return cdb_read(op);
    case CMD_WRITE:
        return cdb_write(op);
    case CMD_FORMAT:
    case CMD_RESET:
    case CMD_ISREADY:
    case CMD_VERIFY:
    case CMD_SEEK:
        return DISK_RET_SUCCESS;
    default:
        return DISK_RET_EPARAM;
    }
}

int VISIBLE32FLAT
process_atapi_op(struct disk_op_s *op)
{
    switch (op->command) {
    case CMD_WRITE:
    case CMD_FORMAT:
        return DISK_RET_EWRITEPROTECT;
    default:
        return process_scsi_op(op);
    }
}

// Execute a disk_op request.
int
process_op(struct disk_op_s *op)
{
    ASSERT16();
    int ret, origcount = op->count;
    u8 type = GET_GLOBALFLAT(op->drive_gf->type);
    switch (type) {
    case DTYPE_FLOPPY:
        ret = process_floppy_op(op);
        break;
    case DTYPE_ATA:
        ret = process_ata_op(op);
        break;
    case DTYPE_RAMDISK:
        ret = process_ramdisk_op(op);
        break;
    case DTYPE_CDEMU:
        ret = process_cdemu_op(op);
        break;
    case DTYPE_VIRTIO_BLK:
        ret = process_virtio_blk_op(op);
        break;
    case DTYPE_AHCI: ;
        extern void _cfunc32flat_process_ahci_op(void);
        ret = call32(_cfunc32flat_process_ahci_op
                     , (u32)MAKE_FLATPTR(GET_SEG(SS), op), DISK_RET_EPARAM);
        break;
    case DTYPE_ATA_ATAPI:
        ret = process_atapi_op(op);
        break;
    case DTYPE_AHCI_ATAPI: ;
        extern void _cfunc32flat_process_atapi_op(void);
        ret = call32(_cfunc32flat_process_atapi_op
                     , (u32)MAKE_FLATPTR(GET_SEG(SS), op), DISK_RET_EPARAM);
        break;
    case DTYPE_USB:
    case DTYPE_UAS:
    case DTYPE_VIRTIO_SCSI:
    case DTYPE_LSI_SCSI:
    case DTYPE_ESP_SCSI:
    case DTYPE_MEGASAS:
        ret = process_scsi_op(op);
        break;
    case DTYPE_USB_32:
    case DTYPE_UAS_32:
    case DTYPE_PVSCSI: ;
        extern void _cfunc32flat_process_scsi_op(void);
        ret = call32(_cfunc32flat_process_scsi_op
                     , (u32)MAKE_FLATPTR(GET_SEG(SS), op), DISK_RET_EPARAM);
        break;
    default:
        ret = DISK_RET_EPARAM;
        break;
    }
    if (ret && op->count == origcount)
        // If the count hasn't changed on error, assume no data transferred.
        op->count = 0;
    return ret;
}

// Execute a "disk_op_s" request - this runs on the extra stack.
static int
__send_disk_op(struct disk_op_s *op_far, u16 op_seg)
{
    struct disk_op_s dop;
    memcpy_far(GET_SEG(SS), &dop
               , op_seg, op_far
               , sizeof(dop));

    dprintf(DEBUG_HDL_13, "disk_op d=%p lba=%d buf=%p count=%d cmd=%d\n"
            , dop.drive_gf, (u32)dop.lba, dop.buf_fl
            , dop.count, dop.command);

    int status = process_op(&dop);

    // Update count with total sectors transferred.
    SET_FARVAR(op_seg, op_far->count, dop.count);

    return status;
}

// Execute a "disk_op_s" request by jumping to the extra 16bit stack.
int
send_disk_op(struct disk_op_s *op)
{
    ASSERT16();
    if (! CONFIG_DRIVES)
        return -1;

    return stack_hop((u32)op, GET_SEG(SS), __send_disk_op);
}
