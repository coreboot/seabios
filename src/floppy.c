// 16bit code to access floppy drives.
//
// Copyright (C) 2008,2009  Kevin O'Connor <kevin@koconnor.net>
// Copyright (C) 2002  MandrakeSoft S.A.
//
// This file may be distributed under the terms of the GNU LGPLv3 license.

#include "types.h" // u8
#include "disk.h" // DISK_RET_SUCCESS
#include "config.h" // CONFIG_FLOPPY
#include "biosvar.h" // SET_BDA
#include "util.h" // dprintf
#include "cmos.h" // inb_cmos
#include "pic.h" // eoi_pic1
#include "bregs.h" // struct bregs
#include "boot.h" // boot_add_floppy
#include "pci.h" // pci_to_bdf
#include "pci_ids.h" // PCI_CLASS_BRIDGE_ISA

#define FLOPPY_SIZE_CODE 0x02 // 512 byte sectors
#define FLOPPY_DATALEN 0xff   // Not used - because size code is 0x02
#define FLOPPY_MOTOR_TICKS 37 // ~2 seconds
#define FLOPPY_FILLBYTE 0xf6
#define FLOPPY_GAPLEN 0x1B
#define FLOPPY_FORMAT_GAPLEN 0x6c
#define FLOPPY_PIO_TIMEOUT 1000

// New diskette parameter table adding 3 parameters from IBM
// Since no provisions are made for multiple drive types, most
// values in this table are ignored.  I set parameters for 1.44M
// floppy here
struct floppy_ext_dbt_s diskette_param_table2 VARFSEG = {
    .dbt = {
        .specify1       = 0xAF, // step rate 12ms, head unload 240ms
        .specify2       = 0x02, // head load time 4ms, DMA used
        .shutoff_ticks  = FLOPPY_MOTOR_TICKS, // ~2 seconds
        .bps_code       = FLOPPY_SIZE_CODE,
        .sectors        = 18,
        .interblock_len = FLOPPY_GAPLEN,
        .data_len       = FLOPPY_DATALEN,
        .gap_len        = FLOPPY_FORMAT_GAPLEN,
        .fill_byte      = FLOPPY_FILLBYTE,
        .settle_time    = 0x0F, // 15ms
        .startup_time   = 0x08, // 1 second
    },
    .max_track      = 79,   // maximum track
    .data_rate      = 0,    // data transfer rate
    .drive_type     = 4,    // drive type in cmos
};

struct floppy_dbt_s diskette_param_table VAR16FIXED(0xefc7);

struct floppyinfo_s {
    struct chs_s chs;
    u8 floppy_size;
    u8 data_rate;
};

#define FLOPPY_SIZE_525 0x01
#define FLOPPY_SIZE_350 0x02

#define FLOPPY_RATE_500K 0x00
#define FLOPPY_RATE_300K 0x01
#define FLOPPY_RATE_250K 0x02
#define FLOPPY_RATE_1M   0x03

struct floppyinfo_s FloppyInfo[] VARFSEG = {
    // Unknown
    { {0, 0, 0}, 0x00, 0x00},
    // 1 - 360KB, 5.25" - 2 heads, 40 tracks, 9 sectors
    { {2, 40, 9}, FLOPPY_SIZE_525, FLOPPY_RATE_300K},
    // 2 - 1.2MB, 5.25" - 2 heads, 80 tracks, 15 sectors
    { {2, 80, 15}, FLOPPY_SIZE_525, FLOPPY_RATE_500K},
    // 3 - 720KB, 3.5"  - 2 heads, 80 tracks, 9 sectors
    { {2, 80, 9}, FLOPPY_SIZE_350, FLOPPY_RATE_250K},
    // 4 - 1.44MB, 3.5" - 2 heads, 80 tracks, 18 sectors
    { {2, 80, 18}, FLOPPY_SIZE_350, FLOPPY_RATE_500K},
    // 5 - 2.88MB, 3.5" - 2 heads, 80 tracks, 36 sectors
    { {2, 80, 36}, FLOPPY_SIZE_350, FLOPPY_RATE_1M},
    // 6 - 160k, 5.25"  - 1 heads, 40 tracks, 8 sectors
    { {1, 40, 8}, FLOPPY_SIZE_525, FLOPPY_RATE_250K},
    // 7 - 180k, 5.25"  - 1 heads, 40 tracks, 9 sectors
    { {1, 40, 9}, FLOPPY_SIZE_525, FLOPPY_RATE_300K},
    // 8 - 320k, 5.25"  - 2 heads, 40 tracks, 8 sectors
    { {2, 40, 8}, FLOPPY_SIZE_525, FLOPPY_RATE_250K},
};

struct drive_s *
init_floppy(int floppyid, int ftype)
{
    if (ftype <= 0 || ftype >= ARRAY_SIZE(FloppyInfo)) {
        dprintf(1, "Bad floppy type %d\n", ftype);
        return NULL;
    }

    struct drive_s *drive_g = malloc_fseg(sizeof(*drive_g));
    if (!drive_g) {
        warn_noalloc();
        return NULL;
    }
    memset(drive_g, 0, sizeof(*drive_g));
    drive_g->cntl_id = floppyid;
    drive_g->type = DTYPE_FLOPPY;
    drive_g->blksize = DISK_SECTOR_SIZE;
    drive_g->floppy_type = ftype;
    drive_g->sectors = (u64)-1;

    memcpy(&drive_g->lchs, &FloppyInfo[ftype].chs
           , sizeof(FloppyInfo[ftype].chs));
    return drive_g;
}

static void
addFloppy(int floppyid, int ftype)
{
    struct drive_s *drive_g = init_floppy(floppyid, ftype);
    if (!drive_g)
        return;
    char *desc = znprintf(MAXDESCSIZE, "Floppy [drive %c]", 'A' + floppyid);
    struct pci_device *pci = pci_find_class(PCI_CLASS_BRIDGE_ISA); /* isa-to-pci bridge */
    int prio = bootprio_find_fdc_device(pci, PORT_FD_BASE, floppyid);
    boot_add_floppy(drive_g, desc, prio);
}

void
floppy_setup(void)
{
    memcpy(&diskette_param_table, &diskette_param_table2
           , sizeof(diskette_param_table));
    SET_IVT(0x1E, SEGOFF(SEG_BIOS
                         , (u32)&diskette_param_table2 - BUILD_BIOS_ADDR));

    if (! CONFIG_FLOPPY)
        return;
    dprintf(3, "init floppy drives\n");

    if (CONFIG_QEMU) {
        u8 type = inb_cmos(CMOS_FLOPPY_DRIVE_TYPE);
        if (type & 0xf0)
            addFloppy(0, type >> 4);
        if (type & 0x0f)
            addFloppy(1, type & 0x0f);
    } else {
        u8 type = romfile_loadint("etc/floppy0", 0);
        if (type)
            addFloppy(0, type);
        type = romfile_loadint("etc/floppy1", 0);
        if (type)
            addFloppy(1, type);
    }

    outb(0x02, PORT_DMA1_MASK_REG);

    enable_hwirq(6, FUNC16(entry_0e));
}

// Find a floppy type that matches a given image size.
int
find_floppy_type(u32 size)
{
    int i;
    for (i=1; i<ARRAY_SIZE(FloppyInfo); i++) {
        struct chs_s *c = &FloppyInfo[i].chs;
        if (c->cylinders * c->heads * c->spt * DISK_SECTOR_SIZE == size)
            return i;
    }
    return -1;
}


/****************************************************************
 * Low-level floppy IO
 ****************************************************************/

static void
floppy_disable_controller(void)
{
    outb(inb(PORT_FD_DOR) & ~0x04, PORT_FD_DOR);
}

static int
floppy_wait_irq(void)
{
    u8 frs = GET_BDA(floppy_recalibration_status);
    SET_BDA(floppy_recalibration_status, frs & ~FRS_IRQ);
    for (;;) {
        if (!GET_BDA(floppy_motor_counter)) {
            floppy_disable_controller();
            return DISK_RET_ETIMEOUT;
        }
        frs = GET_BDA(floppy_recalibration_status);
        if (frs & FRS_IRQ)
            break;
        // Could use yield_toirq() here, but that causes issues on
        // bochs, so use yield() instead.
        yield();
    }

    SET_BDA(floppy_recalibration_status, frs & ~FRS_IRQ);
    return DISK_RET_SUCCESS;
}

struct floppy_pio_s {
    u8 cmdlen;
    u8 resplen;
    u8 waitirq;
    u8 data[9];
};

static int
floppy_pio(struct floppy_pio_s *pio)
{
    // Send command to controller.
    u64 end = calc_future_tsc(FLOPPY_PIO_TIMEOUT);
    int i = 0;
    for (;;) {
        u8 sts = inb(PORT_FD_STATUS);
        if (!(sts & 0x80)) {
            if (check_tsc(end)) {
                floppy_disable_controller();
                return DISK_RET_ETIMEOUT;
            }
            continue;
        }
        if (sts & 0x40) {
            floppy_disable_controller();
            return DISK_RET_ECONTROLLER;
        }
        outb(pio->data[i++], PORT_FD_DATA);
        if (i >= pio->cmdlen)
            break;
    }

    // Wait for command to complete.
    if (pio->waitirq) {
        int ret = floppy_wait_irq();
        if (ret)
            return ret;
    }

    // Read response from controller.
    end = calc_future_tsc(FLOPPY_PIO_TIMEOUT);
    i = 0;
    for (;;) {
        u8 sts = inb(PORT_FD_STATUS);
        if (!(sts & 0x80)) {
            if (check_tsc(end)) {
                floppy_disable_controller();
                return DISK_RET_ETIMEOUT;
            }
            continue;
        }
        if (i >= pio->resplen)
            break;
        if (!(sts & 0x40)) {
            floppy_disable_controller();
            return DISK_RET_ECONTROLLER;
        }
        pio->data[i++] = inb(PORT_FD_DATA);
    }

    return DISK_RET_SUCCESS;
}

static int
floppy_enable_controller(void)
{
    outb(inb(PORT_FD_DOR) | 0x04, PORT_FD_DOR);
    int ret = floppy_wait_irq();
    if (ret)
        return ret;

    struct floppy_pio_s pio;
    pio.cmdlen = 1;
    pio.resplen = 2;
    pio.waitirq = 0;
    pio.data[0] = 0x08;  // 08: Check Interrupt Status
    return floppy_pio(&pio);
}

static int
floppy_select_drive(u8 floppyid)
{
    // reset the disk motor timeout value of INT 08
    SET_BDA(floppy_motor_counter, FLOPPY_MOTOR_TICKS);

    // Enable controller if it isn't running.
    u8 dor = inb(PORT_FD_DOR);
    if (!(dor & 0x04)) {
        int ret = floppy_enable_controller();
        if (ret)
            return ret;
    }

    // Turn on motor of selected drive, DMA & int enabled, normal operation
    dor = (floppyid ? 0x20 : 0x10) | 0x0c | floppyid;
    outb(dor, PORT_FD_DOR);

    return DISK_RET_SUCCESS;
}


/****************************************************************
 * Floppy media sense
 ****************************************************************/

static inline void
set_diskette_current_cyl(u8 floppyid, u8 cyl)
{
    SET_BDA(floppy_track[floppyid], cyl);
}

static int
floppy_drive_recal(u8 floppyid)
{
    int ret = floppy_select_drive(floppyid);
    if (ret)
        return ret;

    // send Recalibrate command (2 bytes) to controller
    struct floppy_pio_s pio;
    pio.cmdlen = 2;
    pio.resplen = 0;
    pio.waitirq = 1;
    pio.data[0] = 0x07;  // 07: Recalibrate
    pio.data[1] = floppyid; // 0=drive0, 1=drive1
    ret = floppy_pio(&pio);
    if (ret)
        return ret;

    pio.cmdlen = 1;
    pio.resplen = 2;
    pio.waitirq = 0;
    pio.data[0] = 0x08;  // 08: Check Interrupt Status
    ret = floppy_pio(&pio);
    if (ret)
        return ret;

    u8 frs = GET_BDA(floppy_recalibration_status);
    SET_BDA(floppy_recalibration_status, frs | (1<<floppyid));
    set_diskette_current_cyl(floppyid, 0);
    return DISK_RET_SUCCESS;
}

static int
floppy_drive_readid(u8 floppyid, u8 data_rate, u8 head)
{
    int ret = floppy_select_drive(floppyid);
    if (ret)
        return ret;

    // Set data rate.
    outb(data_rate, PORT_FD_DIR);

    // send Read Sector Id command
    struct floppy_pio_s pio;
    pio.cmdlen = 2;
    pio.resplen = 7;
    pio.waitirq = 1;
    pio.data[0] = 0x4a;  // 0a: Read Sector Id
    pio.data[1] = (head << 2) | floppyid; // HD DR1 DR2
    ret = floppy_pio(&pio);
    if (ret)
        return ret;
    if (pio.data[0] & 0xc0)
        return -1;
    return 0;
}

static int
floppy_media_sense(struct drive_s *drive_g)
{
    u8 ftype = GET_GLOBAL(drive_g->floppy_type), stype = ftype;
    u8 floppyid = GET_GLOBAL(drive_g->cntl_id);

    u8 data_rate = GET_GLOBAL(FloppyInfo[stype].data_rate);
    int ret = floppy_drive_readid(floppyid, data_rate, 0);
    if (ret) {
        // Attempt media sense.
        for (stype=1; ; stype++) {
            if (stype >= ARRAY_SIZE(FloppyInfo))
                return DISK_RET_EMEDIA;
            if (stype==ftype
                || (GET_GLOBAL(FloppyInfo[stype].floppy_size)
                    != GET_GLOBAL(FloppyInfo[ftype].floppy_size))
                || (GET_GLOBAL(FloppyInfo[stype].chs.heads)
                    > GET_GLOBAL(FloppyInfo[ftype].chs.heads))
                || (GET_GLOBAL(FloppyInfo[stype].chs.cylinders)
                    > GET_GLOBAL(FloppyInfo[ftype].chs.cylinders))
                || (GET_GLOBAL(FloppyInfo[stype].chs.spt)
                    > GET_GLOBAL(FloppyInfo[ftype].chs.spt)))
                continue;
            data_rate = GET_GLOBAL(FloppyInfo[stype].data_rate);
            ret = floppy_drive_readid(floppyid, data_rate, 0);
            if (!ret)
                break;
        }
    }

    u8 old_data_rate = GET_BDA(floppy_media_state[floppyid]) >> 6;
    SET_BDA(floppy_last_data_rate, (old_data_rate<<2) | (data_rate<<6));
    u8 media = (stype == 1 ? 0x04 : (stype == 2 ? 0x05 : 0x07));
    u8 fms = (data_rate<<6) | FMS_MEDIA_DRIVE_ESTABLISHED | media;
    if (GET_GLOBAL(FloppyInfo[stype].chs.cylinders)
        < GET_GLOBAL(FloppyInfo[ftype].chs.cylinders))
        fms |= FMS_DOUBLE_STEPPING;
    SET_BDA(floppy_media_state[floppyid], fms);

    return DISK_RET_SUCCESS;
}

static int
check_recal_drive(struct drive_s *drive_g)
{
    u8 floppyid = GET_GLOBAL(drive_g->cntl_id);
    if ((GET_BDA(floppy_recalibration_status) & (1<<floppyid))
        && (GET_BDA(floppy_media_state[floppyid]) & FMS_MEDIA_DRIVE_ESTABLISHED))
        // Media is known.
        return DISK_RET_SUCCESS;

    // Recalibrate drive.
    int ret = floppy_drive_recal(floppyid);
    if (ret)
        return ret;

    // Sense media.
    return floppy_media_sense(drive_g);
}


/****************************************************************
 * Floppy DMA
 ****************************************************************/

// Perform a floppy transfer command (setup DMA and issue PIO).
static int
floppy_cmd(struct disk_op_s *op, int blocksize, struct floppy_pio_s *pio)
{
    int ret = check_recal_drive(op->drive_g);
    if (ret)
        return ret;

    // es:bx = pointer to where to place information from diskette
    u32 addr = (u32)op->buf_fl;
    int count = op->count * blocksize;

    // check for 64K boundary overrun
    u16 end = count - 1;
    u32 last_addr = addr + end;
    if ((addr >> 16) != (last_addr >> 16))
        return DISK_RET_EBOUNDARY;

    u8 mode_register = 0x4a; // single mode, increment, autoinit disable,
    if (pio->data[0] == 0xe6)
        // read
        mode_register = 0x46;

    //DEBUGF("floppy dma c2\n");
    outb(0x06, PORT_DMA1_MASK_REG);
    outb(0x00, PORT_DMA1_CLEAR_FF_REG); // clear flip-flop
    outb(addr, PORT_DMA_ADDR_2);
    outb(addr>>8, PORT_DMA_ADDR_2);
    outb(0x00, PORT_DMA1_CLEAR_FF_REG); // clear flip-flop
    outb(end, PORT_DMA_CNT_2);
    outb(end>>8, PORT_DMA_CNT_2);

    // port 0b: DMA-1 Mode Register
    // transfer type=write, channel 2
    outb(mode_register, PORT_DMA1_MODE_REG);

    // port 81: DMA-1 Page Register, channel 2
    outb(addr>>16, PORT_DMA_PAGE_2);

    outb(0x02, PORT_DMA1_MASK_REG); // unmask channel 2

    ret = floppy_select_drive(pio->data[1] & 1);
    if (ret)
        return ret;
    pio->resplen = 7;
    pio->waitirq = 1;
    ret = floppy_pio(pio);
    if (ret)
        return ret;

    // Populate floppy_return_status in BDA
    int i;
    for (i=0; i<7; i++)
        SET_BDA(floppy_return_status[i], pio->data[i]);

    if (pio->data[0] & 0xc0) {
        if (pio->data[1] & 0x02)
            return DISK_RET_EWRITEPROTECT;
        dprintf(1, "floppy error: %02x %02x %02x %02x %02x %02x %02x\n"
                , pio->data[0], pio->data[1], pio->data[2], pio->data[3]
                , pio->data[4], pio->data[5], pio->data[6]);
        return DISK_RET_ECONTROLLER;
    }

    u8 track = (pio->cmdlen == 9 ? pio->data[3] : 0);
    set_diskette_current_cyl(pio->data[0] & 1, track);

    return DISK_RET_SUCCESS;
}


/****************************************************************
 * Floppy handlers
 ****************************************************************/

static void
lba2chs(struct disk_op_s *op, u8 *track, u8 *sector, u8 *head)
{
    u32 lba = op->lba;

    u32 tmp = lba + 1;
    u16 nlspt = GET_GLOBAL(op->drive_g->lchs.spt);
    *sector = tmp % nlspt;

    tmp /= nlspt;
    u16 nlh = GET_GLOBAL(op->drive_g->lchs.heads);
    *head = tmp % nlh;

    tmp /= nlh;
    *track = tmp;
}

// diskette controller reset
static int
floppy_reset(struct disk_op_s *op)
{
    u8 floppyid = GET_GLOBAL(op->drive_g->cntl_id);
    SET_BDA(floppy_recalibration_status, 0);
    SET_BDA(floppy_media_state[0], 0);
    SET_BDA(floppy_media_state[1], 0);
    SET_BDA(floppy_track[0], 0);
    SET_BDA(floppy_track[1], 0);
    SET_BDA(floppy_last_data_rate, 0);
    floppy_disable_controller();
    return floppy_select_drive(floppyid);
}

// Read Diskette Sectors
static int
floppy_read(struct disk_op_s *op)
{
    u8 track, sector, head;
    lba2chs(op, &track, &sector, &head);

    // send read-normal-data command (9 bytes) to controller
    u8 floppyid = GET_GLOBAL(op->drive_g->cntl_id);
    struct floppy_pio_s pio;
    pio.cmdlen = 9;
    pio.data[0] = 0xe6; // e6: read normal data
    pio.data[1] = (head << 2) | floppyid; // HD DR1 DR2
    pio.data[2] = track;
    pio.data[3] = head;
    pio.data[4] = sector;
    pio.data[5] = FLOPPY_SIZE_CODE;
    pio.data[6] = sector + op->count - 1; // last sector to read on track
    pio.data[7] = FLOPPY_GAPLEN;
    pio.data[8] = FLOPPY_DATALEN;

    int res = floppy_cmd(op, DISK_SECTOR_SIZE, &pio);
    if (res)
        goto fail;
    return DISK_RET_SUCCESS;
fail:
    op->count = 0; // no sectors read
    return res;
}

// Write Diskette Sectors
static int
floppy_write(struct disk_op_s *op)
{
    u8 track, sector, head;
    lba2chs(op, &track, &sector, &head);

    // send write-normal-data command (9 bytes) to controller
    u8 floppyid = GET_GLOBAL(op->drive_g->cntl_id);
    struct floppy_pio_s pio;
    pio.cmdlen = 9;
    pio.data[0] = 0xc5; // c5: write normal data
    pio.data[1] = (head << 2) | floppyid; // HD DR1 DR2
    pio.data[2] = track;
    pio.data[3] = head;
    pio.data[4] = sector;
    pio.data[5] = FLOPPY_SIZE_CODE;
    pio.data[6] = sector + op->count - 1; // last sector to write on track
    pio.data[7] = FLOPPY_GAPLEN;
    pio.data[8] = FLOPPY_DATALEN;

    int res = floppy_cmd(op, DISK_SECTOR_SIZE, &pio);
    if (res)
        goto fail;
    return DISK_RET_SUCCESS;
fail:
    op->count = 0; // no sectors read
    return res;
}

// Verify Diskette Sectors
static int
floppy_verify(struct disk_op_s *op)
{
    int res = check_recal_drive(op->drive_g);
    if (res)
        goto fail;

    u8 track, sector, head;
    lba2chs(op, &track, &sector, &head);

    // ??? should track be new val from return_status[3] ?
    u8 floppyid = GET_GLOBAL(op->drive_g->cntl_id);
    set_diskette_current_cyl(floppyid, track);
    return DISK_RET_SUCCESS;
fail:
    op->count = 0; // no sectors read
    return res;
}

// format diskette track
static int
floppy_format(struct disk_op_s *op)
{
    u8 head = op->lba;

    // send format-track command (6 bytes) to controller
    u8 floppyid = GET_GLOBAL(op->drive_g->cntl_id);
    struct floppy_pio_s pio;
    pio.cmdlen = 6;
    pio.data[0] = 0x4d; // 4d: format track
    pio.data[1] = (head << 2) | floppyid; // HD DR1 DR2
    pio.data[2] = FLOPPY_SIZE_CODE;
    pio.data[3] = op->count; // number of sectors per track
    pio.data[4] = FLOPPY_FORMAT_GAPLEN;
    pio.data[5] = FLOPPY_FILLBYTE;

    return floppy_cmd(op, 4, &pio);
}

int
process_floppy_op(struct disk_op_s *op)
{
    if (!CONFIG_FLOPPY)
        return 0;

    switch (op->command) {
    case CMD_RESET:
        return floppy_reset(op);
    case CMD_READ:
        return floppy_read(op);
    case CMD_WRITE:
        return floppy_write(op);
    case CMD_VERIFY:
        return floppy_verify(op);
    case CMD_FORMAT:
        return floppy_format(op);
    default:
        op->count = 0;
        return DISK_RET_EPARAM;
    }
}


/****************************************************************
 * HW irqs
 ****************************************************************/

// INT 0Eh Diskette Hardware ISR Entry Point
void VISIBLE16
handle_0e(void)
{
    if (! CONFIG_FLOPPY)
        return;
    debug_isr(DEBUG_ISR_0e);

    // diskette interrupt has occurred
    u8 frs = GET_BDA(floppy_recalibration_status);
    SET_BDA(floppy_recalibration_status, frs | FRS_IRQ);

    eoi_pic1();
}

// Called from int08 handler.
void
floppy_tick(void)
{
    if (! CONFIG_FLOPPY)
        return;

    // time to turn off drive(s)?
    u8 fcount = GET_BDA(floppy_motor_counter);
    if (fcount) {
        fcount--;
        SET_BDA(floppy_motor_counter, fcount);
        if (fcount == 0)
            // turn motor(s) off
            outb(inb(PORT_FD_DOR) & 0xcf, PORT_FD_DOR);
    }
}
